#pragma once

#include <cstdint>
#include <string_view>

namespace aegis {

/**
 * @brief Priority levels for tasks submitted to the Aegis scheduler.
 * Higher numerical value corresponds to higher execution priority.
 */
enum class Priority : uint8_t { Low = 0, Normal = 1, High = 2, Critical = 3 };

constexpr std::string_view to_string(Priority priority) noexcept {
    switch (priority) {
        case Priority::Low:
            return "Low";
        case Priority::Normal:
            return "Normal";
        case Priority::High:
            return "High";
        case Priority::Critical:
            return "Critical";
    }
    return "Unknown";
}

}  // namespace aegis
