#pragma once

#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "aegis/thread_pool.hpp"

namespace aegis {

/**
 * @brief Advanced Task Scheduler providing dependency DAG execution,
 * delayed task dispatch, recurring intervals, and fine-grained task tracking.
 */
class Scheduler {
public:
    explicit Scheduler(ThreadPoolConfig config = ThreadPoolConfig{});
    explicit Scheduler(size_t worker_count);
    ~Scheduler();

    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;

    /**
     * @brief Schedule an immediate task with default priority.
     */
    template <typename F, typename... Args>
    requires std::invocable<F, Args...>
    auto schedule(F&& f, Args&&... args) {
        return pool_.submit(std::forward<F>(f), std::forward<Args>(args)...);
    }

    /**
     * @brief Schedule a task with explicit priority.
     */
    template <typename F, typename... Args>
    requires std::invocable<F, Args...>
    auto schedule_priority(Priority priority, F&& f, Args&&... args) {
        return pool_.submit_priority(priority, std::forward<F>(f), std::forward<Args>(args)...);
    }

    /**
     * @brief Schedule a task after a relative delay.
     */
    template <typename F, typename... Args>
    requires std::invocable<F, Args...>
    auto schedule_after(std::chrono::milliseconds delay, Priority priority, F&& f, Args&&... args) {
        return pool_.submit_delayed(delay, priority, std::forward<F>(f),
                                    std::forward<Args>(args)...);
    }

    /**
     * @brief Schedule a task with prerequisites (DAG dependencies).
     * The task will only execute after all specified dependency task IDs have succeeded.
     */
    template <typename F>
    requires std::invocable<F>
    auto schedule_dependent(std::vector<TaskId> depends_on, Priority priority, F&& f) {
        using ReturnType = std::invoke_result_t<F>;
        auto prom = std::make_shared<std::promise<ReturnType>>();
        auto future = prom->get_future();

        auto action = [p = prom, fn = std::forward<F>(f)]() mutable {
            try {
                if constexpr (std::is_void_v<ReturnType>) {
                    fn();
                    p->set_value();
                } else {
                    p->set_value(fn());
                }
            } catch (...) {
                p->set_exception(std::current_exception());
            }
        };

        auto fail_action = [p = prom](std::exception_ptr ex) {
            try {
                p->set_exception(ex);
            } catch (...) {
            }
        };

        register_dependent_task(std::move(depends_on), priority,
                                MoveOnlyFunction(std::move(action)), std::move(fail_action));
        return future;
    }

    /**
     * @brief Cancel a task by ID.
     */
    bool cancel(TaskId id);

    /**
     * @brief Query task status.
     */
    std::optional<TaskStatus> get_status(TaskId id) const;

    /**
     * @brief Block until all tasks finish.
     */
    void wait_all();

    /**
     * @brief Gracefully shut down scheduler and internal thread pool.
     */
    void shutdown();

    [[nodiscard]] MetricsSnapshot metrics() const { return pool_.metrics(); }
    [[nodiscard]] ThreadPool& pool() noexcept { return pool_; }

private:
    struct DependentTaskNode {
        std::vector<TaskId> remaining_dependencies;
        Priority priority;
        MoveOnlyFunction action;
        std::function<void(std::exception_ptr)> on_dependency_failed;
    };

    void register_dependent_task(std::vector<TaskId> dependencies, Priority priority,
                                 MoveOnlyFunction action,
                                 std::function<void(std::exception_ptr)> on_failure);
    void check_dependent_tasks();

    ThreadPool pool_;

    mutable std::mutex dep_mutex_;
    std::vector<DependentTaskNode> pending_dependents_;
    std::atomic<bool> monitor_running_{true};
    std::thread dep_monitor_thread_;
};

}  // namespace aegis
