#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string_view>
#include <thread>
#include <vector>

#include "aegis/metrics.hpp"
#include "aegis/task.hpp"
#include "aegis/task_queue.hpp"

namespace aegis {

enum class WorkerState : uint8_t { Starting, Idle, Running, Stopped };

constexpr std::string_view to_string(WorkerState state) noexcept {
    switch (state) {
        case WorkerState::Starting:
            return "Starting";
        case WorkerState::Idle:
            return "Idle";
        case WorkerState::Running:
            return "Running";
        case WorkerState::Stopped:
            return "Stopped";
    }
    return "Unknown";
}

class ThreadPool;

/**
 * @brief Represents a dedicated worker thread in the Aegis engine.
 */
class Worker {
public:
    using WorkerId = uint32_t;

    Worker(WorkerId id, TaskQueue& global_queue, MetricsTracker& metrics,
           bool enable_work_stealing = false);

    ~Worker();

    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;
    Worker(Worker&&) = delete;
    Worker& operator=(Worker&&) = delete;

    void start();
    void stop();
    void join();

    [[nodiscard]] WorkerId id() const noexcept { return id_; }
    [[nodiscard]] WorkerState state() const noexcept {
        return state_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] bool is_idle() const noexcept {
        return state_.load(std::memory_order_relaxed) == WorkerState::Idle;
    }
    [[nodiscard]] uint64_t tasks_executed() const noexcept {
        return tasks_executed_.load(std::memory_order_relaxed);
    }

    void set_peers(const std::vector<std::shared_ptr<Worker>>& peers);
    WorkStealingQueue* local_queue() noexcept { return local_queue_.get(); }
    bool push_local(ExecutableTask task);

private:
    void run();
    std::optional<ExecutableTask> acquire_task();

    const WorkerId id_;
    TaskQueue& global_queue_;
    MetricsTracker& metrics_;
    const bool enable_work_stealing_;

    std::atomic<WorkerState> state_{WorkerState::Starting};
    std::atomic<bool> stop_requested_{false};
    std::atomic<uint64_t> tasks_executed_{0};

    std::unique_ptr<WorkStealingQueue> local_queue_;
    mutable std::mutex peers_mutex_;
    std::vector<std::weak_ptr<Worker>> peers_;

    std::thread thread_;
};

}  // namespace aegis
