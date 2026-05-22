#pragma once
#include <fstream>
#include <mutex>
#include <string>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <atomic>

class Logger {
public:
    Logger(const std::string& filename = "scheduler.log") {
        log_file.open(filename, std::ios::app);
        session_start = std::chrono::steady_clock::now();
        write_raw("========================================");
        write_raw("  Scheduler Session Started");
        write_raw("========================================");
    }

    ~Logger() {
        write_session_summary();
        log_file.close();
    }

    // Job lifecycle
    void log_submitted(const std::string& name, int priority) {
        submitted_count++;
        std::ostringstream oss;
        oss << "[" << timestamp() << "] "
            << "SUBMITTED  | "
            << "P:" << std::setw(2) << priority << " | "
            << std::left << std::setw(22) << name;
        write_raw(oss.str());
    }

    void log_completed(const std::string& name, int priority, double elapsed_ms) {
        completed_count++;
        double current = total_elapsed_ms.load();
        total_elapsed_ms.store(current + elapsed_ms);
        std::ostringstream oss;
        oss << "[" << timestamp() << "] "
            << "COMPLETED  | "
            << "P:" << std::setw(2) << priority << " | "
            << std::left << std::setw(22) << name << " | "
            << std::fixed << std::setprecision(0) << elapsed_ms << "ms";
        write_raw(oss.str());
    }

    void log_failed(const std::string& name, int priority) {
        failed_count++;
        std::ostringstream oss;
        oss << "[" << timestamp() << "] "
            << "FAILED     | "
            << "P:" << std::setw(2) << priority << " | "
            << std::left << std::setw(22) << name << " | "
            << "retries: 3";
        write_raw(oss.str());
    }

    void log_retry(const std::string& name, int priority, int retry_count, int backoff_ms) {
        std::ostringstream oss;
        oss << "[" << timestamp() << "] "
            << "RETRY      | "
            << "P:" << std::setw(2) << priority << " | "
            << std::left << std::setw(22) << name << " | "
            << "attempt " << retry_count << " | backoff " << backoff_ms << "ms";
        write_raw(oss.str());
    }

    // Dispatcher state changes
    void log_dispatcher_paused() {
        write_raw("[" + timestamp() + "] DISPATCHER | PAUSED");
    }

    void log_dispatcher_resumed() {
        write_raw("[" + timestamp() + "] DISPATCHER | RESUMED");
    }

    void log_dispatcher_rate(int new_rate) {
        write_raw("[" + timestamp() + "] DISPATCHER | RATE CHANGED -> " +
                  std::to_string(new_rate) + " jobs/sec");
    }

    // Batch submission
    void log_batch(int count) {
        write_raw("[" + timestamp() + "] BATCH      | " +
                  std::to_string(count) + " jobs submitted simultaneously");
    }

private:
    std::ofstream log_file;
    std::mutex log_mutex;
    std::chrono::steady_clock::time_point session_start;

    // Session counters — atomic because callbacks run on worker threads
    std::atomic<int>    submitted_count{0};
    std::atomic<int>    completed_count{0};
    std::atomic<int>    failed_count{0};
    std::atomic<double> total_elapsed_ms{0.0};

    std::string timestamp() {
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm tm_buf;
        localtime_r(&t, &tm_buf);
        std::ostringstream oss;
        oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
        return oss.str();
    }

    void write_raw(const std::string& line) {
        std::lock_guard<std::mutex> lock(log_mutex);
        if (log_file.is_open()) {
            log_file << line << "\n";
            log_file.flush();
        }
    }

    void write_session_summary() {
        auto session_end = std::chrono::steady_clock::now();
        double session_sec = std::chrono::duration<double>(
            session_end - session_start).count();

        double avg_ms = completed_count > 0
            ? total_elapsed_ms.load() / completed_count
            : 0.0;

        std::ostringstream oss;
        oss << "========================================\n"
            << "  Session Summary\n"
            << "  Total submitted  : " << submitted_count  << "\n"
            << "  Completed        : " << completed_count  << "\n"
            << "  Failed           : " << failed_count     << "\n"
            << std::fixed << std::setprecision(1)
            << "  Avg finish time  : " << avg_ms           << "ms\n"
            << "  Session duration : " << session_sec      << "s\n"
            << "========================================\n";
        write_raw(oss.str());
    }
};