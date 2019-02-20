#ifndef LOGTYPES_HPP
#define LOGTYPES_HPP

#include <string>
#include <unordered_map>
#include <vector>
#include <chrono>

struct LogEntry;
struct LogJournal;
struct LogFileWriter;

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

// stores entries mapped to users
struct LogJournal {
    std::string title;
    std::unordered_map<std::string, std::vector<LogEntry>> logs; // mapping user->logentries 

    LogJournal(std::string title) :
        title{title}
    {};

    std::vector<LogEntry> getEntriesForUser(std::string user);
    void addEntry(std::string user, LogEntry entry);

    private:
    LogJournal(); // title needs to be set
};

// responsible for writing LogEntries from LogJournal into Files on per-user basis
struct LogFileWriter {
    static void writeLogForUser(const LogJournal& journal, const std::string& user);
};

#endif // LOGTYPES_HPP