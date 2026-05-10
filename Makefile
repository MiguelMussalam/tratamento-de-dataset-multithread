CXX = g++
CXXFLAGS = -O3 -g -fno-omit-frame-pointer -std=c++20 -pthread
BUILD_DIR = build
PERF_DIR = resultados_perf

# Arquivos fonte
SRC_SEQ = src/sequencial/main.cpp src/sequencial/dataset.cpp
INC_SEQ = -Iinclude/sequencial

SRC_MT = src/multithread/main.cpp src/multithread/dataset.cpp
INC_MT = -Iinclude/multithread

# Alvos principais
all: seq mt

# ==========================================
# COMPILAÇÃO
# ==========================================
seq: $(BUILD_DIR)/main_seq

mt: $(BUILD_DIR)/main_mt

$(BUILD_DIR)/main_seq: $(SRC_SEQ)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(SRC_SEQ) $(INC_SEQ) -o $@

$(BUILD_DIR)/main_mt: $(SRC_MT)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(SRC_MT) $(INC_MT) -o $@

# ==========================================
# EXECUÇÃO SIMPLES (Sem Perf)
# ==========================================
# Uso: make run_seq DATASET=data/dataset_raw.csv
DATASET ?= data/dataset_raw.csv

run_seq: seq
	@echo "=== Executando versão Sequencial ==="
	./$(BUILD_DIR)/main_seq $(DATASET)

run_mt: mt
	@echo "=== Executando versão Multithread ==="
	./$(BUILD_DIR)/main_mt $(DATASET)

# ==========================================
# PROFILING (Com Perf)
# ==========================================
PERF_EVENTS = task-clock,page-faults,branches,branch-misses,instructions,cycles,L1-dcache-load-misses,LLC-loads,LLC-load-misses

profile_seq: seq
	@mkdir -p $(PERF_DIR)/seq
	@echo "=== Iniciando Profiling Sequencial ==="
	@rm -f $(PERF_DIR)/seq/perf_stat.txt $(PERF_DIR)/seq/perf.data
	perf stat -e $(PERF_EVENTS) -o $(PERF_DIR)/seq/perf_stat.txt ./$(BUILD_DIR)/main_seq $(DATASET)
	perf record -g -s -e $(PERF_EVENTS) -o $(PERF_DIR)/seq/perf.data ./$(BUILD_DIR)/main_seq $(DATASET)
	perf report --stdio -g -T -i $(PERF_DIR)/seq/perf.data > $(PERF_DIR)/seq/perf_functions.txt 2>&1
	@echo "Arquivos gerados em $(PERF_DIR)/seq/"

profile_mt: mt
	@mkdir -p $(PERF_DIR)/mt
	@echo "=== Iniciando Profiling Multithread ==="
	@rm -f $(PERF_DIR)/mt/perf_stat.txt $(PERF_DIR)/mt/perf.data
	perf stat -e $(PERF_EVENTS) -o $(PERF_DIR)/mt/perf_stat.txt ./$(BUILD_DIR)/main_mt $(DATASET)
	perf record -g -s -e $(PERF_EVENTS) -o $(PERF_DIR)/mt/perf.data ./$(BUILD_DIR)/main_mt $(DATASET)
	perf report --stdio -g -T -i $(PERF_DIR)/mt/perf.data > $(PERF_DIR)/mt/perf_functions.txt 2>&1
	@echo "Arquivos gerados em $(PERF_DIR)/mt/"

# ==========================================
# LIMPEZA
# ==========================================
clean:
	rm -rf $(BUILD_DIR)
	rm -rf $(PERF_DIR)/*
