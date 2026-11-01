#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

#include "aegis/thread_pool.hpp"

TEST(ConcurrencyTest, MultipleProducersConcurrentSubmission) {
    constexpr size_t PRODUCER_COUNT = 8;
    constexpr size_t TASKS_PER_PRODUCER = 500;
    constexpr size_t TOTAL_EXPECTED = PRODUCER_COUNT * TASKS_PER_PRODUCER;

    aegis::ThreadPool pool(4);
    std::atomic<uint64_t> counter{0};

    std::vector<std::thread> producers;
    producers.reserve(PRODUCER_COUNT);

    for (size_t p = 0; p < PRODUCER_COUNT; ++p) {
        producers.emplace_back([&pool, &counter]() {
            for (size_t i = 0; i < TASKS_PER_PRODUCER; ++i) {
                pool.submit([&counter]() { counter.fetch_add(1, std::memory_order_relaxed); });
            }
        });
    }

    for (auto& th : producers) {
        th.join();
    }

    pool.wait_for_all();
    EXPECT_EQ(counter.load(), TOTAL_EXPECTED);
}

TEST(ConcurrencyTest, WorkStealingPoolExecution) {
    aegis::ThreadPoolConfig config;
    config.worker_count = 4;
    config.enable_work_stealing = true;
    aegis::ThreadPool pool(config);

    std::atomic<uint64_t> sum{0};
    constexpr size_t TASK_COUNT = 2000;

    for (size_t i = 0; i < TASK_COUNT; ++i) {
        pool.submit([&sum, i]() { sum.fetch_add(i, std::memory_order_relaxed); });
    }

    pool.wait_for_all();

    uint64_t expected_sum = 0;
    for (size_t i = 0; i < TASK_COUNT; ++i)
        expected_sum += i;

    EXPECT_EQ(sum.load(), expected_sum);
}

TEST(ConcurrencyTest, DynamicWorkerPoolResize) {
    aegis::ThreadPool pool(2);
    EXPECT_EQ(pool.worker_count(), 2);

    pool.resize(6);
    EXPECT_EQ(pool.worker_count(), 6);

    std::atomic<int> completed{0};
    for (int i = 0; i < 50; ++i) {
        pool.submit([&completed]() {
            std::this_thread::sleep_for(std::chrono::milliseconds{2});
            completed.fetch_add(1, std::memory_order_relaxed);
        });
    }

    pool.resize(3);
    EXPECT_EQ(pool.worker_count(), 3);

    pool.wait_for_all();
    EXPECT_EQ(completed.load(), 50);
}

TEST(ConcurrencyTest, ConcurrentWorkerResizingAndPeerAccess) {
    // Verifies Issue 4: Concurrent resize and work-stealing peer access does not data-race
    aegis::ThreadPoolConfig config;
    config.worker_count = 4;
    config.enable_work_stealing = true;
    aegis::ThreadPool pool(config);

    std::atomic<bool> stop_flag{false};
    std::atomic<uint64_t> total_processed{0};

    // Producer thread constantly submitting work-stealing tasks
    std::thread producer([&pool, &stop_flag, &total_processed]() {
        while (!stop_flag.load(std::memory_order_relaxed)) {
            pool.submit(
                [&total_processed]() { total_processed.fetch_add(1, std::memory_order_relaxed); });
            std::this_thread::yield();
        }
    });

    // Resizer thread repeatedly changing worker counts and peer lists concurrently
    std::thread resizer([&pool, &stop_flag]() {
        for (int i = 0; i < 15; ++i) {
            size_t target_workers = (i % 2 == 0) ? 6 : 2;
            pool.resize(target_workers);
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
        }
        stop_flag.store(true, std::memory_order_relaxed);
    });

    resizer.join();
    producer.join();

    pool.wait_for_all();
    EXPECT_GT(total_processed.load(), 0);
}
