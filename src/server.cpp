#include <iostream>

#include "logtypes.hpp"

using namespace std;

int main() {

    LogFileManager lfm;

    for (int i=0; i<5; i++)
        lfm.writeLogEntry("user1", LogEntry("entry"));

    return 0;
}