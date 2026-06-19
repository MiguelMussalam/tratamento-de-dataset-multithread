# Arquitetura: Parser Multithread Cache-Aware (L3 Tiling)

## Contexto

A versão atual do parser multithread divide o arquivo CSV em **N blocos grandes**, um por
thread (`contarLinhasParalelo` + `processarLinhasParalelo`). Cada thread processa seu bloco
inteiro de uma vez — para um arquivo de 4 GB com 16 threads, isso significa ~250 MB por
thread.

O profiling (`perf stat`) identificou que essa abordagem causa **self-eviction**: o working
set gerado durante o processamento de um bloco (hash map de categorias, vetores de saída,
`string_view` de strings brutas) cresce muito além da capacidade da cache L3 antes do bloco
terminar. O resultado medido foi LLC miss rate subindo de 47,8% (sequencial) para 72,9%
(multithread), e IPC caindo de 2,30 para 1,48.

Este documento especifica uma nova estratégia de particionamento do trabalho de parse —
**tiling consciente do tamanho da L3** — para reduzir esse efeito.

---

## Princípio teórico

> **Importante — leia antes de implementar:** dimensionar o chunk pelo tamanho da L3 **não
> cria partições exclusivas de cache por thread**. Isso é fisicamente impossível via software
> puro: a L3 é *set-associative*, e o mapeamento de um endereço físico para um *set* da cache
> é determinado pelo controlador de memória, não pela aplicação. Múltiplas threads processando
> chunks diferentes podem perfeitamente colidir nos mesmos sets da L3, independente do tamanho
> do chunk escolhido. Partições exclusivas reais só existem via *Intel CAT (Cache Allocation
> Technology)*, que está fora do escopo deste projeto.

O ganho real desta abordagem vem de **localidade temporal**, não de exclusividade espacial:

- Um chunk pequeno o suficiente para caber na L3 mantém seu working set inteiro "quente"
  durante todo o seu próprio processamento — hash map, `string_view`, vetores de saída
  permanecem acessíveis sem ida à RAM.
- Um chunk grande demais causa **self-eviction**: pelo tempo que a thread termina de
  processar o fim do chunk, as estruturas geradas no início já foram expulsas da cache pelo
  próprio volume de dados gerado no meio/fim — independente de qualquer outra thread.
- Como efeito colateral (não como objetivo primário), chunks menores também reduzem a
  pressão agregada instantânea sobre a L3 compartilhada, o que **mitiga, mas não elimina**, a
  contenção entre threads.

A justificativa formal para o markdown e para a banca:

> O dimensionamento de chunk pelo tamanho da L3 não cria partições exclusivas de cache por
> thread — isso é fisicamente impossível via software sem suporte de hardware como Intel CAT.
> O ganho real vem de limitar o working set instantâneo de cada unidade de trabalho, de forma
> que as estruturas geradas durante o processamento de um chunk tenham alta probabilidade de
> permanecer em cache até serem reconsultadas, evitando self-eviction por volume excessivo de
> dados processados em sequência.

---

## Escopo da mudança

| Fase | Muda? | Motivo |
|---|---|---|
| `contarLinhasParalelo()` | **Não** | Operação read-only, sem estado acumulado por byte — working set por iteração é ~0. Não sofre self-eviction. Gargalo é bandwidth, não localidade. |
| `alocarVetores()` | **Não** | Continua fazendo `resize` exato com `num_linhas` já conhecido. |
| `processarLinhasParalelo()` | **Sim** | Passa a consumir de uma fila de chunks pequenos em vez de 1 bloco grande por thread. |
| `categorizarColuna()` | **Não** | Continua por coluna, fora do esquema de tiling. Decisão consciente — ver seção "Fora de escopo". |

---

## Modelo de distribuição: `schedule(dynamic)` sobre vetor de chunks

Em vez de `omp parallel` puro com 1 bloco fixo por `tid` (modelo atual), o novo
`processarLinhasParalelo` pré-calcula um vetor de chunks pequenos e usa
`#pragma omp parallel for schedule(dynamic, 1)` para distribuição dinâmica nativa do
OpenMP — sem fila manual, sem `std::atomic` de controle.

```cpp
struct ChunkInfo {
    size_t inicio_byte;
    size_t fim_byte;
    size_t linha_inicial;
};

std::vector<ChunkInfo> chunks = calcularChunks(); // ver seção de tamanho de chunk

#pragma omp parallel for schedule(dynamic, 1)
for (size_t i = 0; i < chunks.size(); i++) {
    processarBloco(chunks[i].inicio_byte, chunks[i].fim_byte, chunks[i].linha_inicial);
}
```

Qualquer thread livre pega o próximo chunk da fila interna do OpenMP — não há mais
associação fixa `tid → bloco`.

### Boundary handling (alinhamento de linha)

Mesma lógica já usada em `contarLinhasParalelo` — reaproveitar, não reinventar:

```cpp
// ao calcular cada ChunkInfo, alinhar inicio_byte e fim_byte ao próximo '\n'
// exatamente como já é feito hoje para os blocos grandes
```

`linha_inicial` de cada chunk é conhecida porque a contagem de linhas (fase que não muda)
já fornece os offsets — basta recalculá-los na granularidade do chunk pequeno em vez do
bloco grande, usando a mesma função `contar_newlines` (AVX2) já existente.

### Escrita em `valores[]` e `raw_strings[]`

Continua sendo escrita por índice absoluto de linha — `valores[linha_inicial + i]` — exatamente
como hoje. Isso permanece seguro sem lock porque **os índices de linha nunca se sobrepõem
entre chunks**, independente de qual thread processa qual chunk em qual ordem.

---

## Detecção de tamanho da L3 (runtime, multiplataforma)

