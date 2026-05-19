#include <iostream>
#include <queue>
#include <vector>
#include <thread>
#include <chrono>
#include "job.h"

// Runs a single job and prints timing info
void execute_job(const Job& job) {
    auto start = std::chrono::high_resolution_clock::now();

    std::cout << "[RUNNING] Job #" << job.id
              << " | \"" << job.name << "\""
              << " | Priority: " << job.priority << "\n";

    job.task(); // Execute the actual work

    auto end = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double, std::milli>(end - start).count();

    std::cout << "[DONE]    Job #" << job.id
              << " completed in " << elapsed << "ms\n\n";
}

int main() {
    // std::priority_queue uses max-heap by default
    // Our Job::operator< ensures highest priority jobs come out first
    std::priority_queue<Job> job_queue;

    // Create some sample jobs with different priorities
    std::vector<Job> jobs = {
        {1, "Low Priority Cleanup",   2, 100, []() { std::this_thread::sleep_for(std::chrono::milliseconds(100)); }},
        {2, "Critical DB Backup",     9, 300, []() { std::this_thread::sleep_for(std::chrono::milliseconds(300)); }},
        {3, "Send Email Report",      5, 150, []() { std::this_thread::sleep_for(std::chrono::milliseconds(150)); }},
        {4, "Emergency Alert",       10, 50,  []() { std::this_thread::sleep_for(std::chrono::milliseconds(50));  }},
        {5, "Weekly Log Rotation",    1, 200, []() { std::this_thread::sleep_for(std::chrono::milliseconds(200)); }},
    };

    // Push all jobs into the priority queue
    std::cout << "=== Submitting Jobs ===\n";
    for (auto& job : jobs) {
        std::cout << "  Queued: \"" << job.name << "\" (priority " << job.priority << ")\n";
        job_queue.push(job);
    }

    std::cout << "\n=== Executing Jobs (highest priority first) ===\n\n";

    // Drain the queue — highest priority job always runs next
    while (!job_queue.empty()) {
        Job next = job_queue.top(); // peek at highest priority
        job_queue.pop();            // remove it from queue
        execute_job(next);
    }

    std::cout << "=== All jobs complete ===\n";
    return 0;
}