# Como executar o projeto

## Requisito

- GCC com suporte a **C++20** e **OpenMP**

---

## 1. Coloque o dataset na pasta `data/`

Copie o seu arquivo CSV para dentro da pasta `data/`:

```
tratamento-de-dataset-multithread/
└── data/
    └── seu_dataset.csv   ← coloque aqui
```

---

## 2. Compile e execute

```bash
make run_mt DATASET=data/seu_dataset.csv
```
