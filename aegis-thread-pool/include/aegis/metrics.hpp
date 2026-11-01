#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>

namespace aegis {

/**
 * @brief Immutable point-in-time snapshot of engine performance metrics.
 */
struct MetricsSnapshot {
    uint64_t submitted_tasks{0};
    uint64_t completed_tasks{0};
    uint64_t failed_tasks{0};
    uint64_t cancelled_tasks{0};
    uint64_t pending_tasks{0};

    uint32_t total_workers{0};
    uint32_t active_workers{0};
    uint32_t idle_workers{0};

    double avg_execution_time_ms{0.0};
    double max_execution_time_ms{0.0};
    double avg_wait_time_ms{0.0};
    double tasks_per_second{0.0};
    double worker_utilization_pct{0.0};

    std::chrono::steady_clock::time_point timestamp{std::chrono::steady_clock::now()};

    [[nodiscard]] std::string to_string() const;
};

/**
 * @brief Thread-safe performance and telemetry tracker.
 */
class MetricsTracker {
public:
    MetricsTracker();
    ~MetricsTracker() = default;

    // Task lifecycle recording
    void record_task_submitted() noexcept;
    void record_task_cancelled() noexcept;
    void record_task_completed(std::chrono::nanoseconds wait_time,
                               std::chrono::nanoseconds exec_time) noexcept;
    void record_task_failed(std::chrono::nanoseconds wait_time,
                            std::chrono::nanoseconds exec_time) noexcept;

    // Worker state tracking
    void set_worker_counts(uint32_t total) noexcept;
    void worker_started_task() noexcept;
    void worker_finished_task() noexcept;

    // Metric queries
    [[nodiscard]] MetricsSnapshot snapshot() const;
    void reset();

private:
    std::atomic<uint64_t> submitted_tasks_{0};
    std::atomic<uint64_t> completed_tasks_{0};
    std::atomic<uint64_t> failed_tasks_{0};
    std::atomic<uint64_t> cancelled_tasks_{0};

    std::atomic<uint32_t> total_workers_{0};
    std::atomic<uint32_t> active_workers_{0};

    std::atomic<uint64_t> total_exec_time_ns_{0};
    std::atomic<uint64_t> max_exec_time_ns_{0};
    std::atomic<uint64_t> total_wait_time_ns_{0};

    std::chrono::steady_clock::time_point start_time_;
};

}  // namespace aegis
