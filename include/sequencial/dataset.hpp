#pragma once
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif
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

using CategoriaMap = std::unordered_map<std::string_view, int, StringHash, std::equal_to<>>;

struct EstatisticasNumericas{
  double media = 0;
  double mediana = 0;
  double variancia = 0;
  double desvio_padrao = 0;
  double iqr = 0;
};

enum TipoColuna { CATEGORICA, NUMERICA, DESCONHECIDA };

class StringPool {
private:
    std::vector<std::unique_ptr<char[]>> blocos;
    size_t deslocamento_atual = 0;
    size_t capacidade_atual = 0;
    const size_t tamanho_padrao_bloco = 16 * 1024 * 1024; // 16 MB

public:
    std::string_view adicionar(std::string_view texto) {
        if (deslocamento_atual + texto.size() > capacidade_atual) {
            capacidade_atual = std::max(tamanho_padrao_bloco, texto.size());
            blocos.push_back(std::make_unique<char[]>(capacidade_atual));
            deslocamento_atual = 0;
        }
        
        char* destino = blocos.back().get() + deslocamento_atual;
        std::copy(texto.begin(), texto.end(), destino);
        deslocamento_atual += texto.size();
        
        return {destino, texto.size()};
    }
};

struct Coluna {
  std::string nome;
  TipoColuna tipo = DESCONHECIDA;

  std::vector<double> valores;
  StringPool reservatorio;
  CategoriaMap mapeamento;
  std::vector<std::string_view> categorias;
  std::unique_ptr<EstatisticasNumericas> estatisticas;
};

class Dataset {
private:
  void* mapped = nullptr;
  size_t map_size = 0;
  std::string_view arquivo;
  size_t cabecalho_size = 0;

  std::vector<Coluna> colunas;
  size_t num_linhas = 0;
  size_t num_colunas = 0;
  
  void mapearArquivo(const char *caminho);
  void lerCabecalho();
  void processarLinhas();

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