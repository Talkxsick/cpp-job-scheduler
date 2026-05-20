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
    // on_complete callback: called when a job finishes (name, priority, elapsed_ms)
    using CompletionCallback = std::function<void(const std::string&, int, double)>;

    ThreadPool(int num_threads, CompletionCallback on_complete = nullptr)
        : stop(false), on_complete(on_complete) {
        for (int i = 0; i < num_threads; i++) {
            workers.emplace_back([this, i]() {
                worker_loop(i);
            });
        }
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }
        cv.notify_all();
        for (auto& t : workers) {
            t.join();
        }
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

    void worker_loop(int /*thread_id*/) {
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
                job.task();
                auto end = std::chrono::high_resolution_clock::now();
                double elapsed = std::chrono::duration<double, std::milli>(end - start).count();

                // Notify UI that this job is done
                if (on_complete) on_complete(job.name, job.priority, elapsed);

                active_workers--;
                cv.notify_all();
            }
        }
    }
};