#pragma once
#include <string>
#include <vector>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <unordered_map>
#include <memory>
#include <string_view>

struct StringHash {
    using is_transparent = void;
    size_t operator()(std::string_view sv) const {
        return std::hash<std::string_view>{}(sv);
    }
};

struct EstatisticasNumericas{
  double media = 0;
  double mediana = 0;
  double variancia = 0;
  double desvio_padrao = 0;
  double iqr = 0;
};

enum TipoColuna { CATEGORICA, NUMERICA, DESCONHECIDA };

struct Coluna {
  std::string nome;
  TipoColuna tipo = DESCONHECIDA;

  std::vector<double> valores;
  std::unordered_map<std::string, int, StringHash, std::equal_to<>> mapeamento;
  std::vector<std::string> categorias;
  std::unique_ptr<EstatisticasNumericas> estatisticas;
};

class Dataset {
private:
  std::vector<Coluna> colunas;
  size_t num_linhas = 0;
  size_t num_colunas = 0;
  
  void lerArquivo(const char *caminho);

  void categorizar(std::string_view conteudo, size_t indice_coluna);
  void ReprocessarCategorizacao(size_t indice_atual);

  void rotina_coluna_numerica(size_t indice_coluna);
  double media(const std::vector<double>& valores_coluna);
  double variancia(const std::vector<double>& valores_coluna, double media);
  double desvio_padrao(double variancia);
  double mediana(std::vector<double>& valores_coluna);
  double iqr(std::vector<double>& valores_coluna);


public:
  Dataset(const char *caminho);
  void print();
};

/*
  1. LEITURA CHAMADA AO TER O CAMINHO DO ARQUIVO
  2. CATEGORIZAÇÃO CHAMADA DENTRO DA LEITURA PARA TODA COLUNA CATEGÓRICA
  3. REPROCESSARCATEGORIZACAO CHAMADA PARA TODA COLUNA QUE ERA NUMÉRICA E VIRA CATEGÓRICA
*/