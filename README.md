# Tratamento de Dataset Multithread em C++

Implementação em C++ de um *parser* e processador de arquivos CSV de grande porte, desenvolvido com o objetivo de comparar o desempenho entre execução sequencial e execução paralela (OpenMP) nas etapas de leitura, *parsing*, categorização e cálculo de estatísticas descritivas.

---

## Objetivos

- Implementar e comparar duas estratégias de processamento: sequencial e multithread.
- Minimizar o consumo de memória RAM por meio de técnicas de mapeamento de arquivo e gerenciamento explícito de páginas.
- Extrair estatísticas descritivas (média, mediana, desvio padrão e IQR) para colunas numéricas.
- Realizar a codificação de colunas categóricas por mapeamento de dicionário.
- Quantificar o ganho de desempenho obtido com a paralelização via métricas controladas.

---

## Arquitetura

### Mapeamento de Arquivo (`mmap` / `MapViewOfFile`)

O arquivo CSV não é lido por operações de I/O tradicionais (`fread`, `ifstream`). Em vez disso, é projetado diretamente no espaço de endereçamento virtual do processo via `mmap` (POSIX) ou `MapViewOfFile` (Windows). Dessa forma, o acesso aos dados ocorre por referência direta à memória virtual, evitando cópias intermediárias entre *kernel space* e *user space* — técnica denominada *zero-copy*.

### Reservatório de Strings (*StringPool*)

Colunas categóricas potencialmente contêm milhões de valores textuais distintos. A alocação individualizada de objetos `std::string` para cada ocorrência causaria fragmentação de *heap* e pressão excessiva sobre o alocador. Para contornar isso, implementou-se um alocador linear (*StringPool*) que insere os dados de texto em blocos contíguos de 16 MB, retornando `std::string_view` como referência sem posse. Isso elimina fragmentação e reduz o número de chamadas ao sistema.

### Descarte de Páginas (`madvise MADV_DONTNEED`)

Após o processamento de cada bloco do arquivo, o programa emite uma chamada `madvise(MADV_DONTNEED)` sobre as páginas correspondentes. Essa instrução sinaliza ao *kernel* que as páginas podem ser descartadas do *page cache*, liberando memória física sem desmapar o arquivo. Essa técnica é particularmente relevante para arquivos que excedem a RAM disponível.

### Inferência de Tipos

Na versão multithread, o tipo de cada coluna (numérica ou categórica) é inferido a partir das primeiras 10 linhas do arquivo antes do *parsing* completo, permitindo que a alocação dos vetores de resultado seja feita de forma prévia e contígua (`std::vector::resize`), sem realocações incrementais durante o processamento.

---

## Estrutura do Repositório

```
.
├── src/
│   ├── sequencial/       # Implementação sequencial
│   └── multithread/      # Implementação paralela (OpenMP)
├── include/
│   ├── sequencial/       # Cabeçalhos da versão sequencial
│   └── multithread/      # Cabeçalhos da versão multithread
├── data/                 # Datasets CSV de entrada
├── tools/                # FlameGraph e utilitários de profiling
├── resultados_perf/      # Saídas do perf (geradas em tempo de execução)
├── benchmark_results.csv # Dados brutos das medições de tempo
└── Makefile              # Automação de build, execução e profiling
```

---

## Estratégia de Paralelização (OpenMP)

A versão multithread aplica paralelismo em dois níveis:

**Nível 1 — Parsing do arquivo:** O arquivo é dividido em blocos de bytes, um por thread. Cada thread processa suas linhas de forma independente e escreve os resultados em posições pré-alocadas do vetor, sem disputa de memória (*race-free by design*). A contagem prévia de linhas por bloco (`contarLinhasParalelo`) garante que os índices de escrita sejam calculados antes do processamento.

**Nível 2 — Rotina numérica:** O laço sobre as colunas é paralelizado com `#pragma omp parallel for schedule(dynamic)`. Dentro desse laço, as funções `media()` e `variancia()` utilizam `#pragma omp parallel for reduction(+:soma)`, criando sub-regiões paralelas por coluna. O aninhamento é habilitado com `omp_set_max_active_levels(2)`.

As operações de ordenação (`mediana` e `iqr`), baseadas em `std::nth_element`, são executadas sequencialmente sobre uma única cópia do vetor para evitar o custo de memória de cópias adicionais.

---

## Resultados

### Dataset utilizado

`02-20-2018.csv` — tráfego de rede para detecção de intrusão (CIC-IDS-2018).  
Dimensões: ~5,03 milhões de linhas × 84 colunas (78 numéricas, 6 categóricas).

### Benchmark de tempo total (n=97 rodadas estabilizadas, descartadas as 3 primeiras)

| Versão       | Média (ms) | Mediana (ms) | Desvio Padrão (ms) | Mínimo (ms) | Máximo (ms) |
|:-------------|----------:|-------------:|-------------------:|------------:|------------:|
| Sequencial   | 19.330    | 18.842       | 2.126              | 18.124      | 33.321      |
| Multithread  | 4.672     | 4.684        | 117                | 4.426       | 4.945       |

**Speedup observado:** ~4,0× (razão entre medianas) com número de threads igual ao de núcleos lógicos disponíveis.

> As primeiras rodadas da versão sequencial apresentam tempos elevados (até 67 s) decorrentes de *cold cache* de disco. A partir da terceira rodada, o *page cache* do sistema operacional estabiliza os tempos. As rodadas descartadas não compõem as estatísticas acima.

### Análise por fase e número de threads

Medições obtidas com `omp_get_wtime()` por fase, variando `OMP_NUM_THREADS`:

| Threads | Parsing (ms) | Categoriz. + Numérica (ms) | Total (ms) | Speedup vs. 1 thread |
|:-------:|-------------:|---------------------------:|-----------:|:--------------------:|
| 1       | 7.714        | 7.956                      | 15.719     | 1,00×                |
| 2       | 4.398        | 4.172                      | 8.619      | 1,82×                |
| 4       | 2.754        | 2.317                      | 5.120      | 3,07×                |
| 8       | 2.249        | 2.749                      | 5.048      | 3,11×                |

O ganho de 4 para 8 threads é marginal (~1,4%). A fase de categorização e rotina numérica regride levemente com 8 threads em relação a 4, indicando que o gargalo deixa de ser CPU e passa a ser a largura de banda de memória (*memory-bound*). O ponto de operação ótimo para este dataset neste hardware foi de **4 threads**.

---

## Como Executar

```bash
# Compila e executa a versão sequencial
make run_seq DATASET=data/02-20-2018.csv

# Compila e executa a versão multithread
make run_mt DATASET=data/02-20-2018.csv
```

### Profiling com `perf` (Linux)

```bash
# Gera perf_stat.txt, perf.data e flamegraph.svg em resultados_perf/
make profile_seq DATASET=data/02-20-2018.csv
make profile_mt  DATASET=data/02-20-2018.csv
```

Os contadores coletados incluem: `task-clock`, `page-faults`, `branches`, `branch-misses`, `instructions`, `cycles`, `L1-dcache-load-misses`, `LLC-loads` e `LLC-load-misses`.

### Limpeza

```bash
make clean
```

---

## Compilação

O projeto requer GCC com suporte a C++20 e OpenMP.

```
Flags: -O3 -g -fno-omit-frame-pointer -std=c++20 -pthread -fopenmp
```
