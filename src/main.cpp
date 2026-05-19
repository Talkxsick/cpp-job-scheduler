#include <iostream>
#include <chrono>
#include "job.h"
#include "thread_pool.h"

int main() {
    const int NUM_THREADS = 3; // 3 workers running jobs concurrently

    std::cout << "=== Job Scheduler — Thread Pool (" << NUM_THREADS << " threads) ===\n\n";

    ThreadPool pool(NUM_THREADS);

    // Same jobs as Day 1 — but now they run concurrently across threads
    std::vector<Job> jobs = {
        {1, "Low Priority Cleanup",  2, 100, []() { std::this_thread::sleep_for(std::chrono::milliseconds(100)); }},
        {2, "Critical DB Backup",    9, 300, []() { std::this_thread::sleep_for(std::chrono::milliseconds(300)); }},
        {3, "Send Email Report",     5, 150, []() { std::this_thread::sleep_for(std::chrono::milliseconds(150)); }},
        {4, "Emergency Alert",      10,  50, []() { std::this_thread::sleep_for(std::chrono::milliseconds(50));  }},
        {5, "Weekly Log Rotation",   1, 200, []() { std::this_thread::sleep_for(std::chrono::milliseconds(200)); }},
        {6, "Cache Invalidation",    7, 120, []() { std::this_thread::sleep_for(std::chrono::milliseconds(120)); }},
        {7, "Health Check Ping",     6,  80, []() { std::this_thread::sleep_for(std::chrono::milliseconds(80));  }},
        {8, "Metrics Aggregation",   4, 180, []() { std::this_thread::sleep_for(std::chrono::milliseconds(180)); }},
    };

    auto wall_start = std::chrono::high_resolution_clock::now();

    std::cout << "=== Submitting " << jobs.size() << " jobs ===\n\n";
    for (auto& job : jobs) {
        std::cout << "  Queued: \"" << job.name << "\" (priority " << job.priority << ")\n";
        pool.submit(job);
    }

    std::cout << "\n=== Waiting for all jobs to complete ===\n\n";
    pool.wait_until_done();

    auto wall_end = std::chrono::high_resolution_clock::now();
    double total = std::chrono::duration<double, std::milli>(wall_end - wall_start).count();

    std::cout << "=== All jobs complete ===\n";
    std::cout << "Total wall time: " << total << "ms\n";
    std::cout << "(Single-threaded would take ~"
              << 100+300+150+50+200+120+80+180 << "ms)\n";

    return 0;
}