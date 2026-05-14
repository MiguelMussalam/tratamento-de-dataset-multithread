#include "dataset.hpp"
#include <charconv>
#include <iostream>
#include <string>
#include <vector>
#include <math.h>
#include <algorithm>
#include <iomanip>
#include <omp.h>

Dataset::Dataset(const char *caminho){
  mapearArquivo(caminho);
  lerCabecalho();
  inferirTipos();
  
  contarLinhasParalelo();
  alocarVetores();
  processarLinhasParalelo();
  
  #pragma omp parallel for schedule(dynamic)
  for(size_t i = 0; i < num_colunas; i++){
    if(colunas[i].tipo == CATEGORICA){
      categorizarColuna(i);
    } else if(colunas[i].tipo == NUMERICA){
      rotina_coluna_numerica(i);
    }
  }

  if (mapped) {
#ifdef _WIN32
      UnmapViewOfFile(mapped);
#else
      if (mapped != MAP_FAILED) {
          munmap(mapped, map_size);
      }
#endif
      mapped = nullptr;
  }
}

void Dataset::mapearArquivo(const char* caminho) {
#ifdef _WIN32
    HANDLE hFile = CreateFileA(caminho, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "CreateFileA failed\n");
        exit(1);
    }
    
    LARGE_INTEGER size;
    if (!GetFileSizeEx(hFile, &size)) {
        fprintf(stderr, "GetFileSizeEx failed\n");
        CloseHandle(hFile);
        exit(1);
    }
    map_size = size.QuadPart;
    if (map_size == 0) { CloseHandle(hFile); return; }

    HANDLE hMapping = CreateFileMappingA(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
    if (hMapping == NULL) {
        fprintf(stderr, "CreateFileMappingA failed\n");
        CloseHandle(hFile);
        exit(1);
    }

    mapped = MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, 0);
    if (mapped == NULL) {
        fprintf(stderr, "MapViewOfFile failed\n");
        CloseHandle(hMapping);
        CloseHandle(hFile);
        exit(1);
    }
    
    CloseHandle(hMapping);
    CloseHandle(hFile);
#else
    int fd = open(caminho, O_RDONLY);
    if (fd == -1) { perror("open"); exit(1); }

    struct stat sb;
    if (fstat(fd, &sb) == -1) { perror("fstat"); close(fd); exit(1); }

    map_size = sb.st_size;
    if (map_size == 0) { close(fd); return; }

    mapped = mmap(nullptr, map_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapped == MAP_FAILED) { perror("mmap"); close(fd); exit(1); }

    close(fd);
#endif
    arquivo = {static_cast<const char*>(mapped), map_size};
}

void Dataset::lerCabecalho() {
    size_t primeiro_newline = arquivo.find('\n');
    if (primeiro_newline == std::string_view::npos)
        primeiro_newline = arquivo.size();

    std::string_view cabecalho = arquivo.substr(0, primeiro_newline);
    if (!cabecalho.empty() && cabecalho.back() == '\r')
        cabecalho.remove_suffix(1);

    size_t count = 1;
    for (char c : cabecalho) if (c == ',') count++;
    colunas.reserve(count);

    size_t cursor = 0;
    while (cursor <= cabecalho.size()) {
        size_t virgula = cabecalho.find(',', cursor);
        std::string_view nome;
        if (virgula == std::string_view::npos) {
            nome = cabecalho.substr(cursor);
            cursor = cabecalho.size() + 1;
        } else {
            nome = cabecalho.substr(cursor, virgula - cursor);
            cursor = virgula + 1;
        }
        colunas.emplace_back();
        colunas.back().nome = std::string(nome);
        colunas.back().tipo = DESCONHECIDA;
        num_colunas++;
    }

    // guarda onde os dados começam
    cabecalho_size = primeiro_newline + 1;
}

