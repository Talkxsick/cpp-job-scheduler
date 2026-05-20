#include <chrono>
#include <thread>
#include <string>
#include <atomic>
#include <ncurses.h>
#include "job.h"
#include "thread_pool.h"
#include "ui.h"

int main() {
    UI ui;
    int job_id = 1;
    auto start_time = std::chrono::high_resolution_clock::now();

    ThreadPool pool(3, [&](const std::string& name, int priority, double elapsed) {
        ui.mark_completed(name, priority, elapsed);
    });

    std::string input_buf = "";
    std::string status_msg = "";
    std::chrono::steady_clock::time_point status_time;

    // Use nodelay so getch() is non-blocking
    nodelay(stdscr, TRUE);
    noecho();

    while (true) {
        // Redraw UI
        auto now = std::chrono::high_resolution_clock::now();
        double wall_ms = std::chrono::duration<double, std::milli>(now - start_time).count();
        ui.draw(pool.get_active_count(), wall_ms);

        // Draw current input buffer at input bar
        int rows, cols;
        getmaxyx(stdscr, rows, cols);
        move(rows - 3, 15);
        clrtoeol();
        printw("%s", input_buf.c_str());

        // Draw status message if any
        if (!status_msg.empty() && 
            std::chrono::steady_clock::now() - status_time < std::chrono::seconds(1))  {
            move(rows - 3, 2);
            attron(COLOR_PAIR(1));
            printw("%s", status_msg.c_str());
            attroff(COLOR_PAIR(1));
        } else {
            status_msg = "";
        }

        move(rows - 3, 15 + input_buf.size()); // keep cursor at end of input
        refresh();

        // Read one character (non-blocking)
        int ch = getch();

        if (ch == ERR) {
            // No input — sleep briefly to avoid busy loop
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        if (ch == '\n' || ch == KEY_ENTER) {
            status_msg = "";

            if (input_buf == "q" || input_buf == "quit") break;

            if (input_buf == "demo") {
                std::vector<std::pair<std::string,int>> demo_jobs = {
                    {"Emergency Alert",   10},
                    {"Critical DB Backup", 9},
                    {"Cache Invalidation", 7},
                    {"Health Check",       6},
                    {"Email Report",       5},
                    {"Metrics Aggregation",4},
                    {"Log Rotation",       2},
                    {"Temp Cleanup",       1},
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
                    pool.submit(job);
                }
                input_buf = "";
                continue;
            }

            // Parse "name,priority"
            auto comma = input_buf.find(',');
            if (comma == std::string::npos || comma == 0) {
                status_msg = "Bad format! Try: cleanup,5";
                status_time = std::chrono::steady_clock::now();
            } else {
                std::string name = input_buf.substr(0, comma);
                int priority = 5;
                try {
                    priority = std::stoi(input_buf.substr(comma + 1));
                } catch (...) { priority = 5; }
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
                pool.submit(job);
            }

            input_buf = "";

        } else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
            if (!input_buf.empty()) input_buf.pop_back();

        } else if (ch >= 32 && ch < 127) {
            // Printable character
            input_buf += static_cast<char>(ch);
        }
    }

    return 0;
}