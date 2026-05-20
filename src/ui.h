#pragma once
#include <ncurses.h>
#include <vector>
#include <mutex>
#include <string>
#include <iomanip>
#include <sstream>
#include "job.h"

// Tracks a completed job's result for display
struct CompletedJob {
    std::string name;
    int priority;
    double elapsed_ms;
};

class UI {
public:
    UI() {
        initscr();             // start ncurses
        cbreak();              // disable line buffering
        noecho();              // don't echo keypresses
        keypad(stdscr, TRUE);  // enable special keys
        curs_set(1);           // show cursor

        // Setup colors if terminal supports it
        if (has_colors()) {
            start_color();
            init_pair(1, COLOR_RED,     COLOR_BLACK); // high priority
            init_pair(2, COLOR_YELLOW,  COLOR_BLACK); // medium priority
            init_pair(3, COLOR_GREEN,   COLOR_BLACK); // low priority
            init_pair(4, COLOR_CYAN,    COLOR_BLACK); // headers
            init_pair(5, COLOR_WHITE,   COLOR_BLACK); // normal text
            init_pair(6, COLOR_BLACK,   COLOR_CYAN);  // title bar
        }
    }

    ~UI() {
        endwin(); // restore terminal to normal state
    }

    // Add a job to the pending list for display
    void add_pending(const Job& job) {
        std::lock_guard<std::mutex> lock(ui_mutex);
        pending.push_back({job.name, job.priority, 0.0});
    }

    // Move a job from pending to completed
    void mark_completed(const std::string& name, int priority, double elapsed) {
        std::lock_guard<std::mutex> lock(ui_mutex);

        // Remove from pending list
        pending.erase(
            std::remove_if(pending.begin(), pending.end(),
                [&](const CompletedJob& j) { return j.name == name; }),
            pending.end()
        );

        // Add to completed list (newest first)
        completed.insert(completed.begin(), {name, priority, elapsed});
        if (completed.size() > 50) completed.pop_back(); // keep last 50
    }

    // Full redraw of the UI — call this in a loop
    void draw(int active_count, double wall_time_ms) {
        std::lock_guard<std::mutex> lock(ui_mutex);

        clear();
        int rows, cols;
        getmaxyx(stdscr, rows, cols);

        draw_title(cols);
        draw_stats(1, active_count, wall_time_ms, cols);
        draw_divider(2, cols);
        draw_column_headers(3, cols);
        draw_divider(4, cols);
        draw_jobs(5, rows - 6, cols);
        draw_divider(rows - 4, cols);
        draw_input_bar(rows - 3, cols);
        draw_divider(rows - 2, cols);
        draw_help(rows - 1, cols);

        refresh();
    }

    // Public vectors so main can read them if needed
    std::vector<CompletedJob> pending;
    std::vector<CompletedJob> completed;
    std::mutex ui_mutex;

private:
    void draw_title(int cols) {
        attron(COLOR_PAIR(6) | A_BOLD);
        std::string title = "  C++ Job Scheduler v1.0  ";
        int pad = (cols - title.size()) / 2;
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
    printw("|  Completed: %zu  |  Wall Time: %.0fms",
           completed.size(), wall_ms);
    attroff(COLOR_PAIR(4));
}
    void draw_divider(int row, int cols) {
        attron(COLOR_PAIR(5));
        mvprintw(row, 0, "%s", std::string(cols, '-').c_str());
        attroff(COLOR_PAIR(5));
    }

    void draw_column_headers(int row, int cols) {
        int half = cols / 2;
        attron(COLOR_PAIR(4) | A_BOLD);
        mvprintw(row, 2,        "PENDING QUEUE");
        mvprintw(row, half + 2, "COMPLETED JOBS");
        attroff(COLOR_PAIR(4) | A_BOLD);
    }

    void draw_jobs(int start_row, int max_rows, int cols) {
        int half = cols / 2;

        // Draw pending jobs (left column)
        for (int i = 0; i < (int)pending.size() && i < max_rows; i++) {
            auto& job = pending[i];
            int color = priority_color(job.priority);
            attron(COLOR_PAIR(color));
            mvprintw(start_row + i, 2, "[P:%2d] %-25s",
                     job.priority,
                     job.name.substr(0, 24).c_str());
            attroff(COLOR_PAIR(color));
        }

        // Draw completed jobs (right column)
        for (int i = 0; i < (int)completed.size() && i < max_rows; i++) {
            auto& job = completed[i];
            int color = priority_color(job.priority);
            attron(COLOR_PAIR(color));
            mvprintw(start_row + i, half + 2, ";) [P:%2d] %-18s %6.0fms",
                job.priority,
                job.name.substr(0, 17).c_str(),
                job.elapsed_ms);
            attroff(COLOR_PAIR(color));
        }
    }

    void draw_input_bar(int row, int /*cols*/) {
        attron(COLOR_PAIR(5));
        mvprintw(row, 2, "Submit job > ");
        attroff(COLOR_PAIR(5));
    }

    void draw_help(int row, int /*cols*/) {
        attron(COLOR_PAIR(4));
        mvprintw(row, 2, "Format: <job name>,<priority 1-10>   |   'q' to quit");
        attroff(COLOR_PAIR(4));
    }

    int priority_color(int priority) {
        if (priority >= 8) return 1; // red
        if (priority >= 5) return 2; // yellow
        return 3;                    // green
    }
};