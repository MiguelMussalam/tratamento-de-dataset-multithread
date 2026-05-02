#!/bin/bash
set -e

mkdir -p build resultados_perf

echo "Movendo arquivos de perf existentes para resultados_perf..."
for f in perf.data perf.data.old perf_functions.txt; do
  if [ -e "$f" ]; then
    mv -n "$f" resultados_perf/
  fi
done

echo "Compilando com flags de profiling..."
g++ -O3 -g -fno-omit-frame-pointer src/main.cpp src/dataset.cpp -Iinclude -std=c++20 -o build/main_profiling

echo "Rodando o projeto com perf record (contagens e callgraph)..."
perf record -g -s -e task-clock,page-faults,branches,branch-misses,instructions,cycles,L1-dcache-load-misses,LLC-loads,LLC-load-misses -o resultados_perf/perf.data ./build/main_profiling

echo ""
echo "=== Perfilamento concluído! ==="
echo "Arquivo de perf gravado em resultados_perf/perf.data"
echo "Para visualizar as contagens e a árvore de funções, execute:"
echo "perf report -g -T -i resultados_perf/perf.data"