O projeto já usa `#ifdef _WIN32` / `#else` em `dataset.hpp` para mmap. A detecção de cache
segue o mesmo padrão — função única, implementação condicional por SO.

### Linux

```cpp
#ifndef _WIN32
#include <fstream>
#include <string>

size_t detectarTamanhoL3() {
    for (int idx = 2; idx <= 3; idx++) {
        std::string caminho_level = "/sys/devices/system/cpu/cpu0/cache/index"
                                     + std::to_string(idx) + "/level";
        std::string caminho_size  = "/sys/devices/system/cpu/cpu0/cache/index"
                                     + std::to_string(idx) + "/size";

        std::ifstream level_file(caminho_level);
        std::ifstream size_file(caminho_size);
        if (!level_file || !size_file) continue;

        int level;
        level_file >> level;
        if (level != 3) continue;

        std::string size_str;
        size_file >> size_str; // formato: "13312K" ou "13M"

        size_t valor = std::stoul(size_str);
        if (size_str.back() == 'K') return valor * 1024;
        if (size_str.back() == 'M') return valor * 1024 * 1024;
        return valor;
    }
    return 8 * 1024 * 1024; // fallback conservador: 8 MB
}
#endif
```

### Windows

```cpp
#ifdef _WIN32
#include <windows.h>
#include <vector>

size_t detectarTamanhoL3() {
    DWORD tamanho_buffer = 0;
    GetLogicalProcessorInformation(nullptr, &tamanho_buffer);

    std::vector<char> buffer(tamanho_buffer);
    auto* info = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION*>(buffer.data());

    if (!GetLogicalProcessorInformation(info, &tamanho_buffer)) {
        return 8 * 1024 * 1024; // fallback conservador: 8 MB
    }

    size_t count = tamanho_buffer / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
    for (size_t i = 0; i < count; i++) {
        if (info[i].Relationship == RelationCache &&
            info[i].Cache.Level == 3) {
            return info[i].Cache.Size;
        }
    }
    return 8 * 1024 * 1024; // fallback conservador: 8 MB
}
#endif
```

`GetLogicalProcessorInformation` segue o padrão de duas chamadas do Win32 (primeira para
descobrir o tamanho do buffer necessário, segunda para preenchê-lo) — o mesmo padrão já
usado implicitamente em `fstat` + `mmap` no lado Linux do projeto.

---

## Fórmula de dimensionamento do chunk

```cpp
constexpr double FATOR_SEGURANCA = 0.75; // ver justificativa abaixo

size_t calcularTamanhoChunk(size_t l3_bytes, int num_threads_ativas) {
    size_t l3_utilizavel = static_cast<size_t>(l3_bytes * FATOR_SEGURANCA);
    return l3_utilizavel / num_threads_ativas;
}
```

```
L3_total        = detectarTamanhoL3()
L3_utilizavel   = L3_total * FATOR_SEGURANCA
chunk_csv_bytes = L3_utilizavel / num_threads_ativas
```

### Fator de segurança (0.75)

A L3 não é exclusiva do processo — o sistema operacional, processos em segundo plano e o
próprio runtime do programa (stack, código, outras alocações) também ocupam espaço nela.
Mirar 100% da capacidade nominal garante thrashing induzido pelo SO. O fator de 0.75 é uma
margem inicial — prática comum em cache blocking de computação científica, que tipicamente
opera entre 50–80% da capacidade nominal do nível de cache alvo.

### TODO — overhead de estruturas geradas (não implementar ainda)

O `chunk_csv_bytes` calculado acima representa apenas o **texto bruto do CSV**. Durante o
processamento, cada chunk também gera estruturas nativas (entradas de `unordered_map`,
`string_view` em `raw_strings`, valores em `valores`) que ocupam espaço adicional na L3 ao
lado do texto sendo lido. A pressão real sobre a cache é maior que o tamanho do chunk em
bytes de texto.

**Decisão:** implementar primeiro com a fórmula simples acima (sem fator de correção para
estruturas geradas). Medir com `perf stat` o L1/LLC miss rate resultante. Só então ajustar
a constante empiricamente com base no dado medido — evitar chutar um fator de correção sem
evidência, seguindo a mesma disciplina já usada no restante do projeto (decisões sempre
fundamentadas em números do `perf`, não em estimativa).

---

## Fora de escopo (decisão consciente)

- **`categorizarColuna()` permanece como está hoje** — por coluna, com
  `schedule(dynamic)` sobre o índice de coluna, sem tiling. Aplicar tiling também na
  categorização exigiria hash maps locais por chunk e merge de índices entre threads —
  reintroduzindo o custo de merge que o projeto deliberadamente evitou ao adotar offsets
  pré-calculados (ver decisão anterior do projeto: offsets vs. merge).
- **`contarLinhasParalelo()` permanece com blocos grandes** — sem tiling. Justificativa
  na tabela de escopo acima.
- **Intel CAT / cache partitioning real de hardware** — fora do escopo deste projeto
  acadêmico; mencionado apenas como contraste teórico para justificar por que esta
  abordagem não cria exclusividade real de cache.

---

## Resumo do que muda no código

```
include/multithread/dataset.hpp
  + struct ChunkInfo { inicio_byte, fim_byte, linha_inicial }
  + size_t detectarTamanhoL3()                    [Linux + Windows via #ifdef]
  + size_t calcularTamanhoChunk(l3_bytes, n_threads)
  + std::vector<ChunkInfo> calcularChunks()        [novo método privado de Dataset]

src/multithread/dataset.cpp
  ~ processarLinhasParalelo()                      [reescrito: schedule(dynamic) sobre chunks]
  = processarBloco()                               [assinatura inalterada]
  = contarLinhasParalelo()                         [inalterado]
  = alocarVetores()                                [inalterado]
  = categorizarColuna()                            [inalterado]
```
