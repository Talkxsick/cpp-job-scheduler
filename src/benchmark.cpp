#include <iostream>
#include <vector>
#include <iomanip>
#include <numeric>
#include <cmath>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include "job.h"
#include "thread_pool.h"

// Holds results for one benchmark run
struct BenchmarkResult {
    int job_count;
    int thread_count;
    double wall_time_ms;
    double throughput;      // jobs per second
    double avg_latency_ms;
    double min_latency_ms;
    double max_latency_ms;
};

// Runs one benchmark — job_count jobs on thread_count threads
BenchmarkResult run_benchmark(int job_count, int thread_count) {
    std::vector<double> latencies;
    std::mutex latency_mutex;
    std::atomic<int> completed{0};
    std::condition_variable done_cv;
    std::mutex done_mutex;

    // Completion callback — records latency for each job
    ThreadPool pool(thread_count,
        [&](const std::string&, int, double elapsed) {
            {
                std::lock_guard<std::mutex> lock(latency_mutex);
                latencies.push_back(elapsed);
            }
            completed++;
            if (completed == job_count) done_cv.notify_one();
        }
    );

    // Build jobs — random duration 50-300ms to simulate real work
    std::vector<Job> jobs;
    for (int i = 0; i < job_count; i++) {
        Job job;
        job.id          = i + 1;
        job.name        = "bench_job_" + std::to_string(i + 1);
        job.priority    = (i % 10) + 1; // priorities 1-10 cycling
        job.duration_ms = 50 + (rand() % 250);
        job.should_fail = false;
        job.task        = [dur = job.duration_ms]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(dur));
        };
        jobs.push_back(job);
    }

    // Start wall clock
    auto wall_start = std::chrono::high_resolution_clock::now();

    // Submit all jobs
    for (auto& job : jobs) pool.submit(job);

    // Wait until all jobs complete
    {
        std::unique_lock<std::mutex> lock(done_mutex);
        done_cv.wait(lock, [&]() { return completed == job_count; });
    }

    auto wall_end = std::chrono::high_resolution_clock::now();
    double wall_ms = std::chrono::duration<double, std::milli>(
        wall_end - wall_start).count();

    // Calculate stats
    double avg = std::accumulate(latencies.begin(), latencies.end(), 0.0)
                 / latencies.size();
    double min_l = *std::min_element(latencies.begin(), latencies.end());
    double max_l = *std::max_element(latencies.begin(), latencies.end());
    double throughput = (job_count / wall_ms) * 1000.0; // jobs per second

    return {job_count, thread_count, wall_ms, throughput, avg, min_l, max_l};
}

void print_divider(int width) {
    std::cout << std::string(width, '=') << "\n";
}

void print_header() {
    print_divider(75);
    std::cout << "          C++ Job Scheduler — Benchmark Results\n";
    print_divider(75);
    std::cout << std::left
              << std::setw(8)  << "Jobs"
              << std::setw(10) << "Threads"
              << std::setw(14) << "Wall Time"
              << std::setw(14) << "Throughput"
              << std::setw(14) << "Avg Latency"
              << std::setw(12) << "Min"
              << std::setw(12) << "Max"
              << "\n";
    print_divider(75);
}

void print_result(const BenchmarkResult& r) {
    std::cout << std::left  << std::fixed << std::setprecision(1)
              << std::setw(8)  << r.job_count
              << std::setw(10) << r.thread_count
              << std::setw(14) << (std::to_string((int)r.wall_time_ms) + "ms")
              << std::setw(14) << (std::to_string((int)r.throughput)   + " j/s")
              << std::setw(14) << (std::to_string((int)r.avg_latency_ms) + "ms")
              << std::setw(12) << (std::to_string((int)r.min_latency_ms) + "ms")
              << std::setw(12) << (std::to_string((int)r.max_latency_ms) + "ms")
              << "\n";
}

void print_speedup(const std::vector<BenchmarkResult>& results, int job_count) {
    // Find baseline (1 thread) for this job count
    double baseline = -1;
    for (auto& r : results) {
        if (r.job_count == job_count && r.thread_count == 1) {
            baseline = r.wall_time_ms;
            break;
        }
    }
    if (baseline < 0) return;

    std::cout << "\n  Speedup vs single thread (" << job_count << " jobs):\n";
    for (auto& r : results) {
        if (r.job_count != job_count) continue;
        double speedup = baseline / r.wall_time_ms;
        std::cout << "    " << r.thread_count << " thread(s) → "
                  << std::fixed << std::setprecision(2) << speedup << "x\n";
    }
}

int main() {
    srand(42); // fixed seed for reproducible results

    std::vector<int> job_counts    = {100, 500, 1000};
    std::vector<int> thread_counts = {1, 3, 6};

    print_header();

    std::vector<BenchmarkResult> all_results;

    for (int jobs : job_counts) {
        for (int threads : thread_counts) {
            std::cout << "  Running: " << jobs << " jobs, "
                      << threads << " threads...\r" << std::flush;

            BenchmarkResult result = run_benchmark(jobs, threads);
            print_result(result);
            all_results.push_back(result);
        }
        print_divider(75); // divider between job count groups
    }

    // Print speedup analysis per job count
    std::cout << "\n";
    for (int jobs : job_counts) {
        print_speedup(all_results, jobs);
    }

    std::cout << "\n";
    print_divider(75);
    std::cout << "  Note: Job durations randomized 50-300ms. Throughput = jobs/sec.\n";
    print_divider(75);
    std::cout << "\n";

    return 0;
}