void Dataset::inferirTipos() {
    size_t cursor = cabecalho_size;
    int linhas_lidas = 0;

    while (cursor < arquivo.size() && linhas_lidas < 10) {
        size_t fim = arquivo.find('\n', cursor);
        std::string_view linha = arquivo.substr(cursor,
            fim == std::string_view::npos ? arquivo.size() - cursor : fim - cursor);
        if (!linha.empty() && linha.back() == '\r') linha.remove_suffix(1);

        int j = 0;
        size_t ini = 0;
        while (ini < linha.size()) {
            size_t virgula = linha.find(',', ini);
            std::string_view cel = (virgula == std::string_view::npos)
                ? linha.substr(ini)
                : linha.substr(ini, virgula - ini);

            if (colunas[j].tipo != CATEGORICA) {
                float v;
                auto [ptr, ec] = std::from_chars(cel.data(), cel.data() + cel.size(), v);
                if (ec != std::errc() || ptr != cel.data() + cel.size())
                    colunas[j].tipo = CATEGORICA;
                else
                    colunas[j].tipo = NUMERICA;
            }

            ini = (virgula == std::string_view::npos) ? linha.size() : virgula + 1;
            j++;
        }

        cursor = (fim == std::string_view::npos) ? arquivo.size() : fim + 1;
        linhas_lidas++;
    }
}

void Dataset::contarLinhasParalelo() {
    size_t data_start = cabecalho_size;
    size_t data_size = arquivo.size() - data_start;
    
    int num_threads = omp_get_max_threads();
    size_t chunk_size = data_size / num_threads;
    
    blocos_bytes.resize(num_threads);
    blocos_linhas_iniciais.resize(num_threads, 0);
    std::vector<size_t> linhas_por_thread(num_threads, 0);

    #pragma omp parallel
    {
        int tid = omp_get_thread_num();
        size_t inicio = data_start + tid * chunk_size;
        size_t fim = (tid == num_threads - 1) ? arquivo.size() : data_start + (tid + 1) * chunk_size;
        
        if (tid > 0) {
            while (inicio < arquivo.size() && arquivo[inicio - 1] != '\n') {
                inicio++;
            }
        }
        
#ifndef _WIN32
        // Dica de cache local (madvise) para a página exata deste bloco
        size_t page_size = sysconf(_SC_PAGESIZE);
        size_t aligned_start = (inicio / page_size) * page_size;
        void* addr = static_cast<char*>(mapped) + aligned_start;
        size_t length = fim - aligned_start;
        madvise(addr, length, MADV_WILLNEED);
#endif
        
        blocos_bytes[tid] = {inicio, fim};
        
        size_t count = 0;
        for (size_t i = inicio; i < fim; i++) {
            if (arquivo[i] == '\n') count++;
        }
        if (tid == num_threads - 1 && fim > 0 && arquivo[fim - 1] != '\n' && fim > inicio) {
            count++; 
        }
        
        linhas_por_thread[tid] = count;
    }
    
    num_linhas = 0;
    for (int i = 0; i < num_threads; i++) {
        blocos_linhas_iniciais[i] = num_linhas;
        num_linhas += linhas_por_thread[i];
    }
}

void Dataset::alocarVetores() {
    for (auto& col : colunas) {
        if (col.tipo == NUMERICA) {
            col.valores.resize(num_linhas, 0.0f);
        } else {
            col.valores.resize(num_linhas, 0.0f); 
            col.raw_strings.resize(num_linhas, std::string_view()); 
        }
    }
}

void Dataset::processarLinhasParalelo() {
    int num_threads = blocos_bytes.size();
    
    #pragma omp parallel
    {
        int tid = omp_get_thread_num();
        if (tid < num_threads) {
            processarBloco(blocos_bytes[tid].first, blocos_bytes[tid].second, blocos_linhas_iniciais[tid]);
        }
    }
}

void Dataset::processarBloco(size_t inicio_byte, size_t fim_byte, size_t linha_inicial) {
    size_t cursor = inicio_byte;
    size_t indice_linha = linha_inicial;
    
    while (cursor < fim_byte) {
        size_t fim_linha = arquivo.find('\n', cursor);
        std::string_view linha = arquivo.substr(cursor, 
            fim_linha == std::string_view::npos ? arquivo.size() - cursor : fim_linha - cursor);
            
        if (!linha.empty() && linha.back() == '\r') linha.remove_suffix(1);
        
        size_t ini = 0;
        int j = 0;
        float v;
        
        while (ini < linha.size() && j < num_colunas) {
            size_t virgula = linha.find(',', ini);
            std::string_view cel = (virgula == std::string_view::npos)
                ? linha.substr(ini)
                : linha.substr(ini, virgula - ini);
                
            if (colunas[j].tipo == NUMERICA) {
                auto [ptr, ec] = std::from_chars(cel.data(), cel.data() + cel.size(), v);
                if (ec == std::errc() && ptr == cel.data() + cel.size()) {
                    colunas[j].valores[indice_linha] = v;
                }
            } else {
                colunas[j].raw_strings[indice_linha] = cel;
            }
            
            ini = (virgula == std::string_view::npos) ? linha.size() : virgula + 1;
            j++;
        }
        
        cursor = (fim_linha == std::string_view::npos) ? arquivo.size() : fim_linha + 1;
        indice_linha++;
    }

#ifndef _WIN32
    // Libera as páginas deste bloco após parsing completo
    size_t page_size = sysconf(_SC_PAGESIZE);
    size_t aligned   = (inicio_byte / page_size) * page_size;
    madvise(
        static_cast<char*>(mapped) + aligned,
        fim_byte - aligned,
        MADV_DONTNEED
    );
#endif
}

