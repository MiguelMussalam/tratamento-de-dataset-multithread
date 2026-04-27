#include "dataset.hpp"
#include <charconv>
#include <iostream>
#include <string>
#include <vector>

Dataset::Dataset(const char *caminho){
  lerArquivo(caminho); 
  
  for(size_t i = 0; i < num_colunas; i++){
    if(colunas[i].tipo == NUMERICA){
      rotina_coluna_numerica(i);
    }
  }
}

void Dataset::lerArquivo(const char *caminho) {
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

  size_t size = sb.st_size;
  if (size == 0) {
    std::cerr << "Arquivo vazio" << std::endl;
    close(fd);
    return;
  }

  void *mapped = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (mapped == MAP_FAILED) {
    perror("mmap");
    close(fd);
    exit(1);
  }

  close(fd);
  std::string_view arquivo = {static_cast<const char *>(mapped), size};
  std::string_view linha;
  std::string_view conteudo;
  size_t cursor_init_arquivo = 0;
  size_t cursor_fim_arquivo = 0;
  size_t cursor_init_linha = 0;
  size_t cursor_fim_linha = 0;
  size_t tam_substr = 0;
  int coluna_atual = 0;
  double v;

  // reservando tam do cabecalho
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

  std::cout << "Colunas: " << count << " | Capacity: " << colunas.capacity()
            << std::endl;

  // - criação do cabeçalho -
  size_t cursor_init_cab = 0;
  while (cursor_init_cab < cabecalho.size()) {
    tam_substr = cabecalho.find(',', cursor_init_cab);

    if (tam_substr == std::string_view::npos) {
      conteudo = cabecalho.substr(cursor_init_cab);
      cursor_init_cab = cabecalho.size();
    } else {
      conteudo =
          cabecalho.substr(cursor_init_cab, (tam_substr - cursor_init_cab));
      cursor_init_cab = tam_substr + 1;
    }

    colunas.emplace_back();
    colunas.back().nome = std::string(conteudo);
    colunas.back().tipo = NUMERICA;
    num_colunas++;
  }

  // avança o cursor para pular o cabeçalho na leitura de dados
  if (primeiro_newline == arquivo.size()) {
    munmap(mapped, size);
    return;
  }
  cursor_init_arquivo = primeiro_newline + 1;

  while (cursor_init_arquivo < arquivo.size()) {
    cursor_fim_arquivo = arquivo.find('\n', cursor_init_arquivo);

    if (cursor_fim_arquivo == std::string_view::npos) {
      linha = arquivo.substr(cursor_init_arquivo);
    } else {
      linha = arquivo.substr(cursor_init_arquivo,
                             cursor_fim_arquivo - cursor_init_arquivo);
    }
    while (!linha.empty() && (linha.back() == '\r' || linha.back() == '\n'))
      linha.remove_suffix(1);

    while (cursor_init_linha < linha.size()) {
      tam_substr = linha.find(',', cursor_init_linha);

      // npos - não encontrou proximo ','
      if (tam_substr == std::string_view::npos) {
        conteudo = linha.substr(cursor_init_linha);
        cursor_init_linha = linha.size();
      } else {
        conteudo =
            linha.substr(cursor_init_linha, (tam_substr - cursor_init_linha));
        cursor_init_linha = tam_substr + 1;
      }

      auto [ptr, ec] = std::from_chars(conteudo.data(),
                                       conteudo.data() + conteudo.size(), v);

      // valor encaixa em double ou não
      if (ec == std::errc() && ptr == conteudo.data() + conteudo.size()) {
        colunas[coluna_atual].valores.push_back(v);
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

  munmap(mapped, size);
}

void Dataset::categorizar(std::string_view conteudo, size_t indice_coluna) {
  std::string chave(conteudo);

  auto it = colunas[indice_coluna].mapeamento.find(chave);

  if (it == colunas[indice_coluna].mapeamento.end()) {
    int indice = colunas[indice_coluna].categorias.size();
    colunas[indice_coluna].mapeamento[chave] = indice;
    colunas[indice_coluna].categorias.push_back(chave);
    colunas[indice_coluna].valores.push_back(indice);
  } else {
    colunas[indice_coluna].valores.push_back(it->second);
  }
}

void Dataset::ReprocessarCategorizacao(size_t indice_coluna) {
  std::vector<double> valores_anteriores = colunas[indice_coluna].valores;
  colunas[indice_coluna].valores.clear();

  for (double val : valores_anteriores) {
    std::string chave = std::to_string(val);

    auto it = colunas[indice_coluna].mapeamento.find(chave);
    if (it == colunas[indice_coluna].mapeamento.end()) {
      int indice = colunas[indice_coluna].categorias.size();
      colunas[indice_coluna].mapeamento[chave] = indice;
      colunas[indice_coluna].categorias.push_back(chave);
      colunas[indice_coluna].valores.push_back(indice);
    } else {
      colunas[indice_coluna].valores.push_back(it->second);
    }
  }
}

void Dataset::rotina_coluna_numerica(size_t indice_coluna) {
  colunas[indice_coluna].estatisticas = std::make_unique<EstatisticasNumericas>();
  colunas[indice_coluna].estatisticas->media = media(indice_coluna);
  colunas[indice_coluna].estatisticas->mediana = mediana(indice_coluna);
  colunas[indice_coluna].estatisticas->variancia = variancia(indice_coluna, colunas[indice_coluna].estatisticas->media);
  colunas[indice_coluna].estatisticas->desvio_padrao = desvio_padrao(indice_coluna, colunas[indice_coluna].estatisticas->variancia);
  colunas[indice_coluna].estatisticas->iqr = iqr(indice_coluna);
}

double Dataset::media(size_t indice_coluna) {
  double media = 0;

  for(size_t i = 0; i < (num_linhas); i++){
    media += colunas[indice_coluna].valores[i];
  }

  media = media / (num_linhas);


  return media;
}

double Dataset::variancia(size_t indice_coluna, double media) {
  double variancia;

  return variancia;
}

double Dataset::desvio_padrao(size_t indice_coluna, double variancia) {
  double desvio_padrao;

  return desvio_padrao;
}

double Dataset::mediana(size_t indice_coluna) {
  double mediana;

  return mediana;
}

double Dataset::iqr(size_t indice_coluna) {
  double iqr;

  return iqr;
}