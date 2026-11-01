#include "aegis/task_queue.hpp"

#include <algorithm>

namespace aegis {

TaskQueue::TaskQueue(size_t max_capacity, OverloadPolicy overload_policy,
                     std::chrono::milliseconds starvation_aging_threshold)
    : max_capacity_(max_capacity),
      overload_policy_(overload_policy),
      aging_threshold_(starvation_aging_threshold) {}

TaskQueue::~TaskQueue() {
    close(true);
}

void TaskQueue::transfer_due_delayed_tasks_locked(std::chrono::steady_clock::time_point now) {
    if (delayed_queue_.empty())
        return;

    auto it = std::remove_if(
        delayed_queue_.begin(), delayed_queue_.end(), [this, now](ExecutableTask& task) {
            if (task.due_time <= now) {
                uint8_t p = 1;
                if (task.metadata) {
                    p = std::min<uint8_t>(3, static_cast<uint8_t>(task.metadata->priority));
                }
                priority_buckets_[p].push_back(std::move(task));
                ++ready_count_;
                return true;
            }
            return false;
        });
    delayed_queue_.erase(it, delayed_queue_.end());
}

bool TaskQueue::check_and_promote_aged_task_locked(std::chrono::steady_clock::time_point now,
                                                   ExecutableTask& out_task) {
    if (aging_threshold_.count() <= 0)
        return false;

    // Check buckets from Low to High (0 to 2) for starvation
    for (size_t b = 0; b < 3; ++b) {
        auto& bucket = priority_buckets_[b];
        if (!bucket.empty()) {
            const auto& front = bucket.front();
            if (front.metadata) {
                auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - front.metadata->submission_time);
                if (waited >= aging_threshold_) {
                    out_task = std::move(bucket.front());
                    bucket.pop_front();
                    --ready_count_;
                    return true;
                }
            }
        }
    }
    return false;
}

std::optional<ExecutableTask> TaskQueue::select_next_task_locked() {
    auto now = std::chrono::steady_clock::now();
    transfer_due_delayed_tasks_locked(now);

    if (ready_count_ == 0) {
        return std::nullopt;
    }

    // Check starvation mitigation first
    ExecutableTask aged_task;
    if (check_and_promote_aged_task_locked(now, aged_task)) {
        return aged_task;
    }

    // Pop from highest priority non-empty bucket: Critical(3) down to Low(0)
    for (int b = 3; b >= 0; --b) {
        auto& bucket = priority_buckets_[static_cast<size_t>(b)];
        if (!bucket.empty()) {
            ExecutableTask task = std::move(bucket.front());
            bucket.pop_front();
            --ready_count_;
            return task;
        }
    }

    return std::nullopt;
}

bool TaskQueue::push(ExecutableTask task) {
    std::unique_lock<std::mutex> lock(mutex_);

    if (closed_) {
        return false;
    }

    auto now = std::chrono::steady_clock::now();
    transfer_due_delayed_tasks_locked(now);

    size_t current_total = ready_count_ + delayed_queue_.size();
    if (max_capacity_ > 0 && current_total >= max_capacity_) {
        switch (overload_policy_) {
            case OverloadPolicy::Block:
                not_full_cv_.wait(lock, [this]() {
                    return closed_ || (max_capacity_ == 0) ||
                           (ready_count_ + delayed_queue_.size() < max_capacity_);
                });
                if (closed_)
                    return false;
                break;

            case OverloadPolicy::Reject:
                throw QueueOverflowException();

            case OverloadPolicy::DiscardOldest: {
                // Discard oldest task from lowest priority bucket
                bool discarded = false;
                for (size_t b = 0; b < 4; ++b) {
                    if (!priority_buckets_[b].empty()) {
                        if (priority_buckets_[b].front().metadata) {
                            priority_buckets_[b].front().metadata->status.store(
                                TaskStatus::Cancelled, std::memory_order_relaxed);
                        }
                        priority_buckets_[b].pop_front();
                        --ready_count_;
                        discarded = true;
                        break;
                    }
                }
                if (!discarded && !delayed_queue_.empty()) {
                    if (delayed_queue_.front().metadata) {
                        delayed_queue_.front().metadata->status.store(TaskStatus::Cancelled,
                                                                      std::memory_order_relaxed);
                    }
                    delayed_queue_.erase(delayed_queue_.begin());
                }
                break;
            }
        }
    }

    if (task.due_time > now) {
        delayed_queue_.push_back(std::move(task));
    } else {
        uint8_t p = 1;  // Default Normal
        if (task.metadata) {
            p = std::min<uint8_t>(3, static_cast<uint8_t>(task.metadata->priority));
        }
        priority_buckets_[p].push_back(std::move(task));
        ++ready_count_;
    }

    not_empty_cv_.notify_one();
    return true;
}

