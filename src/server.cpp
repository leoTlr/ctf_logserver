#include <iostream>

#include "logtypes.hpp"

int main() {

    LogJournal journal ("journal1");

    for (int i=0; i<5; i++)
        journal.addEntry("user1", LogEntry("entry"));

    auto user1_entries = journal.getEntriesForUser("user1");
    for (LogEntry le : user1_entries) {
        std::cout << le.text << std::endl;
    }

    return 0;
}