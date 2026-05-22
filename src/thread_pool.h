#pragma once
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <queue>
#include <stdexcept>
#include "job.h"

class ThreadPool {
public:
    // on_complete: called when job succeeds (name, priority, elapsed_ms)
    // on_failed:   called when job exhausts all retries
    using CompletionCallback = std::function<void(const std::string&, int, double)>;
    using FailureCallback    = std::function<void(const std::string&, int)>;
    using RetryCallback      = std::function<void(const std::string&, int, int, int)>;

    ThreadPool(int num_threads,
               CompletionCallback on_complete = nullptr,
               FailureCallback    on_failed   = nullptr,
               RetryCallback      on_retry    = nullptr)
        : stop(false),
          on_complete(on_complete),
          on_failed(on_failed),
          on_retry(on_retry)
    {
        for (int i = 0; i < num_threads; i++) {
            workers.emplace_back([this]() { worker_loop(); });
        }
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }
        cv.notify_all();
        for (auto& t : workers) t.join();
    }

    void submit(Job job) {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            job_queue.push(job);
        }
        cv.notify_one();
    }

    void wait_until_done() {
        std::unique_lock<std::mutex> lock(queue_mutex);
        cv.wait(lock, [this]() {
            return job_queue.empty() && active_workers == 0;
        });
    }

    int get_active_count() { return active_workers.load(); }

private:
    std::vector<std::thread> workers;
    std::priority_queue<Job> job_queue;
    std::mutex queue_mutex;
    std::condition_variable cv;
    std::atomic<int> active_workers{0};
    bool stop;
    CompletionCallback on_complete;
    FailureCallback    on_failed;
    RetryCallback      on_retry;

    void worker_loop() {
        while (true) {
            Job job{};
            bool got_job = false;

            {
                std::unique_lock<std::mutex> lock(queue_mutex);
                cv.wait(lock, [this]() {
                    return !job_queue.empty() || stop;
                });

                if (stop && job_queue.empty()) return;

                job = job_queue.top();
                job_queue.pop();
                got_job = true;
                active_workers++;
            }

            if (got_job) {
                auto start = std::chrono::high_resolution_clock::now();
                bool failed = false;

                try {
                    if (job.should_fail) {
                        // Simulate a failing job by throwing an exception
                        throw std::runtime_error("Job failed: " + job.name);
                    }
                    job.task(); // execute normally
                } catch (...) {
                    failed = true;
                }

                auto end = std::chrono::high_resolution_clock::now();
                double elapsed = std::chrono::duration<double, std::milli>(end - start).count();

                if (failed) {
                    job.retry_count++;

                    if (job.retry_count <= job.max_retries) {
                        // Exponential backoff: 500ms * 2^(retry_count - 1)
                        // retry 1 → 500ms, retry 2 → 1000ms, retry 3 → 2000ms
                        int backoff_ms = 500 * (1 << (job.retry_count - 1));

                        std::this_thread::sleep_for(
                            std::chrono::milliseconds(backoff_ms));

                        // Fire retry callback for logging
                        if (on_retry) on_retry(job.name, job.priority, job.retry_count, backoff_ms);

                        // Resubmit job back into the queue
                        submit(job);

                    } else {
                        // Exhausted all retries — mark as permanently failed
                        if (on_failed) on_failed(job.name, job.priority);
                    }

                } else {
                    // Job succeeded
                    if (on_complete) on_complete(job.name, job.priority, elapsed);
                }

                active_workers--;
                cv.notify_all();
            }
        }
    }
};