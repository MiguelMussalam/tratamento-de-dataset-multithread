#include "dataset.hpp"
#include <chrono>
#include <iostream>

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::cerr << "Uso: " << argv[0] << " <caminho_do_dataset.csv>\n";
    std::cerr << "Exemplo: " << argv[0] << " data/dataset_raw.csv\n";
    return 1;
  }

  auto t0 = std::chrono::high_resolution_clock::now();
  Dataset dataset(argv[1]);
  auto t1 = std::chrono::high_resolution_clock::now();

  auto total = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0);

  dataset.print();
  std::cout << "Total: " << total.count() << " ms" << std::endl;
  
  return 0;
}