#include "dataset.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include <charconv>

Dataset::Dataset(const std::string& caminho) {
    std::cout << "Iniciando processamento multithread Produtor-Consumidor..." << std::endl;

    // Abre o arquivo e lê só o cabeçalho para descobrir as colunas
    std::ifstream arquivo(caminho);
    if (!arquivo.is_open()) {
        std::cerr << "Erro ao abrir arquivo" << std::endl;
        exit(1);
    }
    
    std::string linha;
    if (std::getline(arquivo, linha)) {
        std::stringstream ss(linha);
        std::string celula;

        // instanciar colunas
        while (std::getline(ss, celula, ',')) {
            auto col = std::make_unique<Coluna>();
            col->nome = celula;
            colunas.push_back(std::move(col));
            num_colunas++;
        }
    }
    arquivo.close();

    // Inicia os Consumidores (uma thread por coluna)
    std::vector<std::thread> consumidores;
    for (int j = 0; j < num_colunas; j++) {
        consumidores.push_back(std::thread(&Dataset::categorizarColuna, this, j));
    }

    // O Produtor (lerArquivo) rodará na thread principal
    lerArquivo(caminho);

    // Aguarda todos os consumidores terminarem
    for (auto& t : consumidores) {
        t.join();
    }

    // Imprime resultados
    for (int j = 0; j < num_colunas; j++) {
        std::cout << "Coluna[" << j << "] " << colunas[j]->nome 
                  << ": " << colunas[j]->tempo_categorizacao << " us" << std::endl;
    }
}

void Dataset::lerArquivo(const std::string& caminho) {
    auto t0 = std::chrono::high_resolution_clock::now();
    std::ifstream arquivo(caminho);
    if (!arquivo.is_open()) {
        std::cerr << "Erro ao abrir arquivo" << std::endl;
        return;
    }

    std::string linha;
    std::getline(arquivo, linha); // pula o cabeçalho já processado

    // Preparar blocos para o produtor
    const size_t TAMANHO_BLOCO = 1000;
    std::vector<std::vector<std::string>> blocos_por_coluna(num_colunas);
    for (int j = 0; j < num_colunas; j++) {
        blocos_por_coluna[j].reserve(TAMANHO_BLOCO);
    }

    size_t contador_linhas = 0;

    // Produtor: lê o arquivo e despacha para as filas das colunas
    while (std::getline(arquivo, linha)) {
        size_t inicio = 0;
        size_t fim = linha.find(',');
        int j = 0;
        
        while (fim != std::string::npos && j < num_colunas) {
            blocos_por_coluna[j].emplace_back(linha.data() + inicio, fim - inicio);
            inicio = fim + 1;
            fim = linha.find(',', inicio);
            j++;
        }
        if (j < num_colunas) {
            blocos_por_coluna[j].emplace_back(linha.data() + inicio, linha.size() - inicio);
        }

        contador_linhas++;

        // Quando o bloco enche, envia para os consumidores
        if (contador_linhas == TAMANHO_BLOCO) {
            for (int k = 0; k < num_colunas; k++) {
                colunas[k]->fila_dados.push(std::move(blocos_por_coluna[k]));
                // std::move deixa o vetor em estado válido mas vazio, precisa garantir capacidade
                blocos_por_coluna[k].clear();
                blocos_por_coluna[k].reserve(TAMANHO_BLOCO);
            }
            num_linhas += contador_linhas;
            contador_linhas = 0;
        }
    }

    // Enviar as linhas restantes que não completaram um bloco cheio
    if (contador_linhas > 0) {
        for (int k = 0; k < num_colunas; k++) {
            colunas[k]->fila_dados.push(std::move(blocos_por_coluna[k]));
        }
        num_linhas += contador_linhas;
    }

    // Sinalizar fim de arquivo para todas as filas
    for (int k = 0; k < num_colunas; k++) {
        colunas[k]->fila_dados.setFinished();
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    auto total = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0);
    std::cout << "Produtor finalizou a leitura em: " << total.count() << " us" << std::endl;
}

void Dataset::categorizarColuna(int indice_coluna) {
    auto t0 = std::chrono::high_resolution_clock::now();
    Coluna& col = *colunas[indice_coluna];

    col.tipo = NUMERICA;

    std::vector<std::string> bloco;
    
    // Consumidor: puxa blocos da fila até que ela esteja vazia e o produtor tenha finalizado
    while (col.fila_dados.pop(bloco)) {
        for (const auto& celula : bloco) {
            double v;
            auto [ptr, ec] = std::from_chars(celula.data(), celula.data() + celula.size(), v);
            
            // Se falhar a conversão para double, a coluna é considerada categórica
            if (ec != std::errc() || ptr != celula.data() + celula.size()) {
                col.tipo = CATEGORICA;
                
                // Trata mapeamento categórico
                if (col.mapeamento.find(celula) == col.mapeamento.end()) {
                    col.mapeamento[celula] = col.mapeamento.size();
                }
                col.valores.push_back(col.mapeamento[celula]);
            } else {
                col.valores.push_back(v);
            }
        }
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    col.tempo_categorizacao = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
}

const Coluna& Dataset::getColuna(int indice) const {
    return *colunas[indice];
}