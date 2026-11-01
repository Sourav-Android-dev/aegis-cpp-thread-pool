#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "aegis/scheduler.hpp"
#include "aegis/thread_pool.hpp"

void print_banner() {
    std::cout << "\033[1;36m"
              << "===============================================================\n"
              << "   AEGIS - High-Performance C++ Thread Pool & Task Scheduler   \n"
              << "   Production-Grade Multithreading Engine (C++20)              \n"
              << "===============================================================\n"
              << "\033[0m\n";
}

void print_help() {
    std::cout << "Usage: aegis_cli [options]\n"
              << "Options:\n"
              << "  -w, --workers <N>    Number of worker threads (default: hardware concurrency)\n"
              << "  -t, --tasks <N>      Number of tasks to execute (default: 5000)\n"
              << "  -p, --priority       Run priority scheduling demonstration\n"
              << "  -d, --delayed        Run delayed execution demonstration\n"
              << "  -s, --stealing       Enable work-stealing deque architecture\n"
              << "  -h, --help           Display this help manual\n\n";
}

int main(int argc, char* argv[]) {
    print_banner();

    size_t worker_count =
        (std::thread::hardware_concurrency() > 0) ? std::thread::hardware_concurrency() : 4;
    size_t task_count = 5000;
    bool demo_priority = false;
    bool demo_delayed = false;
    bool work_stealing = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-w" || arg == "--workers") {
            if (i + 1 < argc)
                worker_count = std::stoul(argv[++i]);
        } else if (arg == "-t" || arg == "--tasks") {
            if (i + 1 < argc)
                task_count = std::stoul(argv[++i]);
        } else if (arg == "-p" || arg == "--priority") {
            demo_priority = true;
        } else if (arg == "-d" || arg == "--delayed") {
            demo_delayed = true;
        } else if (arg == "-s" || arg == "--stealing") {
            work_stealing = true;
        } else if (arg == "-h" || arg == "--help") {
            print_help();
            return 0;
        }
    }

    std::cout << "\033[1;32m[Config]\033[0m Workers: " << worker_count << " | Tasks: " << task_count
              << " | Work Stealing: " << (work_stealing ? "Enabled" : "Disabled")
              << " | Priority Demo: " << (demo_priority ? "Yes" : "No")
              << " | Delayed Demo: " << (demo_delayed ? "Yes" : "No") << "\n\n";

    aegis::ThreadPoolConfig config;
    config.worker_count = worker_count;
    config.enable_work_stealing = work_stealing;
    config.pool_name = "CliPool";

    aegis::ThreadPool pool(config);

    if (demo_delayed) {
        std::cout << "\033[1;33m[*] Demonstrating Delayed Task Execution...\033[0m\n";
        auto start = std::chrono::steady_clock::now();
        auto f1 =
            pool.submit_delayed(std::chrono::milliseconds{150}, aegis::Priority::High, [start]() {
                auto now = std::chrono::steady_clock::now();
                auto ms =
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
                std::cout << "  -> Delayed task executed after " << ms << " ms.\n";
                return 42;
            });
        f1.get();
        std::cout << "\n";
    }

    if (demo_priority) {
        std::cout << "\033[1;33m[*] Demonstrating Multi-Priority Scheduling with Starvation "
                     "Mitigation...\033[0m\n";
        pool.submit_priority(aegis::Priority::Low,
                             []() { std::cout << "  -> [Low Priority] Dispatched.\n"; });
        pool.submit_priority(aegis::Priority::Critical,
                             []() { std::cout << "  -> [Critical Priority] Dispatched.\n"; });
        pool.submit_priority(aegis::Priority::High,
                             []() { std::cout << "  -> [High Priority] Dispatched.\n"; });
        pool.wait_for_all();
        std::cout << "\n";
    }

    std::cout << "\033[1;33m[*] Executing High-Throughput Batch of " << task_count
              << " Tasks...\033[0m\n";
    auto start_time = std::chrono::high_resolution_clock::now();

    std::atomic<uint64_t> completed_counter{0};
    for (size_t i = 0; i < task_count; ++i) {
        pool.submit([&completed_counter, i]() {
            double dummy = 1.0;
            for (int k = 0; k < 20; ++k) {
                dummy += k * 0.1;
            }
            completed_counter.fetch_add(1, std::memory_order_relaxed);
            (void)dummy;
        });
    }

    // Monitor progress
    while (completed_counter.load(std::memory_order_relaxed) < task_count) {
        size_t current = completed_counter.load(std::memory_order_relaxed);
        double pct = (static_cast<double>(current) / static_cast<double>(task_count)) * 100.0;
        std::cout << "\r  Progress: [" << std::setw(6) << current << " / " << task_count << "] ("
                  << std::fixed << std::setprecision(1) << pct
                  << "%) | Active Workers: " << pool.active_worker_count() << std::flush;
        std::this_thread::sleep_for(std::chrono::milliseconds{20});
    }

    pool.wait_for_all();
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration_sec = std::chrono::duration<double>(end_time - start_time).count();

    std::cout << "\r  Progress: [" << task_count << " / " << task_count
              << "] (100.0%) - Complete!          \n\n";

    std::cout << "\033[1;32m[Performance Result]\033[0m\n"
              << "  Total Time:    " << std::fixed << std::setprecision(4) << duration_sec
              << " seconds\n"
              << "  Throughput:    " << static_cast<double>(task_count) / duration_sec
              << " tasks/sec\n\n";

    auto metrics = pool.metrics();
    std::cout << "\033[1;34m" << metrics.to_string() << "\033[0m\n\n";

    pool.shutdown();
    std::cout << "\033[1;32m=== Aegis CLI Execution Finished ===\033[0m\n";
    return 0;
}
