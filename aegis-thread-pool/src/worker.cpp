#include "aegis/worker.hpp"

#include <random>

namespace aegis {

Worker::Worker(WorkerId id, TaskQueue& global_queue, MetricsTracker& metrics,
               bool enable_work_stealing)
    : id_(id),
      global_queue_(global_queue),
      metrics_(metrics),
      enable_work_stealing_(enable_work_stealing) {
    if (enable_work_stealing_) {
        local_queue_ = std::make_unique<WorkStealingQueue>();
    }
}

Worker::~Worker() {
    stop();
    join();
}

void Worker::start() {
    thread_ = std::thread(&Worker::run, this);
}

void Worker::stop() {
    stop_requested_.store(true, std::memory_order_release);
    global_queue_.wake_all();
}

void Worker::join() {
    if (thread_.joinable()) {
        thread_.join();
    }
}

void Worker::set_peers(const std::vector<std::shared_ptr<Worker>>& peers) {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    peers_.clear();
    for (const auto& p : peers) {
        if (p.get() != this) {
            peers_.push_back(p);
        }
    }
}

bool Worker::push_local(ExecutableTask task) {
    if (local_queue_) {
        local_queue_->push(std::move(task));
        global_queue_.wake_all();
        return true;
    }
    return false;
}

std::optional<ExecutableTask> Worker::acquire_task() {
    // 1. Try local work-stealing queue if present
    if (local_queue_) {
        if (auto task = local_queue_->pop()) {
            return task;
        }
    }

    // 2. Try global queue (blocking pop with small timeout if work stealing enabled, or standard
    // blocking pop)
    if (enable_work_stealing_) {
        if (auto task = global_queue_.pop_wait_for(std::chrono::milliseconds{10})) {
            return task;
        }

        // 3. Work stealing from peers using thread-safe snapshot
        std::vector<std::weak_ptr<Worker>> peers_snapshot;
        {
            std::lock_guard<std::mutex> lock(peers_mutex_);
            peers_snapshot = peers_;
        }

        if (!peers_snapshot.empty()) {
            thread_local std::mt19937 rng(std::random_device{}());
            std::uniform_int_distribution<size_t> dist(0, peers_snapshot.size() - 1);
            size_t start_idx = dist(rng);

            for (size_t i = 0; i < peers_snapshot.size(); ++i) {
                size_t idx = (start_idx + i) % peers_snapshot.size();
                if (auto peer = peers_snapshot[idx].lock()) {
                    if (auto peer_queue = peer->local_queue()) {
                        if (auto stolen = peer_queue->steal()) {
                            return stolen;
                        }
                    }
                }
            }
        }

        // Re-try global pop if nothing to steal
        if (global_queue_.is_closed() && global_queue_.empty()) {
            if (!local_queue_ || local_queue_->empty()) {
                return std::nullopt;
            }
        }
        return global_queue_.pop_wait_for(std::chrono::milliseconds{50});
    }

    // Standard mode: block until global queue has tasks, closes, or worker stop requested
    return global_queue_.pop(stop_requested_);
}

void Worker::run() {
    while (!stop_requested_.load(std::memory_order_relaxed)) {
        state_.store(WorkerState::Idle, std::memory_order_relaxed);

        auto opt_task = acquire_task();
        if (!opt_task.has_value()) {
            if (stop_requested_.load(std::memory_order_relaxed)) {
                break;
            }
            if (global_queue_.is_closed() && global_queue_.empty()) {
                if (!local_queue_ || local_queue_->empty()) {
                    break;
                }
            }
            continue;
        }

        auto& task = *opt_task;

        if (task.is_cancelled()) {
            // Task was cancelled before execution. Complete its promise and account metric exactly
            // once.
            if (task.metadata && task.metadata->transition_to_cancelled()) {
                metrics_.record_task_cancelled();
            }
            if (task.metadata && task.metadata->cancel_promise_handler) {
                task.metadata->cancel_promise_handler("Task cancelled prior to execution.");
            }
            continue;
        }

        state_.store(WorkerState::Running, std::memory_order_relaxed);
        metrics_.worker_started_task();

        auto now = std::chrono::steady_clock::now();
        std::chrono::nanoseconds wait_time{0};
        if (task.metadata) {
            wait_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
                now - task.metadata->submission_time);
        }

        auto start_exec = std::chrono::steady_clock::now();

        // Execute task
        task.func();

        auto end_exec = std::chrono::steady_clock::now();
        auto exec_time =
            std::chrono::duration_cast<std::chrono::nanoseconds>(end_exec - start_exec);

        metrics_.worker_finished_task();
        tasks_executed_.fetch_add(1, std::memory_order_relaxed);

        if (task.metadata &&
            task.metadata->status.load(std::memory_order_relaxed) == TaskStatus::Failed) {
            metrics_.record_task_failed(wait_time, exec_time);
        } else {
            metrics_.record_task_completed(wait_time, exec_time);
        }
    }

    state_.store(WorkerState::Stopped, std::memory_order_relaxed);
}

}  // namespace aegis
