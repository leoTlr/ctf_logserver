#include <system_error>
#include <fstream>
#include <iostream> // maybe dont print to stderr here and return error codes instead

#include "logtypes.hpp"

using namespace std;

// write all LogEntries from user in LogQueue into file and delete from LogQueue when done
void LogFileManager::writeLogEntry(const string user, LogEntry entry) {

    // create dir for logs if needed
    error_code err;
    filesystem::create_directory(logdir, err); // does nothing if exists
    if (err) {
        cerr << "error creating log directory: " << err << endl;
        return;
    }

    auto logfile_path = logdir;
    logfile_path /= "logfile_"+user;  // log/ -> log/logfile_userX

    ofstream logfile (logfile_path, ios::app);
    if (!logfile) {
        cerr << "error opening " << logfile_path.string() << endl;
        return;
    }

    // todo: make timestamp human readable and put in front
    logfile << entry.text << endl;
    logfile.close();
}