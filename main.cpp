#include <iostream>
#include <algorithm>
#include <string>
#include <fstream>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>
#include <syslog.h>

using namespace std;

const string WORKING_DIRECTORY = "";
const string LOG_FILENAME = "";

pid_t pid;

/**
 * @brief Log a string by appending it into the log file (LOG_FILENAME).
 * 
 * @param content The string to be logged
 */
void log(const string& content) {
    ofstream out(WORKING_DIRECTORY + LOG_FILENAME, ofstream::out | ofstream::app);
    out << content << endl;
    out.close();
}

/**
 * @brief Make the program become a daemon.
 */
void daemonize() {
    // First forking
    pid = fork();
    if (pid < 0) {
        log("First forking is unsuccessful");
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        log("Child PID: " + to_string(pid));
        exit(EXIT_SUCCESS);
    }
    if (setsid() < 0)
        exit(EXIT_FAILURE);
    
    // Second forking
    pid = fork();
    if (pid < 0) {
        log("Second forking is unsuccessful");
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        log("Grandchild PID: " + to_string(pid));
        exit(EXIT_SUCCESS);
    }
    umask(0);
    chdir(WORKING_DIRECTORY.c_str());

    int fileDescriptors = sysconf(_SC_OPEN_MAX);
    while (fileDescriptors >= 0) 
        close(fileDescriptors--);
    openlog("load-reduce-daemon", LOG_PID, LOG_DAEMON);
}

int main() {
    daemonize();
    return 0;
}