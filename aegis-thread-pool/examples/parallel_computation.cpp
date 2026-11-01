#include <chrono>
#include <cmath>
#include <iostream>
#include <numeric>
#include <vector>

#include "aegis/thread_pool.hpp"

int main() {
    std::cout << "=== Aegis Thread Pool: Parallel Computation Example ===\n";

    constexpr size_t TOTAL_ELEMENTS = 20'000'000;
    constexpr size_t NUM_CHUNKS = 8;
    constexpr size_t CHUNK_SIZE = TOTAL_ELEMENTS / NUM_CHUNKS;

    std::vector<double> data(TOTAL_ELEMENTS, 1.0);

    aegis::ThreadPoolConfig config;
    config.worker_count = 8;
    config.pool_name = "ComputePool";
    aegis::ThreadPool pool(config);

    auto start_time = std::chrono::high_resolution_clock::now();

    // Spawn chunk processing tasks
    std::vector<std::future<double>> futures;
    for (size_t i = 0; i < NUM_CHUNKS; ++i) {
        size_t start_idx = i * CHUNK_SIZE;
        size_t end_idx = (i == NUM_CHUNKS - 1) ? TOTAL_ELEMENTS : (i + 1) * CHUNK_SIZE;

        futures.push_back(pool.submit([&data, start_idx, end_idx]() {
            double local_sum = 0.0;
            for (size_t j = start_idx; j < end_idx; ++j) {
                local_sum += std::sqrt(data[j] * 4.0);
            }
            return local_sum;
        }));
    }

    double total_sum = 0.0;
    for (auto& f : futures) {
        total_sum += f.get();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    std::cout << "Parallel reduction processed " << TOTAL_ELEMENTS << " items in " << duration_ms
              << " ms.\n";
    std::cout << "Total computed sum: " << total_sum << "\n";

    auto metrics = pool.metrics();
    std::cout << "\n" << metrics.to_string() << "\n";

    return 0;
}
