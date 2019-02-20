#include "logtypes.hpp"

using namespace std;

void LogJournal::addEntry(string user, LogEntry entry) {
    static int count = 0;
    logs[user].push_back(entry);
    count++;
}

vector<LogEntry> LogJournal::getEntriesForUser(string user) {
    auto vec = logs[user]; // copy
    return vec;
}