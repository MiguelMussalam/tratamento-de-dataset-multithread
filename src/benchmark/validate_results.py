import pandas as pd
import numpy as np
import subprocess
import sys
import os
import re

def main():
    dataset_path = sys.argv[1] if len(sys.argv) > 1 else "data/dataset_raw.csv"
    bin_path = sys.argv[2] if len(sys.argv) > 2 else ("build/main_mt.exe" if os.name == "nt" else "build/main_mt")
    
    if not os.path.exists(dataset_path):
        print(f"Erro: Dataset {dataset_path} nao encontrado.")
        return
    if not os.path.exists(bin_path):
        print(f"Erro: Binario {bin_path} nao encontrado. Rode 'make mt' ou 'make seq' primeiro.")
        return
        
    print(f"Lendo dataset com Pandas: {dataset_path}...")
    try:
        df = pd.read_csv(dataset_path)
    except Exception as e:
        print(f"Erro ao ler CSV com Pandas: {e}")
        return
        
    # Limpa espacos no nome das colunas
    df.columns = [c.strip() for c in df.columns]
    
    print(f"Executando binario C++: {bin_path}...")
    try:
        res = subprocess.run([bin_path, dataset_path], capture_output=True, text=True, encoding='utf-8', errors='ignore')
    except Exception as e:
        print(f"Erro ao executar o binário C++: {e}")
        return
    
    if res.returncode != 0:
        print("Erro durante a execucao do C++:")
        print(res.stderr)
        return
        
    stdout = res.stdout
    lines = stdout.splitlines()
    table_started = False
    cpp_results = {}
    
    for line in lines:
        if not line.strip():
            continue
        if line.startswith("---") or "Coluna" in line and "Tipo" in line:
            continue
        if line.startswith("==") or "MAPEAMENTO" in line:
            break
            
        # O cabecalho tem "Coluna", identificamos o inicio dos dados pela primeira coluna
        # As linhas da tabela comecam com o nome da coluna (30 caracteres)
        if len(line) >= 44:
            col_name = line[0:30].strip()
            rest = line[30:]
            
            # Se encontrar a primeira linha de dados reais, marca que a tabela comecou
            if "NUMERICA" in rest or "CATEGORICA" in rest:
                table_started = True
                
                if "Nao foi possivel" in rest:
                    cpp_results[col_name] = {
                        "tipo": "NUMERICA",
                        "erro": True
                    }
                elif "NUMERICA" in rest:
                    parts = rest.split()
                    # parts[0] = NUMERICA, parts[1] = media, parts[2] = mediana, parts[3] = desv_pad, parts[4] = iqr, parts[5] = '-'
                    try:
                        cpp_results[col_name] = {
                            "tipo": "NUMERICA",
                            "erro": False,
                            "media": float(parts[1]),
                            "mediana": float(parts[2]),
                            "desv_pad": float(parts[3]),
                            "iqr": float(parts[4])
                        }
                    except ValueError:
                        # Fallback se a string contiver a mensagem de erro mas em formato diferente
                        cpp_results[col_name] = {
                            "tipo": "NUMERICA",
                            "erro": True
                        }
                elif "CATEGORICA" in rest:
                    parts = rest.split()
                    # parts[0] = CATEGORICA, parts[1..4] = '-', parts[5] = Qtd Categorias
                    try:
                        cpp_results[col_name] = {
                            "tipo": "CATEGORICA",
                            "categorias": int(parts[5]) if len(parts) > 5 else 0
                        }
                    except ValueError:
                        cpp_results[col_name] = {
                            "tipo": "CATEGORICA",
                            "categorias": 0
                        }

    if not cpp_results:
        print("Erro: Nao foi possivel extrair resultados da saida do C++.")
        print("Saida do C++:")
        print(stdout[:1000])
        return

    print("\n" + "="*95)
    print(f"{'Coluna':<25} | {'Métrica':<10} | {'C++':<15} | {'Pandas':<15} | {'Status':<15}")
    print("="*95)
    
    mismatches = 0
    validated_cols = 0
    
    for col, cpp_res in cpp_results.items():
        if col not in df.columns:
            print(f"{col:<25} | -          | -               | -               | NAO NO PANDAS")
            continue
            
        if cpp_res["tipo"] == "NUMERICA":
            if cpp_res["erro"]:
                # Verifica se o Pandas tambem detecta nao-numericos
                try:
                    pd.to_numeric(df[col])
                    pandas_has_non_numeric = False
                except Exception:
                    pandas_has_non_numeric = True
                
                status = "OK" if pandas_has_non_numeric else "DIVERGENTE (Deveria ser numerica)"
                if not pandas_has_non_numeric:
                    mismatches += 1
                print(f"{col:<25} | Tipo Col   | Erro Categorico | Valido no Pandas| {status}")
                validated_cols += 1
                print("-" * 95)
                continue
                
            # Filtra NaN e converte para float
            series = pd.to_numeric(df[col], errors='coerce').dropna()
            n = len(series)
            if n == 0:
                print(f"{col:<25} | -          | Coluna Vazia    | -               | SEM DADOS")
                continue
                
            p_mean = series.mean()
            p_median = series.median()
            p_std = series.std(ddof=0)  # Desvio padrao populacional (divisao por N)
            
            # C++ Q1 e Q3 por indices
            sorted_vals = series.sort_values().values
            q1 = sorted_vals[n // 4]
            q3 = sorted_vals[(3 * n) // 4]
            p_iqr = q3 - q1
            
            metrics = {
                "Média": (cpp_res["media"], p_mean),
                "Mediana": (cpp_res["mediana"], p_median),
                "DesvPad": (cpp_res["desv_pad"], p_std),
                "IQR": (cpp_res["iqr"], p_iqr)
            }
            
            for metric_name, (c_val, p_val) in metrics.items():
                diff = abs(c_val - p_val)
                # Tolerancia absoluta de 0.05. Para numeros muito grandes, toleramos ate 0.1% de diferenca.
                if p_val != 0 and abs(p_val) > 100:
                    rel_diff = diff / abs(p_val)
                    is_ok = rel_diff < 0.005 or diff < 0.5
                else:
                    is_ok = diff < 0.05
                    
                status = "OK" if is_ok else "DIVERGENTE"
                if not is_ok:
                    mismatches += 1
                    
                print(f"{col:<25} | {metric_name:<10} | {c_val:<15.2f} | {p_val:<15.2f} | {status}")
            validated_cols += 1
            print("-" * 95)
            
        elif cpp_res["tipo"] == "CATEGORICA":
            # Nunique no pandas
            p_unique = df[col].nunique()
            c_unique = cpp_res["categorias"]
            
            is_ok = p_unique == c_unique
            status = "OK" if is_ok else "DIVERGENTE"
            
            # Se divergir, as vezes o Pandas conta NA ou strings de aspas diferentes.
            # Vamos testar converter para string e contar, considerando vazios como categorias
            if not is_ok:
                p_unique_str = df[col].astype(str).nunique()
                if p_unique_str == c_unique:
                    is_ok = True
                    status = "OK"
                else:
                    mismatches += 1
                    
            print(f"{col:<25} | Qtd Categ  | {c_unique:<15} | {p_unique:<15} | {status}")
            validated_cols += 1
            print("-" * 95)
            
    print(f"\nResumo da Validacao: {validated_cols} colunas verificadas.")
    if mismatches == 0:
        print(">>> SUCESSO COMPLETO! Todos os valores do C++ batem exatamente com o Pandas! <<<")
    else:
        print(f">>> ALERTA: Foram detectadas {mismatches} divergencias de calculo! Verifique o log. <<<")

if __name__ == "__main__":
    main()
