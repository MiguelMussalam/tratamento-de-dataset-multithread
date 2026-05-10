#include "dataset.hpp"
#include <charconv>
#include <iostream>
#include <string>
#include <vector>
#include <math.h>
#include <algorithm>
#include <iomanip>

Dataset::Dataset(const char *caminho){
  mapearArquivo(caminho);
  lerCabecalho();
  processarLinhas();
  
  for(size_t i = 0; i < num_colunas; i++){
    if(colunas[i].tipo == NUMERICA){
      rotina_coluna_numerica(i);
    }
  }

  if (mapped && mapped != MAP_FAILED) {
      munmap(mapped, map_size);
  }
}

void Dataset::mapearArquivo(const char *caminho) {
  std::cout << "Iniciando leitura..." << std::endl;

  int fd = open(caminho, O_RDONLY);
  if (fd == -1) {
    perror("open");
    exit(1);
  }

  struct stat sb;
  if (fstat(fd, &sb) == -1) {
    perror("fstat");
    close(fd);
    exit(1);
  }

  map_size = sb.st_size;
  if (map_size == 0) {
    std::cerr << "Arquivo vazio" << std::endl;
    close(fd);
    return;
  }

  mapped = mmap(nullptr, map_size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (mapped == MAP_FAILED) {
    perror("mmap");
    close(fd);
    exit(1);
  }

  close(fd);

  madvise(mapped, map_size, MADV_SEQUENTIAL);

  arquivo = {static_cast<const char *>(mapped), map_size};
}

void Dataset::lerCabecalho() {
  size_t primeiro_newline = arquivo.find('\n');
  if (primeiro_newline == std::string_view::npos) {
    primeiro_newline = arquivo.size();
  }

  std::string_view cabecalho = arquivo.substr(0, primeiro_newline);
  if (!cabecalho.empty() && cabecalho.back() == '\r')
    cabecalho.remove_suffix(1);

  size_t count = 1;
  for (char c : cabecalho)
    if (c == ',')
      count++;

  colunas.reserve(count);
  
  std::cout << "Colunas: " << count << std::endl;

  size_t cursor_init_cab = 0;
  size_t tam_substr = 0;
  std::string_view conteudo;

  while (cursor_init_cab < cabecalho.size()) {
    tam_substr = cabecalho.find(',', cursor_init_cab);

    if (tam_substr == std::string_view::npos) {
      conteudo = cabecalho.substr(cursor_init_cab);
      cursor_init_cab = cabecalho.size();
    } else {
      conteudo = cabecalho.substr(cursor_init_cab, (tam_substr - cursor_init_cab));
      cursor_init_cab = tam_substr + 1;
    }

    colunas.emplace_back();
    colunas.back().nome = std::string(conteudo);
    colunas.back().tipo = NUMERICA;
    num_colunas++;
  }

  size_t linhas_estimadas = arquivo.size() / 90;

  for (auto& col : colunas) {
      col.valores.reserve(linhas_estimadas);
  }

  cabecalho_size = primeiro_newline + 1;
}

void Dataset::processarLinhas() {
  if (cabecalho_size >= arquivo.size()) return;

  size_t cursor_init_arquivo = cabecalho_size;
  size_t cursor_fim_arquivo = 0;
  size_t cursor_init_linha = 0;
  size_t tam_substr = 0;
  int coluna_atual = 0;
  double v;
  std::string_view linha;
  std::string_view conteudo;

  while (cursor_init_arquivo < arquivo.size()) {
    cursor_fim_arquivo = arquivo.find('\n', cursor_init_arquivo);

    if (cursor_fim_arquivo == std::string_view::npos) {
      linha = arquivo.substr(cursor_init_arquivo);
    } else {
      linha = arquivo.substr(cursor_init_arquivo, cursor_fim_arquivo - cursor_init_arquivo);
    }
    while (!linha.empty() && (linha.back() == '\r' || linha.back() == '\n'))
      linha.remove_suffix(1);

    while (cursor_init_linha < linha.size()) {
      tam_substr = linha.find(',', cursor_init_linha);

      if (tam_substr == std::string_view::npos) {
        conteudo = linha.substr(cursor_init_linha);
        cursor_init_linha = linha.size();
      } else {
        conteudo = linha.substr(cursor_init_linha, (tam_substr - cursor_init_linha));
        cursor_init_linha = tam_substr + 1;
      }

      auto [ptr, ec] = std::from_chars(conteudo.data(), conteudo.data() + conteudo.size(), v);

      if (ec == std::errc() && ptr == conteudo.data() + conteudo.size()) {
          if (colunas[coluna_atual].tipo == CATEGORICA) {
              categorizar(conteudo, coluna_atual);
          } else {
              colunas[coluna_atual].valores.push_back(v);
          }
      } else {
        if (colunas[coluna_atual].tipo != CATEGORICA) {
          colunas[coluna_atual].tipo = CATEGORICA;
          ReprocessarCategorizacao(coluna_atual);
        }
        categorizar(conteudo, coluna_atual);
      }
      coluna_atual++;
    }
    cursor_init_linha = 0;
    coluna_atual = 0;
    num_linhas++;

    if (cursor_fim_arquivo == std::string_view::npos) {
      cursor_init_arquivo = arquivo.size();
    } else {
      cursor_init_arquivo = cursor_fim_arquivo + 1;
    }
  }
}

void Dataset::categorizar(std::string_view conteudo, size_t indice_coluna) {
  auto it = colunas[indice_coluna].mapeamento.find(conteudo);

  if (it == colunas[indice_coluna].mapeamento.end()) {
    int indice = colunas[indice_coluna].categorias.size();
    colunas[indice_coluna].mapeamento.emplace(std::string(conteudo), indice);
    colunas[indice_coluna].categorias.emplace_back(conteudo);
    colunas[indice_coluna].valores.push_back(indice);
  } else {
    colunas[indice_coluna].valores.push_back(it->second);
  }
}

void Dataset::ReprocessarCategorizacao(size_t indice_coluna) {
  std::vector<double> valores_anteriores = std::move(colunas[indice_coluna].valores);

  for (double val : valores_anteriores) {
    char buffer[64];
    auto [ptr, ec] = std::to_chars(buffer, buffer + sizeof(buffer), val);
    std::string_view chave_temp(buffer, ptr - buffer);

    auto it = colunas[indice_coluna].mapeamento.find(chave_temp);
    if (it == colunas[indice_coluna].mapeamento.end()) {
      int indice = colunas[indice_coluna].categorias.size();
      colunas[indice_coluna].mapeamento.emplace(std::string(chave_temp), indice);
      colunas[indice_coluna].categorias.emplace_back(chave_temp);
      colunas[indice_coluna].valores.push_back(indice);
    } else {
      colunas[indice_coluna].valores.push_back(it->second);
    }
  }
}

void Dataset::rotina_coluna_numerica(size_t indice_coluna) {
  const std::vector<double>& valores_originais = colunas[indice_coluna].valores;
  colunas[indice_coluna].estatisticas = std::make_unique<EstatisticasNumericas>();
  EstatisticasNumericas& estatisticas = *colunas[indice_coluna].estatisticas;

  estatisticas.media        = media(valores_originais);
  estatisticas.variancia    = variancia(valores_originais, estatisticas.media);
  estatisticas.desvio_padrao= desvio_padrao(estatisticas.variancia);
  
  std::vector<double> valores_para_ordenar(valores_originais); 
  
  estatisticas.mediana      = mediana(valores_para_ordenar);
  estatisticas.iqr          = iqr(valores_para_ordenar);
}


double Dataset::media(const std::vector<double>& valores_coluna) {
  double media = 0;

  for(size_t i = 0; i < (num_linhas); i++){
    media += valores_coluna[i];
  }

  media = media / (num_linhas);
  return media;
}

double Dataset::variancia(const std::vector<double>& valores_coluna, double media) {
    double soma = 0;

    for (const double& valor : valores_coluna) {
        double diferenca = valor - media;
        soma += diferenca * diferenca;
    }

    return soma / valores_coluna.size();
}

double Dataset::desvio_padrao(double variancia) {
  return std::sqrt(variancia);
}

double Dataset::mediana(std::vector<double>& valores_coluna) {
    size_t n = valores_coluna.size();
    size_t meio = n / 2;

    std::nth_element(valores_coluna.begin(), valores_coluna.begin() + meio, valores_coluna.end());

    if (n % 2 == 1) {
        return valores_coluna[meio];
    } else {
        double maior_inferior = *std::max_element(valores_coluna.begin(), valores_coluna.begin() + meio);
        return (maior_inferior + valores_coluna[meio]) / 2.0;
    }
}

double Dataset::iqr(std::vector<double>& valores_coluna) {
    size_t n = valores_coluna.size();

    // Q1 — mediana da metade inferior
    size_t pos_q1 = n / 4;
    std::nth_element(valores_coluna.begin(),
                     valores_coluna.begin() + pos_q1,
                     valores_coluna.end());
    double q1 = valores_coluna[pos_q1];

    // Q3 — mediana da metade superior
    size_t pos_q3 = (3 * n) / 4;
    std::nth_element(valores_coluna.begin(),
                     valores_coluna.begin() + pos_q3,
                     valores_coluna.end());
    double q3 = valores_coluna[pos_q3];

    return q3 - q1;
}

void Dataset::print() {
    std::cout << std::left
              << std::setw(30) << "Coluna"
              << std::setw(14) << "Tipo"
              << std::setw(14) << "Media"
              << std::setw(14) << "Mediana"
              << std::setw(14) << "DesvPad"
              << std::setw(14) << "IQR"
              << std::setw(14) << "Qtd Categorias" << "\n";
    std::cout << std::string(114, '-') << "\n";

    for (size_t j = 0; j < num_colunas; j++) {
        std::cout << std::left << std::setw(30) << colunas[j].nome;

        if (colunas[j].tipo == NUMERICA && colunas[j].estatisticas) {
            std::cout << std::setw(14) << "NUMERICA"
                      << std::fixed << std::setprecision(2)
                      << std::setw(14) << colunas[j].estatisticas->media
                      << std::setw(14) << colunas[j].estatisticas->mediana
                      << std::setw(14) << colunas[j].estatisticas->desvio_padrao
                      << std::setw(14) << colunas[j].estatisticas->iqr
                      << std::setw(14) << "-";
        } else {
            std::cout << std::setw(14) << "CATEGORICA"
                      << std::setw(14) << "-"
                      << std::setw(14) << "-"
                      << std::setw(14) << "-"
                      << std::setw(14) << "-"
                      << std::setw(14) << colunas[j].categorias.size();
        }
        std::cout << "\n";
    }

    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "MAPEAMENTOS DAS COLUNAS CATEGÓRICAS\n";
    std::cout << std::string(60, '=') << "\n\n";

    for (size_t j = 0; j < num_colunas; j++) {
        if (colunas[j].tipo == CATEGORICA && !colunas[j].categorias.empty()) {
            std::cout << ">>> Coluna: " << colunas[j].nome << "\n";
            std::cout << std::left << std::setw(10) << "ID" << "Categoria\n";
            std::cout << std::string(40, '-') << "\n";
            
            // Limite de 20 categorias na tela para não travar o terminal
            size_t max_print = std::min<size_t>(20, colunas[j].categorias.size());
            for (size_t i = 0; i < max_print; i++) {
                std::cout << std::left << std::setw(10) << i << colunas[j].categorias[i] << "\n";
            }
            
            if (colunas[j].categorias.size() > max_print) {
                std::cout << std::left << std::setw(10) << "..." 
                          << "... (mais " << colunas[j].categorias.size() - max_print << " categorias ocultas)\n";
            }
            std::cout << "\n";
        }
    }
}