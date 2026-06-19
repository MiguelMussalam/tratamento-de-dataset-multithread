# Arquitetura: Substituição de nth_element por T-Digest na Rotina Numérica

## Contexto

Com o tiling de L3 aplicado à fase de parse, a fase de **Rotina Numérica** passou a ser o
gargalo dominante e absoluto do pipeline multithread:

```
[FASE] Leitura e Parsing:  1671.82 ms
[FASE] Rotina Numerica:    4833.81 ms   ← 74% do tempo total
```

Esse resultado já era esperado: tiling foi aplicado deliberadamente só ao parse (ver
`ARQUITETURA_TILING_L3.md`, seção "Fora de escopo"). A rotina numérica nunca foi tocada e
continua com o mesmo padrão identificado no artigo original — `nth_element` com acesso
aleatório de memória, saturando entre 4 e 8 threads e regredindo a partir daí.

Este documento especifica a substituição do cálculo exato de mediana/IQR via `nth_element`
por uma estimativa via **T-Digest** — estrutura de streaming que processa cada valor uma
única vez, sem cópia do vetor e sem particionamento por pivô.

---

## Diagnóstico — por que nth_element não escala

```cpp
// código atual, src/multithread/dataset.cpp
std::vector<float> valores_para_ordenar(valores_originais);  // cópia O(N) do vetor inteiro

estatisticas.mediana = mediana(valores_para_ordenar);  // nth_element, O(N) médio, single-thread
estatisticas.iqr     = iqr(valores_para_ordenar);      // 2× nth_element, single-thread
```

Três problemas concretos:

1. **Cópia do vetor inteiro** antes de ordenar — para colunas com dezenas de milhões de
   linhas, isso já é uma operação de banda de memória significativa.
2. **`nth_element` é single-thread.** O paralelismo existente no projeto (`#pragma omp
   parallel for schedule(dynamic)` sobre colunas) distribui colunas inteiras entre threads,
   mas dentro de cada coluna o particionamento por pivô do `nth_element` não é paralelizado.
3. **Acesso aleatório de memória.** O particionamento por pivô do `nth_element` salta entre
   posições não sequenciais do vetor — para vetores maiores que a L3, isso é puro
   cache-miss, exatamente como diagnosticado no artigo original (`std::nth_element`
   monopolizando ~25% do tempo de CPU mesmo na versão sequencial).

A paralelização aninhada existente em `media()` e `variancia()`
(`omp_set_max_active_levels(2)`) não se aplica a `mediana()`/`iqr()` — essas duas funções
permanecem inteiramente seriais, e são as mais caras das quatro.

---

## Por que T-Digest resolve isso estruturalmente

T-Digest é uma estrutura de **streaming** para estimativa de quantis — usa uma variante de clustering k-means unidimensional para produzir uma estrutura compacta que permite estimativa precisa de quantis. As propriedades relevantes para este projeto:

- **Uma única passagem.** Cada valor é inserido uma vez via `add()` — sem cópia do vetor,
  sem segunda passagem para reordenar.
- **Paraleliza nativamente.** É possível criar um T-Digest por thread, adicionar dados independentemente, e depois mesclar todos os digests parciais em um digest final via merge — sem lock, sem seção crítica durante a fase de acumulação.
- **Acesso sequencial à memória.** `add()` processa o vetor em ordem — sem saltos de pivô,
  amigável ao prefetcher de hardware (mesmo padrão de acesso que já beneficia `media()` e
  `variancia()` no código atual).
- **Mediana e IQR pelo mesmo objeto.** Quantis 0.25, 0.5 e 0.75 saem do mesmo digest — não
  são mais duas passagens separadas (`mediana()` + `iqr()` chamando `nth_element` duas vezes
  cada).

---

## Biblioteca escolhida

**`derrickburns/tdigest`** (`tdigest2/TDigest.h`) — header-only, sem dependências externas
além da stdlib, API direta:

