#pragma once
#include <vector>
#include <string>
#include <fstream>
#include <thread>
#include <unordered_map>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>

template<typename T>
class ThreadSafeQueue {
private:
    std::queue<T> queue;
    std::mutex mutex;
    std::condition_variable cond_push;
    std::condition_variable cond_pop;
    bool finished = false;
    size_t max_size;

public:
    ThreadSafeQueue(size_t max_size = 1000) : max_size(max_size) {}

    void push(T value) {
        std::unique_lock<std::mutex> lock(mutex);
        cond_push.wait(lock, [this]{ return queue.size() < max_size; });
        queue.push(std::move(value));
        cond_pop.notify_one();
    }

    bool pop(T& value) {
        std::unique_lock<std::mutex> lock(mutex);
        cond_pop.wait(lock, [this]{ return !queue.empty() || finished; });
        if (queue.empty() && finished) {
            return false;
        }
        value = std::move(queue.front());
        queue.pop();
        cond_push.notify_one();
        return true;
    }

    void setFinished() {
        std::lock_guard<std::mutex> lock(mutex);
        finished = true;
        cond_pop.notify_all();
    }
};

enum TipoColuna {
    CATEGORICA,
    NUMERICA,
    DESCONHECIDA
};

struct Coluna {
    std::string nome;
    TipoColuna tipo = DESCONHECIDA;
    std::vector<double> valores;
    std::unordered_map<std::string, double> mapeamento;
    long long tempo_categorizacao = 0;
    
    ThreadSafeQueue<std::vector<std::string>> fila_dados;
};

class Dataset {
private:
    std::vector<std::unique_ptr<Coluna>> colunas;
    std::size_t num_linhas = 0;
    std::size_t num_colunas = 0;

    void lerArquivo(const std::string& caminho);
    void categorizarColuna(int indice_coluna);

public:
    Dataset(const std::string& caminho);

    const Coluna& getColuna(int indice) const;

    template<typename T>
    T calcularMedia(int indice_coluna);
};