# Tratamento de Dataset Multithread em C++

Parser de altíssimo desempenho para leitura e tratamento estruturado de grandes arquivos CSV. 
O projeto utiliza um gerenciamento agressivo de memória com a técnica "Zero-Copy", projetado para processar datasets gigantes com a menor quantidade de memória RAM (Heap) possível e extraindo a velocidade máxima do processador.

Realiza leitura paralela, *parsing*, e **categorização** automática dos dados — convertendo colunas textuais em IDs numéricos utilizando Dicionários e Hash Maps enxutos — além de extrair estatísticas pesadas (**Média**, **Mediana**, **Desvio Padrão** e **IQR**).

---

## Principais Pilares da Arquitetura

1. **Mapeamento de Disco (mmap):** O arquivo não é lido tradicionalmente; ele é projetado da ponte norte direto para a memória RAM. O código é multiplataforma e suporta leitura nativa avançada tanto em sistemas Linux (POSIX) quanto no Windows (WinAPI `MapViewOfFile`).
2. **Reservatório de Strings (StringPool):** As informações de texto não disparam a criação de classes `std::string` soltas na memória. O projeto engloba um alocador customizado que insere milhões de ocorrências de texto inéditas em blocos contíguos perfeitos de 16 MB. Isso elimina a fragmentação do Heap e protege o código contra panes de estouro de memória (Out Of Memory).
3. **Descarte Inteligente (madvise):** A comunicação direta com o Kernel ordena que as páginas de memória derivadas do cache de disco sejam destruídas e limpas silenciosamente assim que o parser não precisa mais visualizá-las.

---

## Sequencial vs Multithread

O repositório contém dois códigos isolados dentro de `src/` para comparativo de abordagens:

### Versão Sequencial
Desenvolvida como base (*baseline*), essa versão processa os blocos e linhas do arquivo de forma linear e ininterrupta em uma única passagem. Por ser linear, seu ponto forte é o descarte sincronizado de memória (o Kernel sabe exatamente que uma página pode sumir sem comprometer a frente). Ela utiliza variáveis `double` (8 bytes) e processa muito bem datasets imensos sem apressar a CPU.

### Versão Multithread (OpenMP)
A versão principal. Ela esquarteja a carga de trabalho entre todos os núcleos de CPU disponíveis no computador.
Para que a memória e o Cache L3 acompanhassem as requisições massivas e simultâneas de processamento, essa versão precisou ser rebaixada para utilizar matemática estruturada em `float` (4 bytes).
Embora no pico exija retenção de um volume um pouco maior de blocos de disco mapeados, a economia de tempo ganha pelo paralelismo a torna infinitamente superior (um arquivo de 4 GB leva questão de pouquíssimos segundos para gerar todas as categorias e métricas).

---

## Estrutura do Repositório

```
.
├── src/
│   ├── sequencial/       # Código Fonte Linear
│   └── multithread/      # Código Fonte Paralelizado
├── include/
│   ├── sequencial/       # Cabeçalhos
│   └── multithread/      # Cabeçalhos
├── data/                 # Datasets CSV (Adicione o seu aqui)
└── Makefile              # Orquestrador de Build e Profiling
```

---

## Como Executar

O projeto usa um **Makefile** que centraliza a compilação, execução e profiling da máquina.

O dataset padrão é `data/dataset_raw.csv`. Use a variável de injeção `DATASET` para especificar o seu próprio arquivo.

### Execução Simples

```bash
# Compila e roda a versão Sequencial
make run_seq DATASET=data/02-20-2018.csv

# Compila e roda a versão Multithread (A mais veloz)
make run_mt DATASET=data/02-20-2018.csv
```

### Profiling Avançado de Desempenho (Linux)

Se desejar investigar a fundo métricas como uso de Cache, Page-Faults ou tempo por função no Linux, gere as estatísticas com a suite do `perf`.

```bash
# Rastreamento Multithread
make profile_mt DATASET=data/02-20-2018.csv
```

Isso criará uma pasta chamada `resultados_perf` com gráficos visuais em vetor e logs profundos do sistema operacional de como o seu CPU se comportou.

### Limpeza

```bash
# Remove pastas de build, binários compilados e rastros de profiling
make clean
```
