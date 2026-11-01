#include <gtest/gtest.h>

#include <chrono>
#include <mutex>
#include <vector>

#include "aegis/scheduler.hpp"

TEST(SchedulerTest, BasicScheduling) {
    aegis::Scheduler scheduler(2);
    auto fut = scheduler.schedule([]() { return 100; });
    EXPECT_EQ(fut.get(), 100);
}

TEST(SchedulerTest, DelayedTaskExecution) {
    aegis::Scheduler scheduler(2);
    auto start = std::chrono::steady_clock::now();

    auto fut = scheduler.schedule_after(std::chrono::milliseconds{100}, aegis::Priority::Normal,
                                        []() { return 42; });

    int res = fut.get();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    EXPECT_EQ(res, 42);
    EXPECT_GE(elapsed.count(), 80);  // Ensure it waited approximately at least the delay
}

TEST(SchedulerTest, PriorityExecutionOrder) {
    aegis::ThreadPoolConfig config;
    config.worker_count = 1;
    config.starvation_aging_threshold = std::chrono::milliseconds{1000};
    aegis::Scheduler scheduler(config);

    std::vector<int> execution_order;
    std::mutex mtx;

    // Blocker to let us queue multiple tasks
    auto blocker =
        scheduler.schedule([]() { std::this_thread::sleep_for(std::chrono::milliseconds{60}); });

    // Enqueue Low, Normal, High, Critical
    scheduler.schedule_priority(aegis::Priority::Low, [&]() {
        std::lock_guard<std::mutex> lock(mtx);
        execution_order.push_back(1);
    });

    scheduler.schedule_priority(aegis::Priority::Critical, [&]() {
        std::lock_guard<std::mutex> lock(mtx);
        execution_order.push_back(4);
    });

    scheduler.schedule_priority(aegis::Priority::High, [&]() {
        std::lock_guard<std::mutex> lock(mtx);
        execution_order.push_back(3);
    });

    scheduler.schedule_priority(aegis::Priority::Normal, [&]() {
        std::lock_guard<std::mutex> lock(mtx);
        execution_order.push_back(2);
    });

    blocker.get();
    scheduler.wait_all();

    ASSERT_EQ(execution_order.size(), 4);
    // Critical (4) and High (3) must execute before Low (1)
    EXPECT_EQ(execution_order[0], 4);
    EXPECT_EQ(execution_order[1], 3);
}

TEST(SchedulerTest, TaskDependenciesDAG) {
    aegis::Scheduler scheduler(4);

    std::atomic<bool> task1_done{false};
    std::atomic<bool> task2_done{false};
    std::atomic<bool> dependent_saw_prereqs{false};

    auto f1 = scheduler.schedule([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds{40});
        task1_done.store(true);
        return 10;
    });

    auto f2 = scheduler.schedule([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds{20});
        task2_done.store(true);
        return 20;
    });

    // Prerequisite task IDs are 1 and 2
    auto dep_fut = scheduler.schedule_dependent({1, 2}, aegis::Priority::High, [&]() {
        if (task1_done.load() && task2_done.load()) {
            dependent_saw_prereqs.store(true);
        }
        return 999;
    });

    EXPECT_EQ(dep_fut.get(), 999);
    EXPECT_TRUE(dependent_saw_prereqs.load());
}
