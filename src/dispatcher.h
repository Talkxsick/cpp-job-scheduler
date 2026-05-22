#pragma once
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include "job.h"
#include "thread_pool.h"

class Dispatcher {
public:
    // pool        — the thread pool to forward jobs to
    // jobs_per_sec — max rate at which jobs are dispatched (rate limiting)
    Dispatcher(ThreadPool& pool, int jobs_per_sec = 5)
        : pool(pool),
          jobs_per_sec(jobs_per_sec),
          paused(false),
          stop(false)
    {
        // Dispatcher runs on its own dedicated thread
        dispatch_thread = std::thread([this]() { dispatch_loop(); });
    }

    ~Dispatcher() {
        {
            std::unique_lock<std::mutex> lock(mtx);
            stop = true;
        }
        cv.notify_all();
        if (dispatch_thread.joinable()) dispatch_thread.join();
    }

    // Enqueue a job — called from main.cpp
    // Job goes into dispatcher's internal queue, NOT directly to pool
    void enqueue(Job job) {
        {
            std::unique_lock<std::mutex> lock(mtx);
            incoming.push(job);
        }
        cv.notify_one(); // wake dispatcher thread
    }

    // Pause dispatching — jobs accumulate in queue but don't reach pool
    void pause() {
        std::unique_lock<std::mutex> lock(mtx);
        paused = true;
    }

    // Resume dispatching
    void resume() {
        {
            std::unique_lock<std::mutex> lock(mtx);
            paused = false;
        }
        cv.notify_one(); // wake dispatcher thread so it starts forwarding again
    }

    bool is_paused() const { return paused.load(); }

    // Set a new rate limit on the fly
    void set_rate(int new_jobs_per_sec) {
        std::unique_lock<std::mutex> lock(mtx);
        jobs_per_sec = new_jobs_per_sec;
    }

    int get_rate() const { return jobs_per_sec; }

    int pending_count() {
        std::unique_lock<std::mutex> lock(mtx);
        return incoming.size();
    }

private:
    ThreadPool& pool;
    std::queue<Job> incoming;   // dispatcher's own internal queue (FIFO)
                                // note: NOT a priority queue — dispatcher forwards
                                // in arrival order; pool handles priority internally

    std::atomic<int>  jobs_per_sec; // max jobs forwarded per second
    std::atomic<bool> paused;       // pause flag
    std::atomic<bool> stop;         // shutdown flag

    std::thread dispatch_thread;
    std::mutex mtx;
    std::condition_variable cv;

    void dispatch_loop() {
        while (true) {
            std::unique_lock<std::mutex> lock(mtx);

            // Sleep until: a job is available AND not paused AND not stopping
            cv.wait(lock, [this]() {
                return (!incoming.empty() && !paused) || stop;
            });

            if (stop && incoming.empty()) return; // clean shutdown

            if (paused) continue; // spurious wakeup while paused — go back to sleep

            // Grab the next job
            Job job = incoming.front();
            incoming.pop();
            lock.unlock(); // release lock before forwarding to pool

            // Forward to thread pool
            pool.submit(job);

            // Rate limiting — wait between dispatches
            // e.g. jobs_per_sec=5 → wait 200ms between each job
            if (jobs_per_sec > 0) {
                int delay_ms = 1000 / jobs_per_sec;
                std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
            }
        }
    }
};