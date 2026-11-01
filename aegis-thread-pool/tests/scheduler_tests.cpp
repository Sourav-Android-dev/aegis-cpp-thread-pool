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

TEST(SchedulerTest, TaskDependenciesDAG_PrerequisiteFailedPropagates) {
    // Verifies Issue 5: When prerequisite task fails, dependent task receives exception and
    // wait_all() does not hang
    aegis::Scheduler scheduler(2);

    auto f_fail = scheduler.schedule(
        []() -> int { throw std::runtime_error("Simulated prerequisite failure"); });

    // Prerequisite task 1 will fail
    auto dep_fut = scheduler.schedule_dependent({1}, aegis::Priority::High, []() { return 42; });

    EXPECT_THROW(f_fail.get(), std::runtime_error);
    EXPECT_THROW(dep_fut.get(), std::runtime_error);

    // wait_all() must terminate quickly without hanging
    scheduler.wait_all();
}

TEST(SchedulerTest, TaskDependenciesDAG_PrerequisiteCancelledPropagates) {
    // Verifies Issue 5: When prerequisite is cancelled, dependent task receives
    // TaskCancelledException
    aegis::ThreadPoolConfig config;
    config.worker_count = 1;
    aegis::Scheduler scheduler(config);

    // Blocker to let us cancel task 2
    auto blocker =
        scheduler.schedule([]() { std::this_thread::sleep_for(std::chrono::milliseconds{40}); });

    auto f_to_cancel = scheduler.schedule([]() { return 100; });
    EXPECT_TRUE(scheduler.cancel(2));

    auto dep_fut = scheduler.schedule_dependent({2}, aegis::Priority::High, []() { return 200; });

    blocker.get();
    EXPECT_THROW(f_to_cancel.get(), aegis::TaskCancelledException);
    EXPECT_THROW(dep_fut.get(), aegis::TaskCancelledException);

    scheduler.wait_all();
}

TEST(SchedulerTest, TaskDependenciesDAG_InvalidDependencyIdFails) {
    // Verifies Issue 5: Invalid prerequisite task ID fails fast and doesn't hang wait_all()
    aegis::Scheduler scheduler(2);

    auto dep_fut =
        scheduler.schedule_dependent({99999}, aegis::Priority::Normal, []() { return 1; });

    EXPECT_THROW(dep_fut.get(), std::runtime_error);
    scheduler.wait_all();
}
