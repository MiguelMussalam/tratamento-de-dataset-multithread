import subprocess
import re
import statistics
import csv
import sys
import os

NUM_RUNS = 100
DATASET = "data/02-20-2018.csv"

# Detect OS to select correct binary extension
EXT = ".exe" if os.name == 'nt' else ""
BIN_SEQ = f"build/main_seq{EXT}"
BIN_MT = f"build/main_mt{EXT}"

def run_and_parse(binary):
    if not os.path.exists(binary):
        print(f"Erro: Binário {binary} não encontrado. Você rodou 'make seq' ou o build no VSCode?")
        sys.exit(1)
        
    try:
        # Run process and capture output
        result = subprocess.run([binary, DATASET], capture_output=True, text=True, check=True)
        
        # Look for "Total: XXXX ms" in output
        match = re.search(r"Total:\s+(\d+)\s+ms", result.stdout)
        if match:
            return int(match.group(1))
        else:
            print(f"Aviso: Não foi possível encontrar o tempo na saída do {binary}")
            print(result.stdout)
            return None
    except subprocess.CalledProcessError as e:
        print(f"Erro ao executar {binary}: {e}")
        sys.exit(1)

def main():
    if not os.path.exists(DATASET):
        print(f"Erro: Dataset {DATASET} não encontrado.")
        sys.exit(1)

    print(f"Iniciando benchmark: {NUM_RUNS} execuções para Seq e MT...")
    
    results_seq = []
    results_mt = []

    # Run Sequencial
    print(f"Rodando Sequencial ({BIN_SEQ})...")
    for i in range(NUM_RUNS):
        print(f"\rProgresso Seq: {i+1}/{NUM_RUNS}", end="", flush=True)
        time_ms = run_and_parse(BIN_SEQ)
        if time_ms is not None:
            results_seq.append(time_ms)
    print()

    # Run Multithread
    print(f"Rodando Multithread ({BIN_MT})...")
    for i in range(NUM_RUNS):
        print(f"\rProgresso MT: {i+1}/{NUM_RUNS}", end="", flush=True)
        time_ms = run_and_parse(BIN_MT)
        if time_ms is not None:
            results_mt.append(time_ms)
    print()

    # Save to CSV
    csv_filename = "benchmark_results.csv"
    with open(csv_filename, 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(["Rodada", "Versao", "Tempo_ms"])
        for i, t in enumerate(results_seq):
            writer.writerow([i+1, "Sequencial", t])
        for i, t in enumerate(results_mt):
            writer.writerow([i+1, "Multithread", t])
    print(f"\nResultados brutos salvos em {csv_filename}")

    # Calculate statistics
    def print_stats(name, data):
        if not data:
            return 0
        mean = statistics.mean(data)
        stdev = statistics.stdev(data) if len(data) > 1 else 0
        min_v = min(data)
        max_v = max(data)
        print(f"\n--- Estatísticas: {name} ---")
        print(f"Média:  {mean:.2f} ms")
        print(f"Desvio: {stdev:.2f} ms")
        print(f"Mínimo: {min_v} ms")
        print(f"Máximo: {max_v} ms")
        return mean

    mean_seq = print_stats("Sequencial", results_seq)
    mean_mt = print_stats("Multithread", results_mt)

    if mean_mt > 0 and mean_seq > 0:
        speedup = mean_seq / mean_mt
        print(f"\n=================================")
        print(f"SPEEDUP: {speedup:.2f}x mais rápido!")
        print(f"=================================\n")

if __name__ == "__main__":
    main()
