#include <chrono>
#include <iostream>
#include <mutex>
#include <vector>

#include "aegis/thread_pool.hpp"

int main() {
    std::cout << "=== Aegis Thread Pool: Priority Scheduling & Starvation Example ===\n";

    // Single worker to make dispatch order deterministic and observable
    aegis::ThreadPoolConfig config;
    config.worker_count = 1;
    config.starvation_aging_threshold = std::chrono::milliseconds{100};
    aegis::ThreadPool pool(config);

    std::mutex print_mutex;
    auto log_task = [&print_mutex](std::string_view name, aegis::Priority prio) {
        std::lock_guard<std::mutex> lock(print_mutex);
        std::cout << "  Executed " << name << " (Priority: " << aegis::to_string(prio) << ")\n";
    };

    // First block worker briefly with a blocker task
    pool.submit([]() { std::this_thread::sleep_for(std::chrono::milliseconds{50}); });

    // Submit in order: Low, Normal, High, Critical
    pool.submit_priority(aegis::Priority::Low,
                         [&]() { log_task("Task-Low", aegis::Priority::Low); });
    pool.submit_priority(aegis::Priority::Normal,
                         [&]() { log_task("Task-Normal", aegis::Priority::Normal); });
    pool.submit_priority(aegis::Priority::High,
                         [&]() { log_task("Task-High", aegis::Priority::High); });
    pool.submit_priority(aegis::Priority::Critical,
                         [&]() { log_task("Task-Critical", aegis::Priority::Critical); });

    pool.wait_for_all();

    std::cout << "\nTask execution finished according to priority ordering.\n";
    return 0;
}
