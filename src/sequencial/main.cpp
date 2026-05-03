#include "dataset.hpp"
#include <chrono>
#include <iostream>

int main() {
  auto t0 = std::chrono::high_resolution_clock::now();
  Dataset dataset("data/dataset_raw.csv");
  auto t1 = std::chrono::high_resolution_clock::now();

  auto total = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0);

  dataset.print();
  std::cout << "Total: " << total.count() << " ms" << std::endl;
  
  return 0;
}