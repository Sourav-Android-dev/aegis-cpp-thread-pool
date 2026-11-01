#include <chrono>
#include <iostream>

#include "aegis/scheduler.hpp"

int main() {
    std::cout << "=== Aegis Advanced Task Scheduler: Dependency DAG Example ===\n";

    aegis::Scheduler scheduler(4);

    // Prerequisite Task 1: Fetch Database Records
    auto f1 = scheduler.schedule([]() {
        std::cout << "[Step 1] Loading user records from storage...\n";
        std::this_thread::sleep_for(std::chrono::milliseconds{80});
        return 120;  // 120 users
    });

    // Prerequisite Task 2: Fetch Configuration
    auto f2 = scheduler.schedule([]() {
        std::cout << "[Step 2] Validating security tokens & configurations...\n";
        std::this_thread::sleep_for(std::chrono::milliseconds{40});
        return true;
    });

    // Wait for task 1 & 2
    int user_count = f1.get();
    bool config_ok = f2.get();

    // Dependent Task: Generate Report once prerequisite tasks complete
    auto f3 = scheduler.schedule([user_count, config_ok]() {
        std::cout << "[Step 3 - Dependent] Generating audit report for " << user_count
                  << " accounts (Auth valid: " << (config_ok ? "true" : "false") << ")...\n";
        return std::string("REPORT_SUCCESS");
    });

    std::string report = f3.get();
    std::cout << "[Result] " << report << "\n";

    scheduler.wait_all();
    std::cout << "=== Dependency Workflow Complete ===\n";
    return 0;
}
