#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <vector>

#include "aegis/thread_pool.hpp"

TEST(ThreadPoolTest, ExecutesVoidTask) {
    aegis::ThreadPool pool(2);
    bool executed = false;

    auto future = pool.submit([&executed]() { executed = true; });

    future.get();
    EXPECT_TRUE(executed);
}

TEST(ThreadPoolTest, ReturnsValuesOfDifferentTypes) {
    aegis::ThreadPool pool(4);

    auto int_future = pool.submit([]() { return 42; });
    auto string_future = pool.submit([]() { return std::string("AegisEngine"); });
    auto double_future = pool.submit([]() { return 3.14159; });

    EXPECT_EQ(int_future.get(), 42);
    EXPECT_EQ(string_future.get(), "AegisEngine");
    EXPECT_DOUBLE_EQ(double_future.get(), 3.14159);
}

TEST(ThreadPoolTest, PropagatesExceptionsCorrectly) {
    aegis::ThreadPool pool(2);

    auto future =
        pool.submit([]() -> int { throw std::invalid_argument("Invalid argument simulated"); });

    EXPECT_THROW(future.get(), std::invalid_argument);
}

TEST(ThreadPoolTest, ExecutesBatchTasks) {
    aegis::ThreadPool pool(4);
    constexpr size_t COUNT = 100;

    std::vector<std::function<int()>> tasks;
    for (size_t i = 0; i < COUNT; ++i) {
        tasks.push_back([i]() { return static_cast<int>(i * 3); });
    }

    auto futures = pool.submit_batch(std::move(tasks));
    EXPECT_EQ(futures.size(), COUNT);

    for (size_t i = 0; i < COUNT; ++i) {
        EXPECT_EQ(futures[i].get(), static_cast<int>(i * 3));
    }
}

TEST(ThreadPoolTest, TaskCancellationBeforeStart) {
    aegis::ThreadPoolConfig config;
    config.worker_count = 1;
    aegis::ThreadPool pool(config);

    // Blocker to hold the single worker
    auto blocker =
        pool.submit([]() { std::this_thread::sleep_for(std::chrono::milliseconds{80}); });

    // Submit target task
    std::atomic<bool> executed{false};
    auto future = pool.submit_named("ToCancel", aegis::Priority::Low,
                                    [&executed]() { executed.store(true); });

    // Cancel task ID 2 (first is blocker: id 1, second: id 2)
    bool cancelled = pool.cancel(2);
    EXPECT_TRUE(cancelled);

    blocker.get();
    pool.wait_for_all();

    EXPECT_FALSE(executed.load());
    auto status = pool.get_task_status(2);
    ASSERT_TRUE(status.has_value());
    EXPECT_EQ(*status, aegis::TaskStatus::Cancelled);
}

TEST(ThreadPoolTest, BoundedQueueWithRejectPolicy) {
    aegis::ThreadPoolConfig config;
    config.worker_count = 1;
    config.queue_capacity = 2;
    config.overload_policy = aegis::OverloadPolicy::Reject;
    aegis::ThreadPool pool(config);

    std::atomic<bool> blocker_started{false};
    std::atomic<bool> allow_blocker_exit{false};

    pool.submit([&blocker_started, &allow_blocker_exit]() {
        blocker_started.store(true);
        while (!allow_blocker_exit.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds{2});
        }
    });

    // Wait until blocker has popped and is executing
    while (!blocker_started.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    // Queue capacity is 2; submit 2 tasks to fill it completely
    pool.submit([]() {});
    pool.submit([]() {});

    // Next submit must throw QueueOverflowException
    EXPECT_THROW({ pool.submit([]() {}); }, aegis::QueueOverflowException);

    allow_blocker_exit.store(true);
    pool.wait_for_all();
}
