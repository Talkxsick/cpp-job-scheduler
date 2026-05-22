#pragma once
#include <string>
#include <chrono>
#include <functional>

// Structure of a job object
struct Job {
    int id;
    std::string name;
    int priority;          // higher priority (1–10)
    int duration_ms;       
    std::function<void()> task; 

    // Retry fields
    int retry_count  = 0; 
    int max_retries  = 3; 
    bool should_fail = false; // if true, job throws exception (for testing retry)

    // Needed by std::priority_queue to order jobs by priority
    bool operator<(const Job& other) const {
        return priority < other.priority; // max-heap: highest priority runs first
    }
};