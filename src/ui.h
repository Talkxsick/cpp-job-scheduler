#pragma once
#include <ncurses.h>
#include <vector>
#include <mutex>
#include <string>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include "job.h"

// Tracks a job's result for display
struct CompletedJob {
    std::string name;
    int priority;
    double elapsed_ms;
};

struct FailedJob {
    std::string name;
    int priority;
    int retries;
};

class UI {
public:
    UI() {
        initscr();
        cbreak();
        noecho();
        keypad(stdscr, TRUE);
        curs_set(1);

        if (has_colors()) {
            start_color();
            init_pair(1, COLOR_RED,     COLOR_BLACK); // high priority / errors
            init_pair(2, COLOR_YELLOW,  COLOR_BLACK); // medium priority / warnings
            init_pair(3, COLOR_GREEN,   COLOR_BLACK); // low priority / success
            init_pair(4, COLOR_CYAN,    COLOR_BLACK); // headers
            init_pair(5, COLOR_WHITE,   COLOR_BLACK); // normal text
            init_pair(6, COLOR_BLACK,   COLOR_CYAN);  // title bar
        }
    }

    ~UI() { endwin(); }

    void add_pending(const Job& job) {
        std::lock_guard<std::mutex> lock(ui_mutex);
        pending.push_back({job.name, job.priority, 0.0});
    }

    void mark_completed(const std::string& name, int priority, double elapsed) {
        std::lock_guard<std::mutex> lock(ui_mutex);
        pending.erase(
            std::remove_if(pending.begin(), pending.end(),
                [&](const CompletedJob& j) { return j.name == name; }),
            pending.end()
        );
        completed.insert(completed.begin(), {name, priority, elapsed});
        if (completed.size() > 50) completed.pop_back();
    }

    // Called when a job exhausts all retries
    void mark_failed(const std::string& name, int priority) {
        std::lock_guard<std::mutex> lock(ui_mutex);
        pending.erase(
            std::remove_if(pending.begin(), pending.end(),
                [&](const CompletedJob& j) { return j.name == name; }),
            pending.end()
        );
        failed.insert(failed.begin(), {name, priority, 3});
        if (failed.size() > 20) failed.pop_back();
    }

    void draw(int active_count, double wall_time_ms,
              bool dispatcher_paused, int dispatch_rate, int dispatcher_pending) {
        std::lock_guard<std::mutex> lock(ui_mutex);

        clear();
        int rows, cols;
        getmaxyx(stdscr, rows, cols);

        draw_title(cols);
        draw_stats(1, active_count, wall_time_ms, cols);
        draw_dispatcher_stats(2, dispatcher_paused, dispatch_rate, dispatcher_pending, cols);
        draw_divider(3, cols);
        draw_column_headers(4, cols);
        draw_divider(5, cols);
        draw_jobs(6, rows - 6, cols);
        draw_divider(rows - 4, cols);
        draw_input_bar(rows - 3, cols);
        draw_divider(rows - 2, cols);
        draw_help(rows - 1, cols);

        refresh();
    }

    std::vector<CompletedJob> pending;
    std::vector<CompletedJob> completed;
    std::vector<FailedJob>    failed;
    std::mutex ui_mutex;

private:
    void draw_title(int cols) {
        attron(COLOR_PAIR(6) | A_BOLD);
        std::string title = "  C++ Job Scheduler v1.0  ";
        int pad = (cols - (int)title.size()) / 2;
        mvprintw(0, 0, "%s", std::string(cols, ' ').c_str());
        mvprintw(0, pad, "%s", title.c_str());
        attroff(COLOR_PAIR(6) | A_BOLD);
    }