std::optional<ExecutableTask> TaskQueue::pop() {
    std::unique_lock<std::mutex> lock(mutex_);

    while (true) {
        if (auto task = select_next_task_locked()) {
            not_full_cv_.notify_one();
            return task;
        }

        if (closed_) {
            return std::nullopt;
        }

        if (!delayed_queue_.empty()) {
            // Find minimum due_time
            auto min_it = std::min_element(delayed_queue_.begin(), delayed_queue_.end(),
                                           [](const ExecutableTask& a, const ExecutableTask& b) {
                                               return a.due_time < b.due_time;
                                           });
            auto next_due = min_it->due_time;
            auto now = std::chrono::steady_clock::now();
            if (next_due > now) {
                not_empty_cv_.wait_until(lock, next_due);
            }
        } else {
            not_empty_cv_.wait(
                lock, [this]() { return closed_ || ready_count_ > 0 || !delayed_queue_.empty(); });
        }
    }
}

std::optional<ExecutableTask> TaskQueue::try_pop() {
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) {
        return std::nullopt;
    }
    auto task = select_next_task_locked();
    if (task) {
        not_full_cv_.notify_one();
    }
    return task;
}

std::optional<ExecutableTask> TaskQueue::pop_wait_for(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    auto deadline = std::chrono::steady_clock::now() + timeout;

    while (true) {
        if (auto task = select_next_task_locked()) {
            not_full_cv_.notify_one();
            return task;
        }

        if (closed_) {
            return std::nullopt;
        }

        auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            return std::nullopt;
        }

        auto wait_limit = deadline;
        if (!delayed_queue_.empty()) {
            auto min_it = std::min_element(delayed_queue_.begin(), delayed_queue_.end(),
                                           [](const ExecutableTask& a, const ExecutableTask& b) {
                                               return a.due_time < b.due_time;
                                           });
            if (min_it->due_time < wait_limit) {
                wait_limit = min_it->due_time;
            }
        }

        not_empty_cv_.wait_until(lock, wait_limit);
    }
}

void TaskQueue::close(bool discard_pending) {
    std::vector<ExecutableTask> discarded;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        if (discard_pending) {
            for (auto& bucket : priority_buckets_) {
                while (!bucket.empty()) {
                    discarded.push_back(std::move(bucket.front()));
                    bucket.pop_front();
                }
            }
            for (auto& task : delayed_queue_) {
                discarded.push_back(std::move(task));
            }
            delayed_queue_.clear();
            ready_count_ = 0;
        }
    }
    not_empty_cv_.notify_all();
    not_full_cv_.notify_all();

    for (auto& task : discarded) {
        if (task.metadata) {
            task.metadata->status.store(TaskStatus::Cancelled, std::memory_order_relaxed);
            if (task.metadata->cancellation_flag) {
                task.metadata->cancellation_flag->store(true, std::memory_order_relaxed);
            }
        }
    }
}

bool TaskQueue::is_closed() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return closed_;
}

bool TaskQueue::empty() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return ready_count_ == 0 && delayed_queue_.empty();
}

size_t TaskQueue::size() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return ready_count_ + delayed_queue_.size();
}

size_t TaskQueue::capacity() const noexcept {
    return max_capacity_;
}

std::vector<ExecutableTask> TaskQueue::drain() {
    std::vector<ExecutableTask> tasks;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& bucket : priority_buckets_) {
            while (!bucket.empty()) {
                tasks.push_back(std::move(bucket.front()));
                bucket.pop_front();
            }
        }
        for (auto& task : delayed_queue_) {
            tasks.push_back(std::move(task));
        }
        delayed_queue_.clear();
        ready_count_ = 0;
    }
    not_full_cv_.notify_all();
    return tasks;
}

// WorkStealingQueue Implementation
void WorkStealingQueue::push(ExecutableTask task) {
    std::lock_guard<std::mutex> lock(mutex_);
    deque_.push_back(std::move(task));
}

std::optional<ExecutableTask> WorkStealingQueue::pop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (deque_.empty()) {
        return std::nullopt;
    }
    ExecutableTask task = std::move(deque_.back());
    deque_.pop_back();
    return task;
}

std::optional<ExecutableTask> WorkStealingQueue::steal() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (deque_.empty()) {
        return std::nullopt;
    }
    ExecutableTask task = std::move(deque_.front());
    deque_.pop_front();
    return task;
}

bool WorkStealingQueue::empty() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return deque_.empty();
}

size_t WorkStealingQueue::size() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return deque_.size();
}

}  // namespace aegis
