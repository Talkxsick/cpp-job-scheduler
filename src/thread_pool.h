#pragma once
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <queue>  
#include "job.h"

class ThreadPool {
public:
    // Constructor: spins up 'num_threads' worker threads immediately
    ThreadPool(int num_threads) : stop(false) {
        for (int i = 0; i < num_threads; i++) {
            // Each worker runs the same loop: wait → grab job → execute
            workers.emplace_back([this, i]() {
                worker_loop(i);
            });
        }
    }

    // Destructor: cleanly shuts down all threads when pool goes out of scope
    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true; // signal all threads to stop after finishing current job
        }
        cv.notify_all(); // wake up any sleeping threads so they can exit
        for (auto& t : workers) {
            t.join(); // wait for every thread to fully finish
        }
    }

    // Submit a job to the queue — thread-safe
    void submit(Job job) {
        {
            // Lock the queue before touching it — only one thread at a time
            std::unique_lock<std::mutex> lock(queue_mutex);
            job_queue.push(job);
        }
        cv.notify_one(); // wake up one sleeping worker to handle the new job
    }

    // Block the main thread until all jobs are done
    void wait_until_done() {
        std::unique_lock<std::mutex> lock(queue_mutex);
        // Sleep until queue is empty AND no workers are currently running
        cv.wait(lock, [this]() {
            return job_queue.empty() && active_workers == 0;
        });
    }

private:
    std::vector<std::thread> workers;
    std::priority_queue<Job> job_queue;

    std::mutex queue_mutex;          // protects job_queue from simultaneous access
    std::condition_variable cv;      // lets threads sleep/wake efficiently
    std::atomic<int> active_workers{0}; // counts how many threads are currently running a job
    bool stop;

    // The loop every worker thread runs continuously
    void worker_loop(int thread_id) {
        while (true) {
            Job job{};   // zero-initialize all fields
            bool got_job = false;

            {
                std::unique_lock<std::mutex> lock(queue_mutex);

                // Sleep here until: a job is available OR pool is stopping
                cv.wait(lock, [this]() {
                    return !job_queue.empty() || stop;
                });

                if (stop && job_queue.empty()) return; // clean exit

                // Grab the highest priority job
                job = job_queue.top();
                job_queue.pop();
                got_job = true;
                active_workers++;
            }

            if (got_job) {
                std::cout << "[Thread " << thread_id << "] Running: \""
                          << job.name << "\" (priority " << job.priority << ")\n";

                auto start = std::chrono::high_resolution_clock::now();
                job.task(); // execute the actual work
                auto end = std::chrono::high_resolution_clock::now();
                double elapsed = std::chrono::duration<double, std::milli>(end - start).count();

                std::cout << "[Thread " << thread_id << "] Done: \""
                          << job.name << "\" in " << elapsed << "ms\n\n";

                active_workers--;
                cv.notify_all(); // notify wait_until_done() in case queue is now empty
            }
        }
    }
};