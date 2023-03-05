#include <iostream>
#include <algorithm>
#include <vector>
#include <string>
#include <fstream>
#include <filesystem>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>
#include <syslog.h>
#include <signal.h>
#include <ctime>
#include <chrono>
#include <thread>

using namespace std;

const string WORKING_DIRECTORY = filesystem::current_path().string() + "/workdir";
const string LOG_FILENAME = "log.txt";
const double LOAD_THRESHOLD = 20;
const int KILL_PROCESSES_LIMIT = 10;

struct Process {
    pid_t pid, ppid;
    double percentCpu, virtualMem;
    string name;
    Process(pid_t _p=0, pid_t _pp=0, double _p1=0, double _v=0, string _n=""): 
        pid(_p), ppid(_pp), percentCpu(_p1), virtualMem(_v), name(_n) {}
};

pid_t pid;
bool sessionStatus;     // true if the current session is doing ok; false otherwise

/**
 * @brief Log a string by appending it into the log file (LOG_FILENAME).
 * 
 * @param content The string to be logged
 */
void log(const string& content) {
    time_t currentTime = time(0);
    string timeStr = asctime(localtime(&currentTime));
    timeStr.pop_back();     // remove newline char solely for formatting purpose
    ofstream out(WORKING_DIRECTORY + "/" + LOG_FILENAME, ofstream::out | ofstream::app);
    out << timeStr << "\t" << content << endl;
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

vector<Process> getProcessesSortedByCpu() {
    vector<Process> expensiveProc;
    FILE* cmdOutput = popen("ps -eo 'pid,pcpu,vsz,ppid,comm'", "r");
    if (!cmdOutput) {
        log("Error: cannot execute popen(ps) command.");
        sessionStatus = false;
        return {};
    }

    char line[4096];

    // Skip the first line
    fgets(line, sizeof(line), cmdOutput);
    // Start reading
    while (fgets(line, sizeof(line), cmdOutput)) {
        int curPid, ppid;
        double pcpu, vsz;
        char cmd[1024];
        sscanf(line, "%d %lf %lf %d %s", &curPid, &pcpu, &vsz, &ppid, &cmd);
        if (curPid != pid) 
            expensiveProc.push_back(Process(pid, ppid, pcpu, vsz, cmd));
    }
    pclose(cmdOutput);

    sort(expensiveProc.begin(), expensiveProc.end(), [](const Process &a, const Process &b) -> bool {
        return a.percentCpu > b.percentCpu;
    });
    return expensiveProc;
}

vector<Process> killProcesses(const vector<Process>& processes, const int lim) {
    if (!sessionStatus) 
        return;

    vector<Process> killed;
    for(int i = 0; i < processes.size() && killed.size() < lim; i++) {
        int result = kill(processes[i].pid, SIGTERM);
        if (result == 0) {
            log("Killed: " + to_string(processes[i].pid) + " - " + processes[i].name);
            killed.push_back(processes[i]);
        } 
        else if (result == EPERM) 
            log("Killing not permitted: " + to_string(processes[i].pid) + " - " + processes[i].name);
        else if (result == ESRCH)
            log("PID not found to kill: " + to_string(processes[i].pid) + " - " + processes[i].name);
    }
    
    return killed;
}

int main() {
    // daemonize();

    chrono::seconds sleepPeriod(10);
    chrono::system_clock::time_point nextRunTime = chrono::system_clock::now();

    double loadavg[3];
    while (true) {
        this_thread::sleep_until(nextRunTime);
        nextRunTime += sleepPeriod;

        log("-------------------------------------------");
        log("Hello, I woke up :)");
        sessionStatus = true;
        if (getloadavg(loadavg, 3) == -1) {
            log("Cannot get the system load. Will try again in the next run");
            continue;
        }
        // if (loadavg[2] <= LOAD_THRESHOLD) {
        //     log("The load is " + to_string(loadavg[2]) + " <= " + to_string(LOAD_THRESHOLD) + ". No further actions needed");
        //     continue;
        // }

        vector<Process> sortedProcsByCpu = getProcessesSortedByCpu();
        vector<Process> killedProcs = killProcesses(sortedProcsByCpu, KILL_PROCESSES_LIMIT);
        if (!sessionStatus) {
            log("Closing current session.");
            continue;
        }
        return 0;
    }
    return 0;
}