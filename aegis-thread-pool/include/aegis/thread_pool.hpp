#pragma once

#include <chrono>
#include <concepts>
#include <future>
#include <memory>
#include <shared_mutex>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "aegis/config.hpp"
#include "aegis/logger.hpp"
#include "aegis/metrics.hpp"
#include "aegis/task.hpp"
#include "aegis/task_queue.hpp"
#include "aegis/worker.hpp"

namespace aegis {

class ThreadPool {
public:
    explicit ThreadPool(ThreadPoolConfig config = ThreadPoolConfig{});
    explicit ThreadPool(size_t worker_count);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    /**
     * @brief Submit a generic callable with arguments to the pool.
     * @return std::future holding result or propagating uncaught exception.
     */
    template <typename F, typename... Args>
    requires std::invocable<F, Args...>
    auto submit(F&& f, Args&&... args) {
        return submit_priority(Priority::Normal, std::forward<F>(f), std::forward<Args>(args)...);
    }

    /**
     * @brief Submit a callable with specific priority.
     */
    template <typename F, typename... Args>
    requires std::invocable<F, Args...>
    auto submit_priority(Priority priority, F&& f, Args&&... args) {
        return submit_internal("", priority, std::chrono::steady_clock::now(), std::forward<F>(f),
                               std::forward<Args>(args)...);
    }

    /**
     * @brief Submit a task with human-readable name and priority.
     */
    template <typename F, typename... Args>
    requires std::invocable<F, Args...>
    auto submit_named(std::string name, Priority priority, F&& f, Args&&... args) {
        return submit_internal(std::move(name), priority, std::chrono::steady_clock::now(),
                               std::forward<F>(f), std::forward<Args>(args)...);
    }

    /**
     * @brief Schedule a task for deferred execution after a given duration.
     */
    template <typename F, typename... Args>
    requires std::invocable<F, Args...>
    auto submit_delayed(std::chrono::milliseconds delay, Priority priority, F&& f, Args&&... args) {
        auto due_time = std::chrono::steady_clock::now() + delay;
        return submit_internal("", priority, due_time, std::forward<F>(f),
                               std::forward<Args>(args)...);
    }

    /**
     * @brief Submit a batch of tasks returning futures for all results.
     */
    template <typename F>
    requires std::invocable<F>
    auto submit_batch(std::vector<F> tasks, Priority priority = Priority::Normal) {
        using ReturnType = std::invoke_result_t<F>;
        std::vector<std::future<ReturnType>> futures;
        futures.reserve(tasks.size());

        for (auto& task : tasks) {
            futures.push_back(submit_priority(priority, std::move(task)));
        }
        return futures;
    }

    /**
     * @brief Attempt to cancel a task by ID if it has not started executing yet.
     */
    bool cancel(TaskId id);

    /**
     * @brief Query current status of a task by ID.
     */
    std::optional<TaskStatus> get_task_status(TaskId id) const;

    /**
     * @brief Initiate shutdown according to configured policy.
     */
    void shutdown();

    /**
     * @brief Immediately drop all unstarted tasks and stop worker threads.
     */
    void shutdown_immediate();

    /**
     * @brief Dynamically adjust worker thread count.
     */
    void resize(size_t new_worker_count);

    /**
     * @brief Blocks calling thread until all pending and currently executing tasks complete.
     */
    void wait_for_all();

