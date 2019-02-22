#ifndef LOGTYPES_HPP
#define LOGTYPES_HPP

#include <string>
#include <vector>
#include <chrono>
#include <filesystem>

struct LogEntry;
class LogFileManager;

// holds a single entry, not bound to specific user
struct LogEntry {
    std::string text;
    std::chrono::system_clock::time_point timestamp;

    LogEntry(std::string text) :
        text{text},
        timestamp{std::chrono::system_clock::now()}
    {};

    LogEntry(std::string text, std::chrono::system_clock::time_point timestamp) :
        text{text},
        timestamp{timestamp}
    {};

    private:
    LogEntry(); // timestamp needs to be created or set
};

// todo: make reading/writing threaded or async
class LogFileManager {
    // file locks etc

    std::filesystem::path logdir; 

public:
    LogFileManager() :
        logdir {std::filesystem::path("./logfiles/")}
    {};
    LogFileManager(std::filesystem::path logdir) :
        logdir {logdir}
    {};

    void writeLogEntry (const std::string user, const LogEntry entry);
    std::vector<LogEntry>& getLogEntries(const std::string user);
};

#endif // LOGTYPES_HPP