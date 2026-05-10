# Tratamento de Dataset Multithread em C++

Este projeto é um processador de grandes volumes de dados (datasets gigantes em CSV, como datasets de tráfego de rede) otimizado para extrair máxima performance do hardware. Ele realiza a leitura, *parsing* e **categorização** dos dados convertendo colunas de texto em IDs numéricos únicos usando Hash Maps, extraindo também estatísticas como Média, Desvio Padrão e IQR das colunas numéricas.

## Como a Performance é Alcançada
1. **Zero-Copy Parsing:** O projeto utiliza `mmap` para mapear o arquivo direto na memória RAM e `std::string_view` para fatiar as strings sem alocações dinâmicas na leitura das linhas.
2. **Estruturas Nativas:** Utiliza `std::unordered_map` em C++20 com "Transparent Comparators" para buscar strings usando views, economizando processamento e reduzindo *cache misses*.
3. **Análise de Hardware:** O repositório vem acoplado com um script de *profiling* utilizando o **Linux Perf**, gerando métricas de ciclos de CPU, falhas de página (*page-faults*), *branch-misses* e *callgraphs*.

## Estrutura do Repositório

O projeto atingiu uma maturidade em que a complexidade do algoritmo base se esgotou. Para ir além, o repositório está dividido em duas frentes de execução:

- `src/sequencial/` e `include/sequencial/`: Contém a implementação base (Single-thread). Ela serve como o *baseline* de tempo e confiança estatística do nosso algoritmo.
- `src/multithread/` e `include/multithread/`: É o ambiente principal de desenvolvimento atual. Aqui a paralelização é aplicada (usando partições de chunks, mutexes ou thread-pools) para dividir as leituras e escritas entre múltiplos núcleos de processamento.
- `resultados_perf/`: Diretório que armazena os relatórios de compilação gerados pelo comando Perf. Possui subpastas (`seq/` e `mt/`) para que possamos comparar lado a lado o ganho de ciclos da versão Multithread contra a versão Sequencial.

## Como Executar

Este projeto utiliza um **Makefile** para facilitar a compilação, execução rápida e o *profiling* com a ferramenta `perf`.

Por padrão, a execução rápida usa um dataset de testes menor (`data/dataset_raw.csv`), para agilizar verificações de código. Você pode sempre especificar qual arquivo quer rodar passando a variável `DATASET`.

### 1. Execução Rápida (Sem Profiling de Hardware)
Ideal para testar se o código compila e não dá erro, mensurando apenas o tempo de relógio:

```bash
# Executa a versão Sequencial (com o dataset padrão)
make run_seq

# Executa a versão Multithread (com o dataset padrão)
make run_mt

# Para especificar um arquivo gigante de 4GB:
make run_seq DATASET=data/02-20-2018.csv
```

### 2. Gerar Relatórios de Performance (Com `perf`)
Use isso caso você esteja num ambiente Linux suportado e queira mapear instruções de CPU, falhas de cache L1/L3, etc. *Nota: Pode exigir permissão de superusuário ou a configuração sysctl `kernel.perf_event_paranoid`.*

```bash
# Gera relatórios detalhados para o algoritmo base
make profile_seq DATASET=data/02-20-2018.csv

# Gera relatórios detalhados para o algoritmo multithread
make profile_mt DATASET=data/02-20-2018.csv
```

*Os relatórios serão salvos dentro de `resultados_perf/seq/` ou `resultados_perf/mt/`, incluindo o `perf_stat.txt` (métricas de hardware bruto) e o `perf_functions.txt` (análise temporal de cada função chamada).*