```cpp
#include "tdigest2/TDigest.h"
using namespace tdigest;

TDigest digest(1000);      // compressão: maior valor = mais precisão, mais memória
digest.add(valor);         // inserção O(1) amortizado
double mediana = digest.quantile(0.5);
double q1      = digest.quantile(0.25);
double q3      = digest.quantile(0.75);
```

O merge de dois digests parciais é suportado diretamente: `digest2.merge(&digest1)` combina os dados de ambos em `digest2` — essa é a operação que viabiliza o paralelismo por thread sem reescrever a estrutura de dados do projeto.

**Por que esta biblioteca e não `folly::TDigest`:** Folly é uma biblioteca monolítica do
Facebook com dezenas de dependências internas — inviável para isolar um único header num
projeto acadêmico com restrição de stdlib-only para o resto do código. `derrickburns/tdigest`
é um header isolado, auditável, sem dependência de build system externo.

**Restrição de stdlib do curso:** T-Digest é a única exceção a essa restrição neste projeto.
Justificativa a documentar para a banca: implementar T-Digest do zero corretamente (clustering
adaptativo, função de escala k, merge de centróides) é um projeto de pesquisa em si — fora do
escopo de uma disciplina de arquitetura de computadores. O valor pedagógico aqui está em
**usar corretamente uma estrutura de streaming para resolver um gargalo de acesso aleatório
de memória diagnosticado empiricamente**, não em reimplementar o algoritmo.

---

## Mudança de estratégia de paralelismo

### Antes (atual)

```cpp
#pragma omp parallel for schedule(dynamic)
for (size_t i = 0; i < num_colunas; i++) {
    rotina_coluna_numerica(i);   // 1 coluna = 1 thread, paralelismo só entre colunas
}

void Dataset::rotina_coluna_numerica(size_t indice_coluna) {
    estatisticas.media     = media(valores_originais);      // reduction paralela interna
    estatisticas.variancia = variancia(valores_originais, estatisticas.media);  // idem

    std::vector<float> valores_para_ordenar(valores_originais);  // cópia O(N)
    estatisticas.mediana = mediana(valores_para_ordenar);  // nth_element serial
    estatisticas.iqr     = iqr(valores_para_ordenar);      // 2× nth_element serial
}
```

### Depois (proposto)

Paralelismo passa a existir em **dois níveis simultâneos**: entre colunas (já existente) e
dentro de cada coluna, via T-Digest por thread.

```cpp
void Dataset::rotina_coluna_numerica(size_t indice_coluna) {
    const std::vector<float>& valores = colunas[indice_coluna].valores;
    size_t n = valores.size();

    estatisticas.media     = media(valores);
    estatisticas.variancia = variancia(valores, estatisticas.media);
    estatisticas.desvio_padrao = desvio_padrao(estatisticas.variancia);

    // --- substituição do bloco nth_element ---
    int num_threads_internas = omp_get_max_threads();
    std::vector<tdigest::TDigest> digests_locais;
    for (int t = 0; t < num_threads_internas; t++)
        digests_locais.emplace_back(1000);  // compressão = 1000, ver seção de tuning

    #pragma omp parallel num_threads(num_threads_internas)
    {
        int tid = omp_get_thread_num();
        #pragma omp for schedule(static)
        for (size_t i = 0; i < n; i++) {
            digests_locais[tid].add(valores[i]);
        }
    }

    // merge serial dos digests parciais — O(num_threads), não O(N)
    tdigest::TDigest digest_final(1000);
    for (auto& d : digests_locais)
        digest_final.merge(&d);

    estatisticas.mediana = digest_final.quantile(0.5);
    double q1 = digest_final.quantile(0.25);
    double q3 = digest_final.quantile(0.75);
    estatisticas.iqr = q3 - q1;
}
```

Funções `mediana()` e `iqr()` como métodos separados de `Dataset` deixam de ser necessárias —
a lógica fica inline na rotina numérica, ou pode ser extraída para um método privado
`calcularQuantis()` se preferir manter a separação de responsabilidades do código atual.

---