    [[nodiscard]] bool is_running() const noexcept;
    [[nodiscard]] size_t worker_count() const noexcept;
    [[nodiscard]] size_t active_worker_count() const noexcept;
    [[nodiscard]] size_t idle_worker_count() const noexcept;
    [[nodiscard]] size_t pending_task_count() const noexcept;
    [[nodiscard]] MetricsSnapshot metrics() const;
    [[nodiscard]] const ThreadPoolConfig& config() const noexcept { return config_; }

private:
    template <typename F, typename... Args>
    auto submit_internal(std::string name, Priority priority,
                         std::chrono::steady_clock::time_point due_time, F&& f, Args&&... args) {
        using ReturnType = std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>;

        if (!is_running_.load(std::memory_order_relaxed)) {
            throw std::runtime_error("Cannot submit task to stopped or shutting down ThreadPool.");
        }

        auto bound_task = [fn = std::forward<F>(f),
                           ... captured_args = std::forward<Args>(args)]() mutable -> ReturnType {
            return std::invoke(std::move(fn), std::move(captured_args)...);
        };

        auto task_promise = std::make_shared<std::promise<ReturnType>>();
        std::future<ReturnType> future = task_promise->get_future();

        TaskId tid = next_task_id_.fetch_add(1, std::memory_order_relaxed);
        auto meta = std::make_shared<TaskMetadata>();
        meta->id = tid;
        meta->name = name.empty() ? ("Task-" + std::to_string(tid)) : std::move(name);
        meta->priority = priority;
        meta->scheduled_time = due_time;

        auto promise_completed = std::make_shared<std::atomic<bool>>(false);
        meta->cancel_promise_handler = [t_prom = task_promise,
                                        promise_completed](const std::string& reason) {
            bool expected = false;
            if (promise_completed->compare_exchange_strong(expected, true)) {
                t_prom->set_exception(std::make_exception_ptr(TaskCancelledException(reason)));
            }
        };

        {
            std::unique_lock<std::shared_mutex> lock(tasks_map_mutex_);
            tasks_metadata_[tid] = meta;
        }

        MoveOnlyFunction wrapped_func = [t_prom = task_promise, b_task = std::move(bound_task),
                                         metadata = meta, promise_completed]() mutable {
            if (metadata->cancellation_flag->load(std::memory_order_relaxed)) {
                metadata->transition_to_cancelled();
                bool expected = false;
                if (promise_completed->compare_exchange_strong(expected, true)) {
                    t_prom->set_exception(std::make_exception_ptr(
                        TaskCancelledException("Task was cancelled prior to execution.")));
                }
                return;
            }

            metadata->status.store(TaskStatus::Running, std::memory_order_relaxed);
            metadata->start_time = std::chrono::steady_clock::now();

            try {
                if constexpr (std::is_void_v<ReturnType>) {
                    b_task();
                    bool expected = false;
                    if (promise_completed->compare_exchange_strong(expected, true)) {
                        t_prom->set_value();
                    }
                } else {
                    auto res = b_task();
                    bool expected = false;
                    if (promise_completed->compare_exchange_strong(expected, true)) {
                        t_prom->set_value(std::move(res));
                    }
                }
                metadata->status.store(TaskStatus::Completed, std::memory_order_relaxed);
            } catch (...) {
                metadata->status.store(TaskStatus::Failed, std::memory_order_relaxed);
                bool expected = false;
                if (promise_completed->compare_exchange_strong(expected, true)) {
                    t_prom->set_exception(std::current_exception());
                }
            }

            metadata->end_time = std::chrono::steady_clock::now();
        };

        ExecutableTask exec_task(tid, meta, std::move(wrapped_func), due_time);
        metrics_.record_task_submitted();

        bool accepted = false;
        auto now = std::chrono::steady_clock::now();

        // If work-stealing is enabled and task is not delayed, distribute directly to worker local
        // queue
        if (config_.enable_work_stealing && due_time <= now) {
            std::shared_ptr<Worker> target_worker;
            {
                std::lock_guard<std::mutex> lock(workers_mutex_);
                if (!workers_.empty()) {
                    size_t idx =
                        next_worker_idx_.fetch_add(1, std::memory_order_relaxed) % workers_.size();
                    target_worker = workers_[idx];
                }
            }
            if (target_worker) {
                accepted = target_worker->push_local(std::move(exec_task));
            }
        }

        if (!accepted) {
            accepted = queue_.push(std::move(exec_task));
        }

        if (!accepted) {
            if (meta->transition_to_cancelled()) {
                metrics_.record_task_cancelled();
            }
            if (meta->cancel_promise_handler) {
                meta->cancel_promise_handler("ThreadPool queue rejected task submission.");
            }
            throw std::runtime_error("ThreadPool queue rejected task submission.");
        }

        return future;
    }

    void initialize_workers(size_t count);
    void prune_finished_metadata();

    ThreadPoolConfig config_;
    TaskQueue queue_;
    MetricsTracker metrics_;

    std::atomic<bool> is_running_{true};
    std::atomic<TaskId> next_task_id_{1};
    std::atomic<size_t> next_worker_idx_{0};

    mutable std::mutex workers_mutex_;
    std::vector<std::shared_ptr<Worker>> workers_;

    mutable std::shared_mutex tasks_map_mutex_;
    std::unordered_map<TaskId, std::shared_ptr<TaskMetadata>> tasks_metadata_;
};

}  // namespace aegis
