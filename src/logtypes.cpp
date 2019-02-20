#include <system_error>
#include <fstream>
#include <iostream> // maybe dont print to stderr here and return error codes instead

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

// write all LogEntries from user in journal into file and delete from journal when done
void LogFileWriter::writeLogForUser(LogJournal& journal, const string user) {

    if (journal.logs.empty()) return;

    // check if entry for user
    if (journal.logs.find(user) == journal.logs.end()) return;

    // create dir for logs if needed
    error_code err;
    filesystem::create_directory(journal.logdir, err); // does nothing if exists
    if (err) {
        cerr << "error creating log directory: " << err << endl;
        return;
    }

    auto logfile_path = journal.logdir;
    logfile_path /= "logfile_"+user;  // log/ -> log/logfile_userX

    ofstream logfile (logfile_path, ios::app);
    if (!logfile) {
        cerr << "error opening " << logfile_path.string() << endl;
        return;
    }

    // todo: make timestamp human readable and put in front
    for (LogEntry le : journal.logs[user]) {
        logfile << le.text << "\n";
    }
    journal.logs.erase(user);
}