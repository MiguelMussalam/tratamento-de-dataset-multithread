#pragma once
#include <vector>
#include <string>

enum TipoColuna{
  CATEGORICA,
  NUMERICA,
  DESCONHECIDA
};

struct Coluna{
  std::string nome;
  TipoColuna tipo = DESCONHECIDA;

  std::vector<double> vals_numerico;
  std::vector<int> vals_categorico;
  std::vector<std::string> vals_n_numerico;
};

struct dataset{
  std::vector<Coluna> Colunas;
  std::size_t tamanho_dataset;
};