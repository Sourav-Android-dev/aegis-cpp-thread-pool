#include <chrono>
#include <iostream>
#include <vector>

#include "aegis/thread_pool.hpp"

int main() {
    std::cout << "=== Aegis Thread Pool: Batch Processing Example ===\n";

    aegis::ThreadPoolConfig config;
    config.worker_count = 6;
    config.pool_name = "BatchProcessor";
    aegis::ThreadPool pool(config);

    constexpr size_t BATCH_SIZE = 50'000;
    std::cout << "Submitting " << BATCH_SIZE << " tasks in parallel...\n";

    auto start_time = std::chrono::high_resolution_clock::now();

    std::vector<std::function<int()>> tasks;
    tasks.reserve(BATCH_SIZE);

    for (size_t i = 0; i < BATCH_SIZE; ++i) {
        tasks.push_back([i]() { return static_cast<int>(i * 2); });
    }

    auto futures = pool.submit_batch(std::move(tasks), aegis::Priority::Normal);

    long long sum = 0;
    for (auto& f : futures) {
        sum += f.get();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration<double>(end_time - start_time).count();

    std::cout << "Finished " << BATCH_SIZE << " tasks in " << elapsed << " seconds.\n";
    std::cout << "Throughput: " << static_cast<double>(BATCH_SIZE) / elapsed << " tasks/sec.\n";
    std::cout << "Calculated sum: " << sum << "\n";

    std::cout << "\n" << pool.metrics().to_string() << "\n";
    return 0;
}
