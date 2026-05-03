#!/bin/bash
set -e

if [ "$1" == "seq" ] || [ "$1" == "sequencial" ]; then
    SRC_DIR="src/sequencial"
    INC_DIR="include/sequencial"
    BIN_NAME="main_seq"
    PERF_OUT_DIR="resultados_perf/seq"
    echo "=== Iniciando perfilamento: SEQUENCIAL ==="
elif [ "$1" == "mt" ] || [ "$1" == "multithread" ]; then
    SRC_DIR="src/multithread"
    INC_DIR="include/multithread"
    BIN_NAME="main_mt"
    PERF_OUT_DIR="resultados_perf/mt"
    echo "=== Iniciando perfilamento: MULTITHREAD ==="
else
    echo "Uso: ./run.sh [seq | mt]"
    exit 1
fi

mkdir -p build $PERF_OUT_DIR

rm -f $PERF_OUT_DIR/perf.data $PERF_OUT_DIR/perf.data.old $PERF_OUT_DIR/perf_stat.txt

g++ -O3 -g -fno-omit-frame-pointer $SRC_DIR/main.cpp $SRC_DIR/dataset.cpp -I$INC_DIR -std=c++20 -pthread -o build/$BIN_NAME

echo "Criando perf_stat.txt"
perf stat -e task-clock,page-faults,branches,branch-misses,instructions,cycles,L1-dcache-load-misses,LLC-loads,LLC-load-misses -o $PERF_OUT_DIR/perf_stat.txt ./build/$BIN_NAME

echo ""
echo "Criando perf.data"
perf record -g -s -e task-clock,page-faults,branches,branch-misses,instructions,cycles,L1-dcache-load-misses,LLC-loads,LLC-load-misses -o $PERF_OUT_DIR/perf.data ./build/$BIN_NAME

echo ""
echo "Criando perf_functions.txt"
perf report --stdio -g -T -i $PERF_OUT_DIR/perf.data > $PERF_OUT_DIR/perf_functions.txt 2>&1

echo ""
echo "Criando perf_trace.txt"
perf script -i $PERF_OUT_DIR/perf.data > $PERF_OUT_DIR/perf_trace.txt 2>&1

echo ""
echo "Profiling concluído!"
echo "Arquivos gerados em $PERF_OUT_DIR/"