#include <gtest/gtest.h>

#include <atomic>
#include <chrono>

#include "aegis/thread_pool.hpp"

TEST(ShutdownTest, GracefulDrainExecutesAllQueuedTasks) {
    aegis::ThreadPoolConfig config;
    config.worker_count = 2;
    config.shutdown_policy = aegis::ShutdownPolicy::GracefulDrain;

    std::atomic<int> completed{0};
    {
        aegis::ThreadPool pool(config);

        for (int i = 0; i < 20; ++i) {
            pool.submit([&completed]() {
                std::this_thread::sleep_for(std::chrono::milliseconds{5});
                completed.fetch_add(1, std::memory_order_relaxed);
            });
        }
        // pool destructs here and must drain all 20 tasks
    }

    EXPECT_EQ(completed.load(), 20);
}

TEST(ShutdownTest, ImmediateCancelStopsPendingTasks) {
    aegis::ThreadPoolConfig config;
    config.worker_count = 1;
    config.shutdown_policy = aegis::ShutdownPolicy::ImmediateCancel;

    std::atomic<int> completed{0};
    {
        aegis::ThreadPool pool(config);

        // Blocker task
        pool.submit([]() { std::this_thread::sleep_for(std::chrono::milliseconds{50}); });

        // Queue remaining tasks
        for (int i = 0; i < 20; ++i) {
            pool.submit([&completed]() { completed.fetch_add(1, std::memory_order_relaxed); });
        }

        pool.shutdown_immediate();
    }

    // Unstarted tasks should have been cancelled
    EXPECT_LT(completed.load(), 20);
}

TEST(ShutdownTest, SubmissionAfterShutdownThrows) {
    aegis::ThreadPool pool(2);
    pool.shutdown();

    EXPECT_THROW({ pool.submit([]() {}); }, std::runtime_error);
}