void Dataset::categorizarColuna(size_t indice_coluna) {
    auto& col = colunas[indice_coluna];
    
    col.mapeamento.reserve(1024);
    col.categorias.reserve(1024);

    for (size_t i = 0; i < num_linhas; i++) {
        std::string_view conteudo = col.raw_strings[i];
        
        auto it = col.mapeamento.find(conteudo);
        if (it == col.mapeamento.end()) {
            int novo_id = col.categorias.size();
            std::string_view pooled_str = col.reservatorio.adicionar(conteudo);
            col.mapeamento.emplace(pooled_str, novo_id);
            col.categorias.emplace_back(pooled_str);
            col.valores[i] = novo_id;
        } else {
            col.valores[i] = it->second;
        }
    }
    
    // Free the raw_strings memory immediately
    std::vector<std::string_view>().swap(col.raw_strings);
}

void Dataset::rotina_coluna_numerica(size_t indice_coluna) {
  const std::vector<float>& valores_originais = colunas[indice_coluna].valores;
  colunas[indice_coluna].estatisticas = std::make_unique<EstatisticasNumericas>();
  EstatisticasNumericas& estatisticas = *colunas[indice_coluna].estatisticas;

  estatisticas.media        = media(valores_originais);
  estatisticas.variancia    = variancia(valores_originais, estatisticas.media);
  estatisticas.desvio_padrao= desvio_padrao(estatisticas.variancia);
  
  std::vector<float> valores_para_ordenar(valores_originais); 
  
  estatisticas.mediana      = mediana(valores_para_ordenar);
  estatisticas.iqr          = iqr(valores_para_ordenar);
}


float Dataset::media(const std::vector<float>& valores_coluna) {
  float media = 0;

  for(size_t i = 0; i < (num_linhas); i++){
    media += valores_coluna[i];
  }

  media = media / (num_linhas);
  return media;
}

float Dataset::variancia(const std::vector<float>& valores_coluna, float media) {
    float soma = 0;

    for (const float& valor : valores_coluna) {
        float diferenca = valor - media;
        soma += diferenca * diferenca;
    }

    return soma / valores_coluna.size();
}

float Dataset::desvio_padrao(float variancia) {
  return std::sqrt(variancia);
}

float Dataset::mediana(std::vector<float>& valores_coluna) {
    size_t n = valores_coluna.size();
    size_t meio = n / 2;

    std::nth_element(valores_coluna.begin(), valores_coluna.begin() + meio, valores_coluna.end());

    if (n % 2 == 1) {
        return valores_coluna[meio];
    } else {
        float maior_inferior = *std::max_element(valores_coluna.begin(), valores_coluna.begin() + meio);
        return (maior_inferior + valores_coluna[meio]) / 2.0f;
    }
}

float Dataset::iqr(std::vector<float>& valores_coluna) {
    size_t n = valores_coluna.size();

    // Q1 — mediana da metade inferior
    size_t pos_q1 = n / 4;
    std::nth_element(valores_coluna.begin(),
                     valores_coluna.begin() + pos_q1,
                     valores_coluna.end());
    float q1 = valores_coluna[pos_q1];

    // Q3 — mediana da metade superior
    size_t pos_q3 = (3 * n) / 4;
    std::nth_element(valores_coluna.begin(),
                     valores_coluna.begin() + pos_q3,
                     valores_coluna.end());
    float q3 = valores_coluna[pos_q3];

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