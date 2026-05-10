# Tratamento de Dataset Multithread em C++

Parser de alto desempenho para grandes datasets CSV. Realiza leitura, *parsing* e **categorização** de dados — convertendo colunas de texto em IDs numéricos via Hash Maps — e extrai estatísticas das colunas numéricas (**Média**, **Mediana**, **Desvio Padrão** e **IQR**).

## Resultados de Performance (dataset de 4GB — `02-20-2018.csv`)

| Versão | Wall Time | Speedup |
|---|---|---|
| Sequencial | ~21 seg | 1× (baseline) |
| **Multithread** | **~5 seg** | **~4×** |

---

## Como a Performance é Alcançada

1. **Zero-Copy Parsing:** Usa `mmap` para mapear o arquivo direto na RAM e `std::string_view` para fatiar strings sem alocações dinâmicas durante a leitura.
2. **Transparent Comparators:** `std::unordered_map` com comparadores transparentes busca strings usando views, eliminando cópias desnecessárias.
3. **Paralelismo por Colunas (OpenMP):** A versão multithread distribui o processamento de colunas entre os núcleos do CPU, atingindo ~4× de speedup em arquivos de 4GB.
4. **Eficiência de Memória (`float` em vez de `double`):** As colunas numéricas usam `float` (4 bytes) em vez de `double` (8 bytes), dobrando a capacidade de dados na RAM e no cache da CPU.
5. **Eliminação de Rehashes:** `reserve(1024)` no `unordered_map` de categorização reduz as realocações de tabela hash durante o processamento.

---

## Estrutura do Repositório

```
.
├── src/
│   ├── sequencial/       # Implementação single-thread (baseline de comparação)
│   └── multithread/      # Implementação com OpenMP (desenvolvimento principal)
├── include/
│   ├── sequencial/
│   └── multithread/
├── resultados_perf/
│   ├── seq/              # perf_stat.txt, perf_functions.txt, flamegraph.svg
│   └── mt/               # perf_stat.txt, perf_functions.txt, flamegraph.svg
├── data/                 # Datasets CSV (não versionados, exceto o de teste)
├── tools/                # FlameGraph (clonado automaticamente, no .gitignore)
└── Makefile
```

---

## Como Executar

O projeto usa um **Makefile** que centraliza compilação, execução e profiling.

O dataset padrão é `data/dataset_raw.csv`. Use a variável `DATASET` para especificar outro arquivo.

### Execução Simples (sem profiling)

```bash
# Versão Sequencial
make run_seq

# Versão Multithread
make run_mt

# Com dataset específico
make run_mt DATASET=data/02-20-2018.csv
```

### Profiling Completo (perf + FlameGraph)

Gera **3 artefatos** por versão em uma única chamada:
- `perf_stat.txt` — métricas de hardware (ciclos, cache misses, page-faults)
- `perf_functions.txt` — hotspots por função (callgraph)
- `flamegraph.svg` — visualização interativa; abra no navegador

```bash
# Profiling da versão Sequencial
make profile_seq DATASET=data/02-20-2018.csv

# Profiling da versão Multithread
make profile_mt DATASET=data/02-20-2018.csv

# Profiling das duas versões em sequência
make profile DATASET=data/02-20-2018.csv
```

> **Nota:** O `perf` requer Linux e pode precisar de `sudo sysctl kernel.perf_event_paranoid=1`.  
> O **FlameGraph** é clonado automaticamente de [brendangregg/FlameGraph](https://github.com/brendangregg/FlameGraph) na primeira execução de `profile_*`. A pasta `tools/` está no `.gitignore`.

### Limpeza

```bash
make clean   # Remove build/ e resultados_perf/*
```

---

## Análise de Gargalos (Perf)

Os principais gargalos identificados via `perf report` são:

| Gargalo | Versão Afetada | % CPU | Causa |
|---|---|---|---|
| Hash lookup no `unordered_map` | Ambas | 10–22% | Linked list fragmentada na heap → L1 cache miss |
| `nth_element` (mediana/IQR) | Ambas | ~21% | Vetor de floats (~20MB) > Cache L3 (~12MB) |
| Contenção no controlador de RAM | Multithread | IPC 1.55 | Múltiplos threads disputando largura de banda |

**IPC (Instructions Per Cycle):**
- Sequencial: **2.10** — pipeline eficiente, memória previsível
- Multithread: **1.55** — contenção de memória, mas paralelismo compensa

O speedup real de ~4× com eficiência paralela de **~80%** é considerado excelente para workloads com essa intensidade de acesso à memória.
