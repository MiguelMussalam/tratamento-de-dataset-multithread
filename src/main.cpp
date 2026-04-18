#include <vector>
#include <fstream>
#include <iostream>
#include "dataset.hpp"

int main(){

    std::ofstream dataset_tratado("data/dataset_tratado.csv");

    std::ifstream dataset_bruto("data/dataset_raw.csv");

    if (!dataset_bruto.is_open()) {
        std::cerr << "Erro: não foi possível abrir o arquivo" << std::endl;
        return 1;
    }

    std::string line;
    while (std::getline(dataset_bruto, line)) {
        std::cout << line << std::endl;
    }

    dataset_bruto.close();

  return 0;
}