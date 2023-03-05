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
#include <ctime>
#include <chrono>
#include <thread>

using namespace std;

const string WORKING_DIRECTORY = filesystem::current_path().string() + "/workdir";
const string LOG_FILENAME = "log.txt";
const double LOAD_THRESHOLD = 20;
const int CPU_COMSUMING_PROCESSES_LIMIT = 10;

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

vector<Process> getCpuExpensiveProcesses(const int limit) {
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
    expensiveProc.resize(min(limit, (int)expensiveProc.size()));
    return expensiveProc;
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

        vector<Process> cpuExpensiveProc = getCpuExpensiveProcesses(CPU_COMSUMING_PROCESSES_LIMIT);
        // killProcesses(cpuExpensiveProc);
        if (!sessionStatus) {
            log("Closing current session.");
            continue;
        }
        return 0;
    }
    return 0;
}