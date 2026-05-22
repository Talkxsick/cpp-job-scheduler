#include <chrono>
#include <thread>
#include <string>
#include <atomic>
#include <vector>
#include <ncurses.h>
#include "job.h"
#include "thread_pool.h"
#include "dispatcher.h"
#include "ui.h"
#include "logger.h"

int main() {
    UI ui;
    Logger logger("scheduler.log");
    int job_id = 1;
    auto start_time = std::chrono::high_resolution_clock::now();

    // Thread pool — two callbacks: completion and failure
    ThreadPool pool(3,
        [&](const std::string& name, int priority, double elapsed) {
            ui.mark_completed(name, priority, elapsed);
            logger.log_completed(name, priority, elapsed);
        },
        [&](const std::string& name, int priority) {
            ui.mark_failed(name, priority);
            logger.log_failed(name, priority);
        },
        [&](const std::string& name, int priority, int retry_count, int backoff_ms) {
            logger.log_retry(name, priority, retry_count, backoff_ms);
        }
    );

    // Dispatcher — sits between UI input and thread pool
    Dispatcher dispatcher(pool, 5);

    std::string input_buf = "";
    std::string status_msg = "";
    std::chrono::steady_clock::time_point status_time;
    bool status_is_warning = false;

    // Batch mode state
    bool batch_mode = false;
    std::vector<Job> batch_queue;

    nodelay(stdscr, TRUE);
    noecho();

    while (true) {
        auto now = std::chrono::high_resolution_clock::now();
        double wall_ms = std::chrono::duration<double, std::milli>(now - start_time).count();

        ui.draw(pool.get_active_count(), wall_ms,
                dispatcher.is_paused(), dispatcher.get_rate(),
                dispatcher.pending_count());

        int rows, cols;
        getmaxyx(stdscr, rows, cols);

        // Draw input bar
        move(rows - 3, 2);
        clrtoeol();
        if (batch_mode) {
            attron(COLOR_PAIR(2) | A_BOLD);
            printw("BATCH [%zu staged] > ", batch_queue.size());
            attroff(COLOR_PAIR(2) | A_BOLD);
        } else {
            attron(COLOR_PAIR(5));
            printw("Submit job > ");
            attroff(COLOR_PAIR(5));
        }
        printw("%s", input_buf.c_str());

        // Draw status message
        if (!status_msg.empty()) {
            if (std::chrono::steady_clock::now() - status_time < std::chrono::seconds(2)) {
                move(rows - 3, 2);
                int msg_color = status_is_warning ? 1 : 2;
                attron(COLOR_PAIR(msg_color));
                printw("%s", status_msg.c_str());
                attroff(COLOR_PAIR(msg_color));
            } else {
                status_msg = "";
            }
        }

        // Draw help line
        move(rows - 1, 2);
        clrtoeol();
        attron(COLOR_PAIR(4));
        if (batch_mode) {
            printw("Add jobs as name,priority | 'done' to submit all | 'cancel' to abort");
        } else {
            printw("name,priority | 'batch' | 'demo' | 'fail' | 'pause' | 'resume' | 'rate N' | 'q'");
        }
        attroff(COLOR_PAIR(4));

        // Cursor position
        int prompt_len = batch_mode
            ? (18 + (int)std::to_string(batch_queue.size()).length())
            : 13;
        move(rows - 3, prompt_len + (int)input_buf.size() + 2);
        refresh();

        int ch = getch();
        if (ch == ERR) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        if (ch == '\n' || ch == KEY_ENTER) {
            status_msg = "";

            // ── NORMAL MODE ──────────────────────────────────────────
            if (!batch_mode) {

                if (input_buf == "q" || input_buf == "quit") break;

                // Pause dispatcher
                if (input_buf == "pause") {
                    dispatcher.pause();
                    logger.log_dispatcher_paused();
                    status_msg = "Dispatcher paused. Jobs will queue but not execute.";
                    status_time = std::chrono::steady_clock::now();
                    status_is_warning = false;
                    input_buf = "";
                    continue;
                }

                // Resume dispatcher
                if (input_buf == "resume") {
                    dispatcher.resume();
                    logger.log_dispatcher_resumed();
                    status_msg = "Dispatcher resumed.";
                    status_time = std::chrono::steady_clock::now();
                    status_is_warning = false;
                    input_buf = "";
                    continue;
                }

                // Rate limiting
                if (input_buf.size() > 5 && input_buf.substr(0, 5) == "rate ") {
                    try {
                        int rate = std::stoi(input_buf.substr(5));
                        rate = std::max(1, std::min(20, rate));
                        dispatcher.set_rate(rate);
                        logger.log_dispatcher_rate(rate);
                        status_msg = "Rate set to " + std::to_string(rate) + " jobs/sec.";
                        status_time = std::chrono::steady_clock::now();
                        status_is_warning = false;
                    } catch (...) {
                        status_msg = "Usage: rate N  (e.g. rate 3)";
                        status_time = std::chrono::steady_clock::now();
                        status_is_warning = true;
                    }
                    input_buf = "";
                    continue;
                }

                // Batch mode
                if (input_buf == "batch") {
                    batch_mode = true;
                    batch_queue.clear();
                    input_buf = "";
                    continue;
                }

                // Demo mode — normal jobs
                if (input_buf == "demo") {
                    std::vector<std::pair<std::string, int>> demo_jobs = {
                        {"Emergency Alert",    10},
                        {"Critical DB Backup",  9},
                        {"Cache Invalidation",  7},
                        {"Health Check",         6},
                        {"Email Report",         5},
                        {"Metrics Aggregation",  4},
                        {"Log Rotation",         2},
                        {"Temp Cleanup",         1},
                    };
                    for (auto& [name, priority] : demo_jobs) {
                        Job job;
                        job.id          = job_id++;
                        job.name        = name;
                        job.priority    = priority;
                        job.duration_ms = 200 + (rand() % 400);
                        job.should_fail = false;
                        job.task        = [dur = job.duration_ms]() {
                            std::this_thread::sleep_for(std::chrono::milliseconds(dur));
                        };
                        ui.add_pending(job);
                        dispatcher.enqueue(job);
                        logger.log_submitted(job.name, job.priority);
                    }
                    input_buf = "";
                    continue;
                }

                // Fail demo — jobs that will fail and retry
                if (input_buf == "fail") {
                    std::vector<std::pair<std::string, int>> fail_jobs = {
                        {"Flaky Network Call",  8},
                        {"Unstable DB Write",   6},
                        {"Corrupt File Read",   4},
                    };
                    for (auto& [name, priority] : fail_jobs) {
                        Job job;
                        job.id          = job_id++;
                        job.name        = name;
                        job.priority    = priority;
                        job.duration_ms = 100;
                        job.should_fail = true;  // will throw exception
                        job.max_retries = 3;
                        job.task        = [dur = job.duration_ms]() {
                            std::this_thread::sleep_for(std::chrono::milliseconds(dur));
                        };
                        ui.add_pending(job);
                        dispatcher.enqueue(job);
                        logger.log_submitted(job.name, job.priority);
                    }
                    status_msg = "3 failing jobs submitted — watch them retry then fail.";
                    status_time = std::chrono::steady_clock::now();
                    status_is_warning = false;
                    input_buf = "";
                    continue;
                }

                // Single job submit
                auto comma = input_buf.find(',');
                if (comma == std::string::npos || comma == 0) {
                    status_msg = "Bad format! Use: name,priority  e.g. cleanup,5";
                    status_time = std::chrono::steady_clock::now();
                    status_is_warning = true;
                } else {
                    std::string name = input_buf.substr(0, comma);
                    int priority = 5;
                    try { priority = std::stoi(input_buf.substr(comma + 1)); }
                    catch (...) { priority = 5; }
                    priority = std::max(1, std::min(10, priority));

                    Job job;
                    job.id          = job_id++;
                    job.name        = name;
                    job.priority    = priority;
                    job.duration_ms = 200 + (rand() % 400);
                    job.should_fail = false;
                    job.task        = [dur = job.duration_ms]() {
                        std::this_thread::sleep_for(std::chrono::milliseconds(dur));
                    };
                    ui.add_pending(job);
                    dispatcher.enqueue(job);
                    logger.log_submitted(job.name, job.priority);
                }

            // ── BATCH MODE ───────────────────────────────────────────
            } else {

                if (input_buf == "done") {
                    if (batch_queue.empty()) {
                        status_msg = "No jobs staged. Add jobs first.";
                        status_time = std::chrono::steady_clock::now();
                        status_is_warning = true;
                    } else {
                        int count = batch_queue.size();
                        for (auto& job : batch_queue) {
                            ui.add_pending(job);
                            dispatcher.enqueue(job);
                            logger.log_submitted(job.name, job.priority);
                        }
                        logger.log_batch(count);
                        batch_queue.clear();
                        batch_mode = false;
                        status_msg = std::to_string(count) + " jobs submitted to dispatcher!";
                        status_time = std::chrono::steady_clock::now();
                        status_is_warning = false;
                    }
                    input_buf = "";
                    continue;
                }

                if (input_buf == "cancel") {
                    batch_queue.clear();
                    batch_mode = false;
                    status_msg = "Batch cancelled.";
                    status_time = std::chrono::steady_clock::now();
                    status_is_warning = false;
                    input_buf = "";
                    continue;
                }

                // Stage job
                auto comma = input_buf.find(',');
                if (comma == std::string::npos || comma == 0) {
                    status_msg = "Bad format! Use: name,priority  e.g. cleanup,5";
                    status_time = std::chrono::steady_clock::now();
                    status_is_warning = true;
                } else {
                    std::string name = input_buf.substr(0, comma);
                    int priority = 5;
                    try { priority = std::stoi(input_buf.substr(comma + 1)); }
                    catch (...) { priority = 5; }
                    priority = std::max(1, std::min(10, priority));

                    Job job;
                    job.id          = job_id++;
                    job.name        = name;
                    job.priority    = priority;
                    job.duration_ms = 200 + (rand() % 400);
                    job.should_fail = false;
                    job.task        = [dur = job.duration_ms]() {
                        std::this_thread::sleep_for(std::chrono::milliseconds(dur));
                    };
                    batch_queue.push_back(job);
                }
            }

            input_buf = "";

        } else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
            if (!input_buf.empty()) input_buf.pop_back();

        } else if (ch >= 32 && ch < 127) {
            input_buf += static_cast<char>(ch);
        }
    }

    return 0;
}