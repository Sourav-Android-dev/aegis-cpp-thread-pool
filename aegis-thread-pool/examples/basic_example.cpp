#include <iostream>
#include <numeric>
#include <vector>

#include "aegis/thread_pool.hpp"

int main() {
    std::cout << "=== Aegis Thread Pool: Basic Example ===\n";

    // Initialize ThreadPool with 4 worker threads
    aegis::ThreadPoolConfig config;
    config.worker_count = 4;
    config.pool_name = "BasicExamplePool";
    aegis::ThreadPool pool(config);

    // 1. Submit void task
    auto f_void = pool.submit([]() { std::cout << "[Worker] Executing a void task...\n"; });

    // 2. Submit task with return value and arguments
    auto f_calc = pool.submit(
        [](int a, int b) {
            std::cout << "[Worker] Calculating " << a << " + " << b << "...\n";
            return a + b;
        },
        42, 58);

    // 3. Submit task with exception
    auto f_err = pool.submit([]() -> int {
        std::cout << "[Worker] Task throwing intentional exception...\n";
        throw std::runtime_error("Simulated computation failure");
    });

    // Wait and retrieve results
    f_void.get();
    std::cout << "[Main] Void task finished.\n";

    int sum = f_calc.get();
    std::cout << "[Main] Calculation result: " << sum << " (expected 100)\n";

    try {
        f_err.get();
    } catch (const std::exception& e) {
        std::cout << "[Main] Caught expected exception from worker: " << e.what() << "\n";
    }

    // Inspect metrics
    auto snap = pool.metrics();
    std::cout << "\n" << snap.to_string() << "\n";

    pool.shutdown();
    std::cout << "=== Basic Example Finished ===\n";
    return 0;
}
