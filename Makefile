CXX = g++
CXXFLAGS = -O3 -g -fno-omit-frame-pointer -std=c++20 -pthread -fopenmp

BUILD_DIR = build
PERF_DIR = resultados_perf
FLAMEGRAPH_DIR = tools/FlameGraph

DATASET ?= data/dataset_raw.csv

PERF_EVENTS = task-clock,page-faults,branches,branch-misses,instructions,cycles,cpu_core/L1-dcache-load-misses/,LLC-loads,LLC-load-misses

# ==========================================================
# DETECÇÃO DO SISTEMA OPERACIONAL
# ==========================================================

ifeq ($(OS),Windows_NT)
    EXE = .exe
    PATH_SEP = \

    # Criar diretórios (ignora se já existirem)
    define MKDIR_P
        if not exist "$(subst /,\,$1)" mkdir "$(subst /,\,$1)"
    endef

    # Remover diretórios
    define RM_RF
        if exist "$(subst /,\,$1)" rmdir /s /q "$(subst /,\,$1)"
    endef

    # Remover arquivos
    define RM_F
        if exist "$(subst /,\,$1)" del /q "$(subst /,\,$1)"
    endef
else
    EXE =
    PATH_SEP = /

    define MKDIR_P
        mkdir -p "$1"
    endef

    define RM_RF
        rm -rf "$1"
    endef

    define RM_F
        rm -f "$1"
    endef
endif

# ==========================================================
# ARQUIVOS FONTE
# ==========================================================

SRC_SEQ = src/sequencial/main.cpp src/sequencial/dataset.cpp
INC_SEQ = -Iinclude/sequencial

SRC_MT = src/multithread/main.cpp src/multithread/dataset.cpp
INC_MT = -Iinclude/multithread

BIN_SEQ = $(BUILD_DIR)/main_seq$(EXE)
BIN_MT  = $(BUILD_DIR)/main_mt$(EXE)

# ==========================================================
# COMPILAÇÃO
# ==========================================================

.PHONY: all seq mt run_seq run_mt clean

all: seq mt

seq: $(BIN_SEQ)

mt: $(BIN_MT)

$(BIN_SEQ): $(SRC_SEQ)
	@$(call MKDIR_P,$(BUILD_DIR))
	$(CXX) $(CXXFLAGS) $(SRC_SEQ) $(INC_SEQ) -o $@

$(BIN_MT): $(SRC_MT)
	@$(call MKDIR_P,$(BUILD_DIR))
	$(CXX) $(CXXFLAGS) $(SRC_MT) $(INC_MT) -o $@

# ==========================================================
# EXECUÇÃO
# ==========================================================

run_seq: seq
	@echo === Executando versão Sequencial ===
	./$(BIN_SEQ) $(DATASET)

run_mt: mt
	@echo === Executando versão Multithread ===
	./$(BIN_MT) $(DATASET)

# ==========================================================
# FLAMEGRAPH
# ==========================================================

$(FLAMEGRAPH_DIR):
	@echo === Baixando FlameGraph ===
	@$(call MKDIR_P,tools)
	git clone --depth=1 https://github.com/brendangregg/FlameGraph $(FLAMEGRAPH_DIR)

# ==========================================================
# PROFILING (recomendado em Linux/WSL)
# ==========================================================

profile_seq: seq $(FLAMEGRAPH_DIR)
	@$(call MKDIR_P,$(PERF_DIR)/seq)
	@echo ================================================
	@echo Profiling Sequencial
	@echo ================================================
	@$(call RM_F,$(PERF_DIR)/seq/perf_stat.txt)
	@$(call RM_F,$(PERF_DIR)/seq/perf.data)

	perf stat -e $(PERF_EVENTS) -o $(PERF_DIR)/seq/perf_stat.txt \
		./$(BIN_SEQ) $(DATASET)

	perf record -g -e $(PERF_EVENTS) -o $(PERF_DIR)/seq/perf.data \
		./$(BIN_SEQ) $(DATASET)

	perf report --stdio -g -T -i $(PERF_DIR)/seq/perf.data \
		> $(PERF_DIR)/seq/perf_functions.txt 2>&1

	perf script -i $(PERF_DIR)/seq/perf.data \
		| $(FLAMEGRAPH_DIR)/stackcollapse-perf.pl \
		| $(FLAMEGRAPH_DIR)/flamegraph.pl --title="Sequencial - $(DATASET)" --width=1400 \
		> $(PERF_DIR)/seq/flamegraph.svg

profile_mt: mt $(FLAMEGRAPH_DIR)
	@$(call MKDIR_P,$(PERF_DIR)/mt)
	@echo ================================================
	@echo Profiling Multithread
	@echo ================================================
	@$(call RM_F,$(PERF_DIR)/mt/perf_stat.txt)
	@$(call RM_F,$(PERF_DIR)/mt/perf.data)

	perf stat -e $(PERF_EVENTS) -o $(PERF_DIR)/mt/perf_stat.txt \
		./$(BIN_MT) $(DATASET)

	perf record -g -e $(PERF_EVENTS) -o $(PERF_DIR)/mt/perf.data \
		./$(BIN_MT) $(DATASET)

	perf report --stdio -g -T -i $(PERF_DIR)/mt/perf.data \
		> $(PERF_DIR)/mt/perf_functions.txt 2>&1

	perf script -i $(PERF_DIR)/mt/perf.data \
		| $(FLAMEGRAPH_DIR)/stackcollapse-perf.pl \
		| $(FLAMEGRAPH_DIR)/flamegraph.pl --title="Multithread - $(DATASET)" --width=1400 \
		> $(PERF_DIR)/mt/flamegraph.svg

profile: profile_seq profile_mt

# ==========================================================
# LIMPEZA
# ==========================================================

clean:
	@$(call RM_RF,$(BUILD_DIR))
	@$(call RM_RF,$(PERF_DIR))