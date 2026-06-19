#include "dataset.hpp"
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <immintrin.h>
#include <iomanip>
#include <iostream>
#include <math.h>
#include <omp.h>
#include <string>
#include <vector>
#include <chrono>
#include <fstream>

Dataset::Dataset(const char *caminho) {
  // Habilita paralelismo aninhado: o loop das colunas já é paralelo,
  // e as funções internas (media, variancia) também criarão sub-regiões
  // paralelas.
  omp_set_max_active_levels(2);

  auto t_start = std::chrono::high_resolution_clock::now();

  mapearArquivo(caminho);
  lerCabecalho();
  inferirTipos();

  contarLinhasParalelo();
  alocarVetores();
  processarLinhasParalelo();

  auto t_parsing = std::chrono::high_resolution_clock::now();

#pragma omp parallel for schedule(dynamic)
  for (size_t i = 0; i < num_colunas; i++) {
    if (colunas[i].tipo == CATEGORICA) {
      categorizarColuna(i);
    } else if (colunas[i].tipo == NUMERICA) {
      rotina_coluna_numerica(i);
    }
  }

  auto t_end = std::chrono::high_resolution_clock::now();

  double parsing_ms = std::chrono::duration<double, std::milli>(t_parsing - t_start).count();
  double routine_ms = std::chrono::duration<double, std::milli>(t_end - t_parsing).count();

  std::cerr << "[FASE] Leitura e Parsing: " << parsing_ms << " ms\n";
  std::cerr << "[FASE] Rotina Numerica: " << routine_ms << " ms\n";

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

void Dataset::mapearArquivo(const char *caminho) {
#ifdef _WIN32
  HANDLE hFile = CreateFileA(caminho, GENERIC_READ, FILE_SHARE_READ, NULL,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
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
  if (map_size == 0) {
    CloseHandle(hFile);
    return;
  }

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
#endif
  arquivo = {static_cast<const char *>(mapped), map_size};
}

void Dataset::lerCabecalho() {
  size_t primeiro_newline = arquivo.find('\n');
  if (primeiro_newline == std::string_view::npos)
    primeiro_newline = arquivo.size();

  std::string_view cabecalho = arquivo.substr(0, primeiro_newline);
  if (!cabecalho.empty() && cabecalho.back() == '\r')
    cabecalho.remove_suffix(1);

  size_t count = 1;
  for (char c : cabecalho)
    if (c == ',')
      count++;
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
    std::string_view linha = arquivo.substr(
        cursor,
        fim == std::string_view::npos ? arquivo.size() - cursor : fim - cursor);
    if (!linha.empty() && linha.back() == '\r')
      linha.remove_suffix(1);

    int j = 0;
    size_t ini = 0;
    while (ini < linha.size()) {
      size_t virgula = linha.find(',', ini);
      std::string_view cel = (virgula == std::string_view::npos)
                                 ? linha.substr(ini)
                                 : linha.substr(ini, virgula - ini);

      if (colunas[j].tipo != CATEGORICA) {
        float v;
        auto [ptr, ec] =
            std::from_chars(cel.data(), cel.data() + cel.size(), v);
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

// Conta ocorrências de '\n' em data[0..len).
// Usa AVX2 (32 bytes/iteração) quando disponível; cai para escalar caso
// contrário.
static size_t contar_newlines(const char *data, size_t len) {
  size_t count = 0;
#ifdef __AVX2__
  const __m256i nl = _mm256_set1_epi8('\n');
  size_t i = 0;
  for (; i + 32 <= len; i += 32) {
    __m256i chunk =
        _mm256_loadu_si256(reinterpret_cast<const __m256i *>(data + i));
    __m256i cmp = _mm256_cmpeq_epi8(chunk, nl); // 32 comparações simultâneas
    uint32_t mask = static_cast<uint32_t>(_mm256_movemask_epi8(cmp)); // bitmask
    count += static_cast<size_t>(__builtin_popcount(mask)); // conta bits
  }
  for (; i < len; i++)
    count += (data[i] == '\n'); // tail escalar
#else
  for (size_t i = 0; i < len; i++)
    count += (data[i] == '\n');
#endif
  return count;
}

// Retorna o índice do próximo 'delim' em data[start..len), ou npos se não
// encontrar. Usa AVX2 (32 bytes/iteração): __builtin_ctz dá a posição do 1º bit
// 1 no mask.
static size_t encontrar_proximo(const char *data, size_t start, size_t len,
                                char delim) {
#ifdef __AVX2__
  const __m256i vd = _mm256_set1_epi8(delim);
  size_t i = start;
  for (; i + 32 <= len; i += 32) {
    __m256i chunk =
        _mm256_loadu_si256(reinterpret_cast<const __m256i *>(data + i));
    __m256i cmp = _mm256_cmpeq_epi8(chunk, vd);
    uint32_t mask = static_cast<uint32_t>(_mm256_movemask_epi8(cmp));
    if (mask != 0)
      return i +
             static_cast<size_t>(__builtin_ctz(mask)); // posição do 1º match
  }
  for (; i < len; i++)
    if (data[i] == delim)
      return i;
  return std::string_view::npos;
#else
  for (size_t i = start; i < len; i++)
    if (data[i] == delim)
      return i;
  return std::string_view::npos;
#endif
}

void Dataset::contarLinhasParalelo() {
  size_t data_start = cabecalho_size;
  size_t data_size = arquivo.size() - data_start;

  int num_threads = omp_get_max_threads();
  if (num_threads > 1 && data_size / num_threads < 1024) {
    num_threads = 1;
  }
  size_t chunk_size = data_size / num_threads;

  blocos_bytes.resize(num_threads);
  blocos_linhas_iniciais.resize(num_threads, 0);
  std::vector<size_t> linhas_por_thread(num_threads, 0);

#pragma omp parallel num_threads(num_threads)
  {
    int tid = omp_get_thread_num();
    size_t inicio = data_start + tid * chunk_size;
    size_t fim = (tid == num_threads - 1) ? arquivo.size()
                                          : data_start + (tid + 1) * chunk_size;

    if (tid > 0) {
      while (inicio < arquivo.size() && arquivo[inicio - 1] != '\n') {
        inicio++;
      }
    }

    if (tid < num_threads - 1) {
      while (fim < arquivo.size() && arquivo[fim - 1] != '\n') {
        fim++;
      }
    }

#ifndef _WIN32
    // Dica de cache local (madvise) para a página exata deste bloco
    size_t page_size = sysconf(_SC_PAGESIZE);
    size_t aligned_start = (inicio / page_size) * page_size;
    void *addr = static_cast<char *>(mapped) + aligned_start;
    size_t length = fim - aligned_start;
    madvise(addr, length, MADV_WILLNEED);
#endif

    blocos_bytes[tid] = {inicio, fim};

    linhas_por_thread[tid] =
        contar_newlines(arquivo.data() + inicio, fim - inicio);

    // Última linha do arquivo sem '\n' final: conta como linha extra
    if (tid == num_threads - 1 && fim > inicio && arquivo[fim - 1] != '\n') {
      linhas_por_thread[tid]++;
    }
  }

  num_linhas = 0;
  for (int i = 0; i < num_threads; i++) {
    blocos_linhas_iniciais[i] = num_linhas;
    num_linhas += linhas_por_thread[i];
  }
}

void Dataset::alocarVetores() {
  for (auto &col : colunas) {
    if (col.tipo == NUMERICA) {
      col.valores.resize(num_linhas, std::numeric_limits<float>::quiet_NaN());
    } else {
      col.valores.resize(num_linhas, 0.0f);
      col.raw_strings.resize(num_linhas, std::string_view());
    }
  }
}

size_t Dataset::detectarTamanhoL3() {
#ifdef _WIN32
  DWORD tamanho_buffer = 0;
  GetLogicalProcessorInformation(nullptr, &tamanho_buffer);
  if (tamanho_buffer == 0) {
    return 8 * 1024 * 1024; // fallback conservador: 8 MB
  }

  std::vector<char> buffer(tamanho_buffer);
  auto* info = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION*>(buffer.data());

  if (!GetLogicalProcessorInformation(info, &tamanho_buffer)) {
    return 8 * 1024 * 1024; // fallback conservador: 8 MB
  }

  size_t count = tamanho_buffer / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
  for (size_t i = 0; i < count; i++) {
    if (info[i].Relationship == RelationCache &&
        info[i].Cache.Level == 3) {
      return info[i].Cache.Size;
    }
  }
  return 8 * 1024 * 1024; // fallback conservador: 8 MB
#else
  for (int idx = 2; idx <= 3; idx++) {
    std::string caminho_level = "/sys/devices/system/cpu/cpu0/cache/index"
                                 + std::to_string(idx) + "/level";
    std::string caminho_size  = "/sys/devices/system/cpu/cpu0/cache/index"
                                 + std::to_string(idx) + "/size";

    std::ifstream level_file(caminho_level);
    std::ifstream size_file(caminho_size);
    if (!level_file || !size_file) continue;

    int level;
    level_file >> level;
    if (level != 3) continue;

    std::string size_str;
    size_file >> size_str; // formato: "13312K" ou "13M"

    if (size_str.empty()) continue;
    size_t valor = std::stoul(size_str);
    if (size_str.back() == 'K') return valor * 1024;
    if (size_str.back() == 'M') return valor * 1024 * 1024;
    return valor;
  }
  return 8 * 1024 * 1024; // fallback conservador: 8 MB
#endif
}

size_t Dataset::calcularTamanhoChunk(size_t l3_bytes, int num_threads_ativas) {
  constexpr double FATOR_SEGURANCA = 0.75;
  int threads = num_threads_ativas > 0 ? num_threads_ativas : 1;
  size_t l3_utilizavel = static_cast<size_t>(l3_bytes * FATOR_SEGURANCA);
  size_t calculated = l3_utilizavel / threads;
  return std::max<size_t>(calculated, 256 * 1024); // Lower bound: 256 KB
}

std::vector<ChunkInfo> Dataset::calcularChunks(size_t chunk_size) {
  std::vector<ChunkInfo> chunks;
  int num_threads = blocos_bytes.size();

  for (int tid = 0; tid < num_threads; tid++) {
    size_t bloco_inicio = blocos_bytes[tid].first;
    size_t bloco_fim = blocos_bytes[tid].second;
    size_t linha_atual = blocos_linhas_iniciais[tid];

    size_t inicio = bloco_inicio;
    while (inicio < bloco_fim) {
      size_t proposto_fim = inicio + chunk_size;
      size_t fim = proposto_fim;

      if (fim >= bloco_fim) {
        fim = bloco_fim;
      } else {
        // Align to the next newline
        while (fim < bloco_fim && arquivo[fim - 1] != '\n') {
          fim++;
        }
      }

      size_t num_linhas_chunk = contar_newlines(arquivo.data() + inicio, fim - inicio);
      
      // Last line of the file without final '\n' count handling
      if (fim == arquivo.size() && fim > inicio && arquivo[fim - 1] != '\n') {
        num_linhas_chunk++;
      }

      chunks.push_back({inicio, fim, linha_atual});

      linha_atual += num_linhas_chunk;
      inicio = fim;
    }
  }

  return chunks;
}

void Dataset::processarLinhasParalelo() {
  size_t l3_total = detectarTamanhoL3();
  int num_threads = omp_get_max_threads();
  size_t chunk_size = calcularTamanhoChunk(l3_total, num_threads);
  
  std::vector<ChunkInfo> chunks = calcularChunks(chunk_size);

#pragma omp parallel for schedule(dynamic, 1)
  for (size_t i = 0; i < chunks.size(); i++) {
    processarBloco(chunks[i].inicio_byte, chunks[i].fim_byte, chunks[i].linha_inicial);
  }
}

void Dataset::processarBloco(size_t inicio_byte, size_t fim_byte,
                             size_t linha_inicial) {
  size_t cursor = inicio_byte;
  size_t indice_linha = linha_inicial;

  while (cursor < fim_byte) {
    size_t fim_linha =
        encontrar_proximo(arquivo.data(), cursor, arquivo.size(), '\n');
    std::string_view linha = arquivo.substr(
        cursor, fim_linha == std::string_view::npos ? arquivo.size() - cursor
                                                    : fim_linha - cursor);

    if (!linha.empty() && linha.back() == '\r')
      linha.remove_suffix(1);

    size_t ini = 0;
    int j = 0;
    float v;

    while (ini < linha.size() && j < num_colunas) {
      size_t virgula = encontrar_proximo(linha.data(), ini, linha.size(), ',');
      std::string_view cel = (virgula == std::string_view::npos)
                                 ? linha.substr(ini)
                                 : linha.substr(ini, virgula - ini);

      if (colunas[j].tipo == NUMERICA) {
        auto [ptr, ec] =
            std::from_chars(cel.data(), cel.data() + cel.size(), v);
        if (ec == std::errc() && ptr == cel.data() + cel.size()) {
          colunas[j].valores[indice_linha] = v;
        }
      } else {
        colunas[j].raw_strings[indice_linha] = cel;
      }

      ini = (virgula == std::string_view::npos) ? linha.size() : virgula + 1;
      j++;
    }

    cursor =
        (fim_linha == std::string_view::npos) ? arquivo.size() : fim_linha + 1;
    indice_linha++;
  }

#ifndef _WIN32
  // Libera as páginas deste bloco após parsing completo
  size_t page_size = sysconf(_SC_PAGESIZE);
  size_t aligned = (inicio_byte / page_size) * page_size;
  madvise(static_cast<char *>(mapped) + aligned, fim_byte - aligned,
          MADV_DONTNEED);
#endif
}

void Dataset::categorizarColuna(size_t indice_coluna) {
  auto &col = colunas[indice_coluna];

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
  const std::vector<float> &valores_originais = colunas[indice_coluna].valores;

  int tem_nan = 0;
#pragma omp parallel for reduction(+ : tem_nan) schedule(static)
  for (size_t i = 0; i < valores_originais.size(); i++) {
    if (std::isnan(valores_originais[i])) {
      tem_nan = 1;
    }
  }

  if (tem_nan > 0) {
    colunas[indice_coluna].erro_categorico = true;
    return;
  }

  colunas[indice_coluna].estatisticas =
      std::make_unique<EstatisticasNumericas>();
  EstatisticasNumericas &estatisticas = *colunas[indice_coluna].estatisticas;

  estatisticas.media = media(valores_originais);
  estatisticas.variancia = variancia(valores_originais, estatisticas.media);
  estatisticas.desvio_padrao = desvio_padrao(estatisticas.variancia);

  // Uma única cópia para manter o footprint de memória controlado.
  // nth_element é in-place; mediana e iqr reutilizam o mesmo vetor.
  std::vector<float> valores_para_ordenar(valores_originais);

  estatisticas.mediana = mediana(valores_para_ordenar);
  estatisticas.iqr = iqr(valores_para_ordenar);
}

float Dataset::media(const std::vector<float> &valores_coluna) {
  float soma = 0;
  size_t n = valores_coluna.size();

// Cada thread acumula sua soma parcial; OpenMP combina ao final sem race
// condition.
#pragma omp parallel for reduction(+ : soma) schedule(static)
  for (size_t i = 0; i < n; i++) {
    soma += valores_coluna[i];
  }

  return soma / static_cast<float>(n);
}

float Dataset::variancia(const std::vector<float> &valores_coluna,
                         float media) {
  float soma = 0;
  size_t n = valores_coluna.size();

// Redução paralela: cada thread computa desvios ao quadrado parciais.
#pragma omp parallel for reduction(+ : soma) schedule(static)
  for (size_t i = 0; i < n; i++) {
    float diferenca = valores_coluna[i] - media;
    soma += diferenca * diferenca;
  }

  return soma / static_cast<float>(n);
}

float Dataset::desvio_padrao(float variancia) { return std::sqrt(variancia); }

float Dataset::mediana(std::vector<float> &valores_coluna) {
  size_t n = valores_coluna.size();
  size_t meio = n / 2;

  std::nth_element(valores_coluna.begin(), valores_coluna.begin() + meio,
                   valores_coluna.end());

  if (n % 2 == 1) {
    return valores_coluna[meio];
  } else {
    float maior_inferior = *std::max_element(valores_coluna.begin(),
                                             valores_coluna.begin() + meio);
    return (maior_inferior + valores_coluna[meio]) / 2.0f;
  }
}

float Dataset::iqr(std::vector<float> &valores_coluna) {
  size_t n = valores_coluna.size();

  // Q1 — mediana da metade inferior
  size_t pos_q1 = n / 4;
  std::nth_element(valores_coluna.begin(), valores_coluna.begin() + pos_q1,
                   valores_coluna.end());
  float q1 = valores_coluna[pos_q1];

  // Q3 — mediana da metade superior
  size_t pos_q3 = (3 * n) / 4;
  std::nth_element(valores_coluna.begin(), valores_coluna.begin() + pos_q3,
                   valores_coluna.end());
  float q3 = valores_coluna[pos_q3];

  return q3 - q1;
}

void Dataset::print() {
  std::cout << std::left << std::setw(30) << "Coluna" << std::setw(14) << "Tipo"
            << std::setw(14) << "Media" << std::setw(14) << "Mediana"
            << std::setw(14) << "DesvPad" << std::setw(14) << "IQR"
            << std::setw(14) << "Qtd Categorias" << "\n";
  std::cout << std::string(114, '-') << "\n";

  for (size_t j = 0; j < num_colunas; j++) {
    std::cout << std::left << std::setw(30) << colunas[j].nome;

    if (colunas[j].erro_categorico) {
      std::cout << std::setw(14) << "NUMERICA"
                << "Nao foi possivel fazer estatistica descritiva: Encontrado "
                   "valor categorico no meio da estatistica";
    } else if (colunas[j].tipo == NUMERICA && colunas[j].estatisticas) {
      std::cout << std::setw(14) << "NUMERICA" << std::fixed
                << std::setprecision(2) << std::setw(14)
                << colunas[j].estatisticas->media << std::setw(14)
                << colunas[j].estatisticas->mediana << std::setw(14)
                << colunas[j].estatisticas->desvio_padrao << std::setw(14)
                << colunas[j].estatisticas->iqr << std::setw(14) << "-";
    } else {
      std::cout << std::setw(14) << "CATEGORICA" << std::setw(14) << "-"
                << std::setw(14) << "-" << std::setw(14) << "-" << std::setw(14)
                << "-" << std::setw(14) << colunas[j].categorias.size();
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
        std::cout << std::left << std::setw(10) << i << colunas[j].categorias[i]
                  << "\n";
      }

      if (colunas[j].categorias.size() > max_print) {
        std::cout << std::left << std::setw(10) << "..."
                  << "... (mais " << colunas[j].categorias.size() - max_print
                  << " categorias ocultas)\n";
      }
      std::cout << "\n";
    }
  }
}