CXX = g++
CXXFLAGS = -O3 -g -fno-omit-frame-pointer -std=c++20 -pthread -fopenmp
BUILD_DIR = build
PERF_DIR = resultados_perf
FLAMEGRAPH_DIR = tools/FlameGraph

# Arquivos fonte
SRC_SEQ = src/sequencial/main.cpp src/sequencial/dataset.cpp
INC_SEQ = -Iinclude/sequencial

SRC_MT = src/multithread/main.cpp src/multithread/dataset.cpp
INC_MT = -Iinclude/multithread

DATASET ?= data/dataset_raw.csv

PERF_EVENTS = task-clock,page-faults,branches,branch-misses,instructions,cycles,cpu_core/L1-dcache-load-misses/,LLC-loads,LLC-load-misses

# ==========================================
# COMPILAÇÃO
# ==========================================
all: seq mt

seq: $(BUILD_DIR)/main_seq

mt: $(BUILD_DIR)/main_mt

$(BUILD_DIR)/main_seq: $(SRC_SEQ)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(SRC_SEQ) $(INC_SEQ) -o $@

$(BUILD_DIR)/main_mt: $(SRC_MT)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(SRC_MT) $(INC_MT) -o $@

# ==========================================
# EXECUÇÃO SIMPLES (sem profiling)
# Uso: make run_seq DATASET=data/arquivo.csv
# ==========================================
run_seq: seq
	@echo "=== Executando versão Sequencial ==="
	./$(BUILD_DIR)/main_seq $(DATASET)

run_mt: mt
	@echo "=== Executando versão Multithread ==="
	./$(BUILD_DIR)/main_mt $(DATASET)

# ==========================================
# PROFILING COMPLETO
#
# Rodada 1 - perf stat : coleta métricas (rápido, sem gravar amostras)
# Rodada 2 - perf record: grava amostras para flamegraph e relatório de funções
# As duas rodadas são obrigatórias pois são ferramentas distintas do perf.
#
# Uso: make profile_seq DATASET=data/arquivo.csv
# ==========================================
$(FLAMEGRAPH_DIR):
	@echo "=== Baixando FlameGraph ==="
	@mkdir -p tools
	git clone --depth=1 https://github.com/brendangregg/FlameGraph $(FLAMEGRAPH_DIR)

profile_seq: seq $(FLAMEGRAPH_DIR)
	@mkdir -p $(PERF_DIR)/seq
	@echo "================================================"
	@echo " Profiling Sequencial — Rodada 1/2: métricas"
	@echo "================================================"
	@rm -f $(PERF_DIR)/seq/perf_stat.txt $(PERF_DIR)/seq/perf.data
	perf stat -e $(PERF_EVENTS) -o $(PERF_DIR)/seq/perf_stat.txt \
		./$(BUILD_DIR)/main_seq $(DATASET)
	@echo "================================================"
	@echo " Profiling Sequencial — Rodada 2/2: flamegraph"
	@echo "================================================"
	perf record -g -e $(PERF_EVENTS) -o $(PERF_DIR)/seq/perf.data \
		./$(BUILD_DIR)/main_seq $(DATASET)
	perf report --stdio -g -T -i $(PERF_DIR)/seq/perf.data \
		> $(PERF_DIR)/seq/perf_functions.txt 2>&1
	perf script -i $(PERF_DIR)/seq/perf.data \
		| $(FLAMEGRAPH_DIR)/stackcollapse-perf.pl \
		| $(FLAMEGRAPH_DIR)/flamegraph.pl --title="Sequencial - $(DATASET)" --width=1400 \
		> $(PERF_DIR)/seq/flamegraph.svg
	@echo "================================================"
	@echo " Sequencial concluído! Arquivos em $(PERF_DIR)/seq/"
	@echo "   perf_stat.txt      <- métricas de performance"
	@echo "   perf_functions.txt <- hotspots por função"
	@echo "   flamegraph.svg     <- visualização interativa"
	@echo "================================================"

profile_mt: mt $(FLAMEGRAPH_DIR)
	@mkdir -p $(PERF_DIR)/mt
	@echo "================================================"
	@echo " Profiling Multithread — Rodada 1/2: métricas"
	@echo "================================================"
	@rm -f $(PERF_DIR)/mt/perf_stat.txt $(PERF_DIR)/mt/perf.data
	perf stat -e $(PERF_EVENTS) -o $(PERF_DIR)/mt/perf_stat.txt \
		./$(BUILD_DIR)/main_mt $(DATASET)
	@echo "================================================"
	@echo " Profiling Multithread — Rodada 2/2: flamegraph"
	@echo "================================================"
	perf record -g -e $(PERF_EVENTS) -o $(PERF_DIR)/mt/perf.data \
		./$(BUILD_DIR)/main_mt $(DATASET)
	perf report --stdio -g -T -i $(PERF_DIR)/mt/perf.data \
		> $(PERF_DIR)/mt/perf_functions.txt 2>&1
	perf script -i $(PERF_DIR)/mt/perf.data \
		| $(FLAMEGRAPH_DIR)/stackcollapse-perf.pl \
		| $(FLAMEGRAPH_DIR)/flamegraph.pl --title="Multithread - $(DATASET)" --width=1400 \
		> $(PERF_DIR)/mt/flamegraph.svg
	@echo "================================================"
	@echo " Multithread concluído! Arquivos em $(PERF_DIR)/mt/"
	@echo "   perf_stat.txt      <- métricas de performance"
	@echo "   perf_functions.txt <- hotspots por função"
	@echo "   flamegraph.svg     <- visualização interativa"
	@echo "================================================"

profile: profile_seq profile_mt
	@echo ""
	@echo "======================================================"
	@echo " Profiling completo! Arquivos gerados:"
	@echo "======================================================"
	@echo " Sequencial : $(PERF_DIR)/seq/"
	@echo " Multithread: $(PERF_DIR)/mt/"
	@echo "======================================================"

# ==========================================
# LIMPEZA
# ==========================================
clean:
	rm -rf $(BUILD_DIR)
	rm -rf $(PERF_DIR)/*
