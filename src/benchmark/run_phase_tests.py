import os
import subprocess
import re
import sys
import argparse
import statistics

# Detect OS to select correct binary extension
EXT = ".exe" if os.name == 'nt' else ""
BIN_SEQ = f"build/main_seq{EXT}"
BIN_MT = f"build/main_mt{EXT}"

def run_once(binary, dataset, num_threads=None):
    env = os.environ.copy()
    if num_threads is not None:
        env["OMP_NUM_THREADS"] = str(num_threads)
        
    try:
        # Run binary and capture output
        res = subprocess.run([binary, dataset], capture_output=True, text=True, check=True, env=env)
        
        # Parse stdout for "Total: XXX ms"
        total_match = re.search(r"Total:\s+([\d.]+)\s+ms", res.stdout)
        total_time = float(total_match.group(1)) if total_match else 0.0
        
        # Parse stderr for "[FASE] Leitura e Parsing: XXX ms" and "[FASE] Rotina Numerica: YYY ms"
        parsing_match = re.search(r"\[FASE\] Leitura e Parsing:\s+([\d.]+)\s+ms", res.stderr)
        routine_match = re.search(r"\[FASE\] Rotina Numerica:\s+([\d.]+)\s+ms", res.stderr)
        
        parsing_time = float(parsing_match.group(1)) if parsing_match else 0.0
        routine_time = float(routine_match.group(1)) if routine_match else 0.0
        
        return parsing_time, routine_time, total_time
        
    except subprocess.CalledProcessError as e:
        print(f"\nErro ao executar {binary}: {e}")
        print(f"Stdout: {e.stdout}")
        print(f"Stderr: {e.stderr}")
        sys.exit(1)

def main():
    parser = argparse.ArgumentParser(description="Benchmark de tempo de execução por fases.")
    parser.add_argument("--dataset", type=str, default="data/dataset_raw.csv", help="Caminho do arquivo de dataset.")
    parser.add_argument("--runs", type=int, default=5, help="Número de execuções para calcular a média (ignora a primeira de aquecimento).")
    args = parser.parse_args()
    
    if not os.path.exists(args.dataset):
        print(f"Erro: Dataset '{args.dataset}' não encontrado.")
        sys.exit(1)
        
    if not os.path.exists(BIN_SEQ) or not os.path.exists(BIN_MT):
        print("Erro: Binários não compilados. Execute 'make' antes de rodar o benchmark.")
        sys.exit(1)
        
    print(f"Iniciando benchmark de fases utilizando dataset: {args.dataset}")
    print(f"Número de execuções por configuração: {args.runs} (+1 de aquecimento)")
    
    configs = [
        {"name": "1 (Seq.)", "binary": BIN_MT, "threads": 1},
        {"name": "2", "binary": BIN_MT, "threads": 2},
        {"name": "4", "binary": BIN_MT, "threads": 4},
        {"name": "8", "binary": BIN_MT, "threads": 8},
    ]
    
    results = {}
    
    for config in configs:
        name = config["name"]
        binary = config["binary"]
        threads = config["threads"]
        
        print(f"\n=== Executando com {name} thread(s) ===")
        # Run once to warm up (cold cache, mmap load, disk read, etc.)
        print("  Aquecimento...", end="", flush=True)
        run_once(binary, args.dataset, threads)
        print(" OK")
        
        runs_parsing = []
        runs_routine = []
        runs_total = []
        
        for r in range(args.runs):
            print(f"  Rodada {r+1}/{args.runs}...", end="", flush=True)
            p_time, r_time, t_time = run_once(binary, args.dataset, threads)
            runs_parsing.append(p_time)
            runs_routine.append(r_time)
            runs_total.append(t_time)
            print(f" OK (Parsing: {p_time:.2f} ms, Rotina: {r_time:.2f} ms, Total: {t_time:.2f} ms)")
            
        results[name] = {
            "parsing": statistics.mean(runs_parsing),
            "routine": statistics.mean(runs_routine),
            "total": statistics.mean(runs_total)
        }
        
    # Calculate Speedup relative to the 1 (Seq.) MT run
    seq_total = results["1 (Seq.)"]["total"]
    
    # Generate the Markdown Table
    print("\n\n" + "="*80)
    print("TABELA DE RESULTADOS COMPARATIVOS (MULTITHREAD)")
    print("="*80 + "\n")
    
    # Header
    print("| Threads | Leitura e Parsing (ms) | Rotina Numérica (ms) | Total (ms) | Speedup |")
    print("| :--- | :---: | :---: | :---: | :---: |")
    
    # Rows
    for name in ["1 (Seq.)", "2", "4", "8"]:
        r = results[name]
        speedup = seq_total / r["total"] if r["total"] > 0 else 0.0
        
        # Localized float format (using comma as decimal separator, e.g., '1,82x')
        p_str = f"{r['parsing']:,.3f}".replace(".", "_").replace(",", ".").replace("_", ",")
        r_str = f"{r['routine']:,.3f}".replace(".", "_").replace(",", ".").replace("_", ",")
        t_str = f"{r['total']:,.3f}".replace(".", "_").replace(",", ".").replace("_", ",")
        sp_str = f"{speedup:.2f}x".replace(".", ",")
        
        print(f"| {name} | {p_str} | {r_str} | {t_str} | {sp_str} |")
        
    print("\n" + "="*80)


if __name__ == "__main__":
    main()
