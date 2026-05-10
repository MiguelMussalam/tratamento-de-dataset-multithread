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
#include <algorithm>

struct StringHash {
    using is_transparent = void;
    size_t operator()(std::string_view sv) const {
        return std::hash<std::string_view>{}(sv);
    }
};

using CategoriaMap = std::unordered_map<std::string, int, StringHash, std::equal_to<>>;

struct EstatisticasNumericas{
  float media = 0;
  float mediana = 0;
  float variancia = 0;
  float desvio_padrao = 0;
  float iqr = 0;
};

enum TipoColuna { CATEGORICA, NUMERICA, DESCONHECIDA };

struct Coluna {
  std::string nome;
  TipoColuna tipo = DESCONHECIDA;

  std::vector<float> valores;
  std::vector<std::string_view> raw_strings; // Salva o ponteiro sem parsear
  CategoriaMap mapeamento;
  std::vector<std::string> categorias;
  std::unique_ptr<EstatisticasNumericas> estatisticas;
};

class Dataset {
private:

  void* mapped = nullptr;
  size_t map_size = 0;
  std::string_view arquivo;

  std::vector<Coluna> colunas;
  std::vector<std::pair<size_t, size_t>> blocos_bytes;
  std::vector<size_t> blocos_linhas_iniciais;
  size_t num_linhas = 0;
  size_t num_colunas = 0;
  size_t cabecalho_size = 0;
  
  void mapearArquivo(const char* caminho);
  void lerCabecalho();
  void inferirTipos();
  void contarLinhasParalelo();
  void alocarVetores();
  void processarLinhasParalelo();
  void processarBloco(size_t inicio_byte, size_t fim_byte, size_t linha_inicial);
  void categorizarColuna(size_t indice_coluna);

  void rotina_coluna_numerica(size_t indice_coluna);
  float media(const std::vector<float>& valores_coluna);
  float variancia(const std::vector<float>& valores_coluna, float media);
  float desvio_padrao(float variancia);
  float mediana(std::vector<float>& valores_coluna);
  float iqr(std::vector<float>& valores_coluna);


public:
  Dataset(const char *caminho);
  void print();
};

/*
  1. LEITURA CHAMADA AO TER O CAMINHO DO ARQUIVO
  2. CATEGORIZAÇÃO CHAMADA DENTRO DA LEITURA PARA TODA COLUNA CATEGÓRICA
  3. REPROCESSARCATEGORIZACAO CHAMADA PARA TODA COLUNA QUE ERA NUMÉRICA E VIRA CATEGÓRICA
*/