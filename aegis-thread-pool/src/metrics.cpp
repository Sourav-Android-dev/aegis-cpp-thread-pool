#include "aegis/metrics.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace aegis {

MetricsTracker::MetricsTracker() : start_time_(std::chrono::steady_clock::now()) {}

void MetricsTracker::record_task_submitted() noexcept {
    submitted_tasks_.fetch_add(1, std::memory_order_relaxed);
}

void MetricsTracker::record_task_cancelled() noexcept {
    cancelled_tasks_.fetch_add(1, std::memory_order_relaxed);
}

void MetricsTracker::record_task_completed(std::chrono::nanoseconds wait_time,
                                           std::chrono::nanoseconds exec_time) noexcept {
    completed_tasks_.fetch_add(1, std::memory_order_relaxed);
    total_wait_time_ns_.fetch_add(wait_time.count(), std::memory_order_relaxed);
    total_exec_time_ns_.fetch_add(exec_time.count(), std::memory_order_relaxed);

    uint64_t current_max = max_exec_time_ns_.load(std::memory_order_relaxed);
    uint64_t exec_ns = exec_time.count();
    while (exec_ns > current_max &&
           !max_exec_time_ns_.compare_exchange_weak(current_max, exec_ns, std::memory_order_relaxed,
                                                    std::memory_order_relaxed)) {
        // Retry loop until updated or superseded
    }
}

void MetricsTracker::record_task_failed(std::chrono::nanoseconds wait_time,
                                        std::chrono::nanoseconds exec_time) noexcept {
    failed_tasks_.fetch_add(1, std::memory_order_relaxed);
    total_wait_time_ns_.fetch_add(wait_time.count(), std::memory_order_relaxed);
    total_exec_time_ns_.fetch_add(exec_time.count(), std::memory_order_relaxed);
}

void MetricsTracker::set_worker_counts(uint32_t total) noexcept {
    total_workers_.store(total, std::memory_order_relaxed);
}

void MetricsTracker::worker_started_task() noexcept {
    active_workers_.fetch_add(1, std::memory_order_relaxed);
}

void MetricsTracker::worker_finished_task() noexcept {
    active_workers_.fetch_sub(1, std::memory_order_relaxed);
}

MetricsSnapshot MetricsTracker::snapshot() const {
    MetricsSnapshot s;
    s.submitted_tasks = submitted_tasks_.load(std::memory_order_relaxed);
    s.completed_tasks = completed_tasks_.load(std::memory_order_relaxed);
    s.failed_tasks = failed_tasks_.load(std::memory_order_relaxed);
    s.cancelled_tasks = cancelled_tasks_.load(std::memory_order_relaxed);

    uint64_t finished = s.completed_tasks + s.failed_tasks + s.cancelled_tasks;
    s.pending_tasks = (s.submitted_tasks > finished) ? (s.submitted_tasks - finished) : 0;

    s.total_workers = total_workers_.load(std::memory_order_relaxed);
    s.active_workers = active_workers_.load(std::memory_order_relaxed);
    s.idle_workers =
        (s.total_workers > s.active_workers) ? (s.total_workers - s.active_workers) : 0;

    uint64_t executed_count = s.completed_tasks + s.failed_tasks;
    if (executed_count > 0) {
        uint64_t tot_exec = total_exec_time_ns_.load(std::memory_order_relaxed);
        uint64_t tot_wait = total_wait_time_ns_.load(std::memory_order_relaxed);

        s.avg_execution_time_ms =
            (static_cast<double>(tot_exec) / static_cast<double>(executed_count)) / 1'000'000.0;
        s.avg_wait_time_ms =
            (static_cast<double>(tot_wait) / static_cast<double>(executed_count)) / 1'000'000.0;
    }
    s.max_execution_time_ms =
        static_cast<double>(max_exec_time_ns_.load(std::memory_order_relaxed)) / 1'000'000.0;

    auto now = std::chrono::steady_clock::now();
    double elapsed_sec = std::chrono::duration<double>(now - start_time_).count();
    if (elapsed_sec > 0.0001) {
        s.tasks_per_second = static_cast<double>(executed_count) / elapsed_sec;
    }

    if (s.total_workers > 0) {
        s.worker_utilization_pct =
            (static_cast<double>(s.active_workers) / s.total_workers) * 100.0;
    }

    s.timestamp = now;
    return s;
}

void MetricsTracker::reset() {
    submitted_tasks_.store(0, std::memory_order_relaxed);
    completed_tasks_.store(0, std::memory_order_relaxed);
    failed_tasks_.store(0, std::memory_order_relaxed);
    cancelled_tasks_.store(0, std::memory_order_relaxed);
    active_workers_.store(0, std::memory_order_relaxed);
    total_exec_time_ns_.store(0, std::memory_order_relaxed);
    max_exec_time_ns_.store(0, std::memory_order_relaxed);
    total_wait_time_ns_.store(0, std::memory_order_relaxed);
    start_time_ = std::chrono::steady_clock::now();
}

std::string MetricsSnapshot::to_string() const {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);
    ss << "=== Aegis Metrics Snapshot ===\n"
       << "  Tasks Submitted:   " << submitted_tasks << "\n"
       << "  Tasks Completed:   " << completed_tasks << "\n"
       << "  Tasks Failed:      " << failed_tasks << "\n"
       << "  Tasks Cancelled:   " << cancelled_tasks << "\n"
       << "  Tasks Pending:     " << pending_tasks << "\n"
       << "  Workers (Tot/Act): " << total_workers << " / " << active_workers
       << " (Idle: " << idle_workers << ")\n"
       << "  Avg Exec Time:     " << avg_execution_time_ms << " ms\n"
       << "  Max Exec Time:     " << max_execution_time_ms << " ms\n"
       << "  Avg Queue Wait:    " << avg_wait_time_ms << " ms\n"
       << "  Throughput:        " << tasks_per_second << " tasks/sec\n"
       << "  Worker Utilization:" << worker_utilization_pct << " %\n"
       << "==============================";
    return ss.str();
}

}  // namespace aegis
