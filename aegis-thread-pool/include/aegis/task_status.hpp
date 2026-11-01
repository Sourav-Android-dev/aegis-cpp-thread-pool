#pragma once

#include <cstdint>
#include <string_view>

namespace aegis {

/**
 * @brief Represents the lifecycle states of an asynchronous task.
 */
enum class TaskStatus : uint8_t {
    Pending,    ///< Enqueued in the ready queue or delayed timer
    Scheduled,  ///< Picked up by the scheduler or assigned to worker
    Running,    ///< Actively executing on a worker thread
    Completed,  ///< Finished execution normally
    Failed,     ///< Terminated due to an uncaught exception
    Cancelled   ///< Cancelled prior to execution or during shutdown
};

constexpr std::string_view to_string(TaskStatus status) noexcept {
    switch (status) {
        case TaskStatus::Pending:
            return "Pending";
        case TaskStatus::Scheduled:
            return "Scheduled";
        case TaskStatus::Running:
            return "Running";
        case TaskStatus::Completed:
            return "Completed";
        case TaskStatus::Failed:
            return "Failed";
        case TaskStatus::Cancelled:
            return "Cancelled";
    }
    return "Unknown";
}

}  // namespace aegis
