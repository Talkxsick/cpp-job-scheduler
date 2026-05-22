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

int main() {
    UI ui;
    int job_id = 1;
    auto start_time = std::chrono::high_resolution_clock::now();

    ThreadPool pool(3, [&](const std::string& name, int priority, double elapsed) {
        ui.mark_completed(name, priority, elapsed);
    });

    // Dispatcher sits between UI input and thread pool
    // Default rate: 5 jobs/sec
    Dispatcher dispatcher(pool, 5);

    std::string input_buf = "";
    std::string status_msg = "";
    std::chrono::steady_clock::time_point status_time;
    bool status_is_warning = false; 

    // Batch mode state
    bool batch_mode = false;
    std::vector<Job> batch_queue;

    nodelay(stdscr, TRUE); // Non-blocking input 
    noecho(); // Don't echo input characters

    while (true) {
        // Calculate wall time
        auto now = std::chrono::high_resolution_clock::now();
        double wall_ms = std::chrono::duration<double, std::milli>(now - start_time).count();

        // Draw UI — pass dispatcher state
        ui.draw(pool.get_active_count(), wall_ms,
                dispatcher.is_paused(), dispatcher.get_rate(),
                dispatcher.pending_count());

        int rows, cols;
        getmaxyx(stdscr, rows, cols);

        // Draw input bar — changes based on batch mode
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

        // Draw status message if still within 2 seconds
        if (!status_msg.empty()) {
            if (std::chrono::steady_clock::now() - status_time < std::chrono::seconds(2)) {
                move(rows - 3, 2);
                int msg_color = status_is_warning ? 1 : 2; // red for errors, yellow for info
                attron(COLOR_PAIR(msg_color));
                printw("%s", status_msg.c_str());
                attroff(COLOR_PAIR(msg_color));
            } else {
                status_msg = "";
            }
        }

        // Draw help line based on mode
        move(rows - 1, 2);
        clrtoeol();
        attron(COLOR_PAIR(4));
        if (batch_mode) {
            printw("Add jobs as name,priority | 'done' to submit all | 'cancel' to abort");
        } else {
            printw("name,priority | 'batch' | 'demo' | 'pause' | 'resume' | 'rate N' | 'q'");
        }
        attroff(COLOR_PAIR(4));

        // Calculate exact cursor position based on prompt length + input length
        int prompt_len = batch_mode ? (18 + (int)std::to_string(batch_queue.size()).length()) : 13;
        move(rows - 3, prompt_len + (int)input_buf.size() + 2);
        refresh();

        // Read one character (non-blocking)
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
                    status_msg = "Dispatcher paused. Jobs will queue but not execute.";
                    status_time = std::chrono::steady_clock::now();
                    status_is_warning = false;
                    input_buf = "";
                    continue;
                }

                // Resume dispatcher
                if (input_buf == "resume") {
                    dispatcher.resume();
                    status_msg = "Dispatcher resumed.";
                    status_time = std::chrono::steady_clock::now();
                    status_is_warning = false;
                    input_buf = "";
                    continue;
                }

                // Rate limiting: "rate 3" sets 3 jobs/sec
                if (input_buf.size() > 5 && input_buf.substr(0, 5) == "rate ") {
                    try {
                        int rate = std::stoi(input_buf.substr(5));
                        rate = std::max(1, std::min(20, rate)); // clamp 1-20
                        dispatcher.set_rate(rate);
                        status_msg = "Rate set to " + std::to_string(rate) + " jobs/sec.";
                        status_is_warning = false;
                        status_time = std::chrono::steady_clock::now();
                    } catch (...) {
                        status_msg = "Usage: rate N  (e.g. rate 3)";
                        status_is_warning = true;
                        status_time = std::chrono::steady_clock::now();
                    }
                    input_buf = "";
                    continue;
                }

                // Enter batch mode
                if (input_buf == "batch") {
                    batch_mode = true;
                    batch_queue.clear();
                    input_buf = "";
                    continue;
                }

                // Demo mode
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
                        job.task        = [dur = job.duration_ms]() {
                            std::this_thread::sleep_for(std::chrono::milliseconds(dur));
                        };
                        ui.add_pending(job);
                        dispatcher.enqueue(job); // through dispatcher
                    }
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
                    job.task        = [dur = job.duration_ms]() {
                        std::this_thread::sleep_for(std::chrono::milliseconds(dur));
                    };
                    ui.add_pending(job);
                    dispatcher.enqueue(job); // through dispatcher
                }

            // ── BATCH MODE ───────────────────────────────────────────
            } else {

                // Submit all staged jobs
                if (input_buf == "done") {
                    if (batch_queue.empty()) {
                        status_msg = "No jobs staged. Add jobs first.";
                        status_time = std::chrono::steady_clock::now();
                        status_is_warning = true;
                    } else {
                        int count = batch_queue.size();
                        for (auto& job : batch_queue) {
                            ui.add_pending(job);
                            dispatcher.enqueue(job); // through dispatcher
                        }
                        batch_queue.clear();
                        batch_mode = false;
                        status_msg = std::to_string(count) + " jobs submitted to dispatcher!";
                        status_time = std::chrono::steady_clock::now();
                        status_is_warning = false;
                    }
                    input_buf = "";
                    continue;
                }

                // Cancel batch mode
                if (input_buf == "cancel") {
                    batch_queue.clear();
                    batch_mode = false;
                    status_msg = "Batch cancelled.";
                    status_time = std::chrono::steady_clock::now();
                    status_is_warning = false;
                    input_buf = "";
                    continue;
                }

                // Stage a job into batch queue
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
                    job.task        = [dur = job.duration_ms]() {
                        std::this_thread::sleep_for(std::chrono::milliseconds(dur));
                    };
                    batch_queue.push_back(job); // stage, don't submit yet
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