    void draw_stats(int row, int active, double wall_ms, int /*cols*/) {
        mvprintw(row, 2, "Threads: ");
        for (int i = 0; i < 3; i++) {
            if (i < active) {
                attron(COLOR_PAIR(1) | A_BOLD | A_REVERSE);
                printw("[BUSY] ");
                attroff(COLOR_PAIR(1) | A_BOLD | A_REVERSE);
            } else {
                attron(COLOR_PAIR(3));
                printw("[idle] ");
                attroff(COLOR_PAIR(3));
            }
        }
        attron(COLOR_PAIR(4));
        printw("|  Done: %zu  |  Failed: %zu  |  Wall Time: %.0fms",
               completed.size(), failed.size(), wall_ms);
        attroff(COLOR_PAIR(4));
    }

    void draw_dispatcher_stats(int row, bool paused, int rate, int pending_count, int /*cols*/) {
        mvprintw(row, 2, "Dispatcher: ");
        if (paused) {
            attron(COLOR_PAIR(1) | A_BOLD | A_REVERSE);
            printw("[PAUSED] ");
            attroff(COLOR_PAIR(1) | A_BOLD | A_REVERSE);
        } else {
            attron(COLOR_PAIR(3) | A_BOLD);
            printw("[RUNNING] ");
            attroff(COLOR_PAIR(3) | A_BOLD);
        }
        attron(COLOR_PAIR(4));
        printw("|  Rate: %d jobs/sec  |  Queued in dispatcher: %d",
               rate, pending_count);
        attroff(COLOR_PAIR(4));
    }

    void draw_divider(int row, int cols) {
        attron(COLOR_PAIR(5));
        mvprintw(row, 0, "%s", std::string(cols, '-').c_str());
        attroff(COLOR_PAIR(5));
    }

    void draw_column_headers(int row, int cols) {
        int third = cols / 3;
        attron(COLOR_PAIR(4) | A_BOLD);
        mvprintw(row, 2,           "PENDING QUEUE");
        mvprintw(row, third + 2,   "COMPLETED JOBS");
        mvprintw(row, third*2 + 2, "FAILED JOBS");
        attroff(COLOR_PAIR(4) | A_BOLD);
    }

    void draw_jobs(int start_row, int max_rows, int cols) {
        int third = cols / 3;

        // Left column — pending
        for (int i = 0; i < (int)pending.size() && i < max_rows; i++) {
            auto& job = pending[i];
            attron(COLOR_PAIR(priority_color(job.priority)));
            mvprintw(start_row + i, 2, "[P:%2d] %-18s",
                     job.priority, job.name.substr(0, 17).c_str());
            attroff(COLOR_PAIR(priority_color(job.priority)));
        }

        // Middle column — completed
        for (int i = 0; i < (int)completed.size() && i < max_rows; i++) {
            auto& job = completed[i];
            attron(COLOR_PAIR(priority_color(job.priority)));
            mvprintw(start_row + i, third + 2, ":) [P:%2d] %-12s %5.0fms",
                     job.priority, job.name.substr(0, 11).c_str(), job.elapsed_ms);
            attroff(COLOR_PAIR(priority_color(job.priority)));
        }

        // Right column — failed
        for (int i = 0; i < (int)failed.size() && i < max_rows; i++) {
            auto& job = failed[i];
            attron(COLOR_PAIR(1) | A_BOLD); // always red
            mvprintw(start_row + i, third*2 + 2, "X [P:%2d] %-12s r:%d",
                     job.priority, job.name.substr(0, 11).c_str(), job.retries);
            attroff(COLOR_PAIR(1) | A_BOLD);
        }
    }

    void draw_input_bar(int row, int /*cols*/) {
        attron(COLOR_PAIR(5));
        mvprintw(row, 2, "Submit job > ");
        attroff(COLOR_PAIR(5));
    }

    void draw_help(int row, int /*cols*/) {
        attron(COLOR_PAIR(4));
        mvprintw(row, 2, "name,priority | 'batch' | 'demo' | 'fail' | 'pause' | 'resume' | 'rate N' | 'q'");
        attroff(COLOR_PAIR(4));
    }

    int priority_color(int priority) {
        if (priority >= 8) return 1;
        if (priority >= 5) return 2;
        return 3;
    }
};