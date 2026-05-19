#pragma once
#include <string>
#include <chrono>
#include <functional>

// Represents a single unit of work in the scheduler
struct Job {
    int id;
    std::string name;
    int priority;          // Higher number = higher priority (1–10)
    int duration_ms;       // Simulated execution time in milliseconds
    std::function<void()> task;  // The actual work to run

    // Needed by std::priority_queue to order jobs by priority
    bool operator<(const Job& other) const {
        return priority < other.priority; // max-heap: highest priority runs first
    }
};