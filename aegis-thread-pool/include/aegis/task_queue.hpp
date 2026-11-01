#pragma once

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <vector>

#include "aegis/config.hpp"
#include "aegis/task.hpp"

namespace aegis {

class QueueOverflowException : public std::runtime_error {
public:
    QueueOverflowException()
        : std::runtime_error(
              "Aegis TaskQueue is at maximum capacity and overload policy is Reject.") {}
};

/**
 * @brief Thread-safe, bounded, multi-priority task queue with starvation mitigation
 * and delayed task support.
 */
class TaskQueue {
public:
    explicit TaskQueue(
        size_t max_capacity = 0, OverloadPolicy overload_policy = OverloadPolicy::Block,
        std::chrono::milliseconds starvation_aging_threshold = std::chrono::milliseconds{200});
    ~TaskQueue();

    TaskQueue(const TaskQueue&) = delete;
    TaskQueue& operator=(const TaskQueue&) = delete;

    /**
     * @brief Enqueue a task into the queue.
     * @return true if accepted, false if dropped or closed.
     */
    bool push(ExecutableTask task);

    /**
     * @brief Pop the next highest-priority ready task.
     * Blocks if empty until a task is available or queue is closed.
     * @return Dispatched task or std::nullopt if queue is closed and empty.
     */
    std::optional<ExecutableTask> pop();

    /**
     * @brief Pop with stop_token interruption. Unblocks immediately if stop_token is set.
     */
    std::optional<ExecutableTask> pop(const std::atomic<bool>& stop_token);

    /**
     * @brief Try to pop a ready task without blocking.
     */
    std::optional<ExecutableTask> try_pop();

    /**
     * @brief Pop with timeout.
     */
    std::optional<ExecutableTask> pop_wait_for(std::chrono::milliseconds timeout);

    /**
     * @brief Wake all waiting threads on not_empty_cv_ without closing the queue.
     */
    void wake_all();

    /**
     * @brief Close the queue, unblocking waiting workers.
     * @param discard_pending If true, all pending tasks are cleared immediately.
     */
    void close(bool discard_pending = false);

    [[nodiscard]] bool is_closed() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] size_t size() const noexcept;
    [[nodiscard]] size_t capacity() const noexcept;

    /**
     * @brief Clear all pending tasks and mark them cancelled.
     */
    std::vector<ExecutableTask> drain();

private:
    void transfer_due_delayed_tasks_locked(std::chrono::steady_clock::time_point now);
    bool check_and_promote_aged_task_locked(std::chrono::steady_clock::time_point now,
                                            ExecutableTask& out_task);
    std::optional<ExecutableTask> select_next_task_locked();

    const size_t max_capacity_;
    const OverloadPolicy overload_policy_;
    const std::chrono::milliseconds aging_threshold_;

    mutable std::mutex mutex_;
    std::condition_variable not_empty_cv_;
    std::condition_variable not_full_cv_;

    bool closed_{false};

    // Priority buckets: 0 = Low, 1 = Normal, 2 = High, 3 = Critical
    std::array<std::deque<ExecutableTask>, 4> priority_buckets_;

    // Delayed tasks min-heap or sorted vector (sorted by due_time ascending)
    std::vector<ExecutableTask> delayed_queue_;

    size_t ready_count_{0};
};

/**
 * @brief Thread-safe work-stealing deque for fine-grained per-worker task distribution.
 * Owner pushes/pops from the back (LIFO cache locality), thieves steal from the front (FIFO).
 */
class WorkStealingQueue {
public:
    WorkStealingQueue() = default;
    ~WorkStealingQueue() = default;

    WorkStealingQueue(const WorkStealingQueue&) = delete;
    WorkStealingQueue& operator=(const WorkStealingQueue&) = delete;

    void push(ExecutableTask task);
    std::optional<ExecutableTask> pop();
    std::optional<ExecutableTask> steal();

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] size_t size() const noexcept;

private:
    mutable std::mutex mutex_;
    std::deque<ExecutableTask> deque_;
};

}  // namespace aegis
