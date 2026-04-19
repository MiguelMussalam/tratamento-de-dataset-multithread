#include "dataset.hpp"
#include <fstream>
#include <iostream>
#include <sstream>

Dataset::Dataset(const std::string& caminho) {
    lerArquivo(caminho);
}

void Dataset::lerArquivo(const std::string& caminho) {
    std::ifstream arquivo(caminho);
    if (!arquivo.is_open()) {
        std::cerr << "Erro ao abrir arquivo" << std::endl;
        exit(1);
    }

    std::string linha;
    std::getline(arquivo, linha);
    std::stringstream ss(linha);
    std::string celula;

    // instanciar e nome de cols
    while (std::getline(ss, celula, ',')) {
        Coluna col;
        col.nome = celula;
        colunas.push_back(col);
        num_colunas++;
    }

    // conta qtd linha
    while (std::getline(arquivo, linha)) {
        num_linhas++;
    }


    // alocar linhas
    for (auto& col : colunas) {
        col.vals.resize(num_linhas);
    }

    // reset
    arquivo.clear();
    arquivo.seekg(0);

    // pular cabecaalho
    std::getline(arquivo, linha);

    // popular estrutura do dataset
    int i = 0;
    while (std::getline(arquivo, linha)) {
        std::stringstream ss2(linha);
        int j = 0;
        while (std::getline(ss2, celula, ',')) {
            colunas[j].vals[i] = celula;
            j++;
        }
        i++;
    }
}
void Dataset::processarColuna(int indice_coluna) {
}