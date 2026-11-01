#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <vector>

#include "aegis/thread_pool.hpp"

TEST(StressTest, HighVolumeConcurrentSubmission) {
    constexpr size_t THREAD_COUNT = 8;
    constexpr size_t TASKS_TOTAL = 100'000;
    constexpr size_t TASKS_PER_THREAD = TASKS_TOTAL / THREAD_COUNT;

    aegis::ThreadPoolConfig config;
    config.worker_count = 8;
    config.pool_name = "StressPool";
    aegis::ThreadPool pool(config);

    std::atomic<uint64_t> execution_counter{0};
    std::vector<std::thread> producers;
    producers.reserve(THREAD_COUNT);

    auto start_time = std::chrono::high_resolution_clock::now();

    for (size_t t = 0; t < THREAD_COUNT; ++t) {
        producers.emplace_back([&pool, &execution_counter]() {
            for (size_t i = 0; i < TASKS_PER_THREAD; ++i) {
                pool.submit([&execution_counter]() {
                    execution_counter.fetch_add(1, std::memory_order_relaxed);
                });
            }
        });
    }

    for (auto& t : producers) {
        t.join();
    }

    pool.wait_for_all();
    auto end_time = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration<double>(end_time - start_time).count();

    EXPECT_EQ(execution_counter.load(), TASKS_TOTAL);

    auto metrics = pool.metrics();
    EXPECT_EQ(metrics.completed_tasks, TASKS_TOTAL);
    EXPECT_EQ(metrics.failed_tasks, 0);
    EXPECT_EQ(metrics.cancelled_tasks, 0);
    EXPECT_EQ(metrics.pending_tasks, 0);
    EXPECT_GT(metrics.tasks_per_second, 0.0);

    std::cout << "[Stress Test] Successfully processed " << TASKS_TOTAL << " tasks in " << elapsed
              << " s (" << (TASKS_TOTAL / elapsed) << " tasks/sec).\n";
}