## Paralelismo aninhado: revisão necessária

O código atual já tem paralelismo aninhado habilitado (`omp_set_max_active_levels(2)`) para
`media()`/`variancia()`. Com T-Digest também paralelizado internamente, **três níveis de
paralelismo passam a coexistir potencialmente**:

```
nível 1: #pragma omp parallel for schedule(dynamic)     → distribui colunas entre threads
nível 2: #pragma omp parallel for reduction(+:soma)      → dentro de media()/variancia()
nível 2: #pragma omp parallel num_threads(...) for       → dentro do T-Digest paralelo (novo)
```

Isso é um risco real de over-subscription — mais threads de software competindo do que
núcleos físicos disponíveis, especialmente em colunas pequenas onde o overhead de abrir
threads internas supera o ganho.

**Recomendação:** medir antes de assumir. Implementar a versão com T-Digest paralelo
internamente, rodar `perf stat` comparando:

1. T-Digest com paralelismo interno (3 níveis aninhados)
2. T-Digest single-thread por coluna, paralelismo só no nível 1 (colunas)

A decisão de manter ou remover o paralelismo interno do T-Digest deve ser baseada no
`task-clock` e `context-switches` medidos, não assumida — mesma disciplina já aplicada no
restante do projeto.

---

## Trade-off de precisão

T-Digest é uma estrutura de **estimativa**, não cálculo exato. A precisão é controlada pelo
parâmetro de compressão:

| Compressão | Precisão típica em quantis centrais (p50) | Memória por digest |
|---|---|---|
| 100  | erro relativo ~1%   | menor |
| 1000 | erro relativo ~0.1% (recomendado como ponto de partida) | moderado |
| 5000 | erro relativo <0.01% | maior |

Para colunas como `Amount Received` no dataset testado — com mediana 1469.25 e IQR
11627.43 — um erro de 0.1% é irrelevante para fins de estatística descritiva exploratória.
Se a banca questionar a precisão, a resposta correta é: **mediana exata via nth_element
custa O(N) com acesso aleatório de memória; mediana estimada via T-Digest custa O(N) com
acesso sequencial e erro limitado e configurável** — trade-off documentado, não acidental.

**TODO de validação:** comparar numericamente os resultados de `mediana`/`iqr` via T-Digest
contra os valores já obtidos via `nth_element` nos prints existentes (ex.: `From Bank`,
mediana 39024.00) para confirmar que o erro está dentro do esperado antes de descartar de
vez o código antigo.

---

## Fora de escopo

- **Reimplementação do T-Digest do zero** — usar a biblioteca header-only é a decisão
  consciente; ver justificativa na seção "Biblioteca escolhida".
- **Substituir `media()`/`variancia()`** — essas duas já são O(N) com acesso sequencial e
  já paralelizam bem via `reduction`. Não fazem parte do gargalo medido. T-Digest não traz
  ganho aqui.
- **Resolver o bug de tipagem de `From Bank`/`To Bank`** — colunas que são códigos
  categóricos mas passam pelo detector de tipo como NUMERICA. Isso é um problema de
  classificação de tipo, não de performance da rotina numérica, e está fora do escopo deste
  documento.

---

## Resumo do que muda no código

```
include/multithread/dataset.hpp
  + #include "tdigest2/TDigest.h"      [novo arquivo de terceiros, adicionar ao repositório]
  - float mediana(std::vector<float>&)  [removido]
  - float iqr(std::vector<float>&)      [removido]
  + (lógica de quantil movida para dentro de rotina_coluna_numerica, ou método novo)

src/multithread/dataset.cpp
  ~ rotina_coluna_numerica()    [reescrito: T-Digest por thread + merge, sem cópia de vetor]
  - mediana()                   [removido]
  - iqr()                       [removido]
  = media()                     [inalterado]
  = variancia()                 [inalterado]
  = desvio_padrao()             [inalterado]

third_party/ (novo diretório)
  + tdigest2/TDigest.h          [header da biblioteca derrickburns/tdigest]
```
