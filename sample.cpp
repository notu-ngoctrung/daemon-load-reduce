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

/*
    Intro: a program that   (1) turns itself into a linux daemon, 
                            (2) get & sort running processes by %CPU
                            (3) reach out to ChatGPT to ask about their info ("What do these processes do in Linux?")
                                res = req.post('https://api.openai.com/v1/chat/completions', json=request_body, headers=request_headers, timeout=30)
*/

/* Global configuration */
const string WORKING_DIRECTORY = filesystem::current_path().string();
const int PERIOD_MIN = 30;
const double LOAD_THRESHOLD = 10;
const int KILL_PROCESSES_LIMIT = 5;

/* Datatype to manage details of a process */
struct Process {
    pid_t pid, ppid;
    double percentCpu, virtualMem;
    string name;
    Process(pid_t _p=0, pid_t _pp=0, double _p1=0, double _v=0, string _n=""): 
        pid(_p), ppid(_pp), percentCpu(_p1), virtualMem(_v), name(_n) {}
};

pid_t pid;      // this daemon PID

/**
 * @brief Make this program become a daemon.
 */
void daemonize() {
    // First forking
    pid = fork();
    if (pid < 0) {
        // log("First forking is unsuccessful");
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        // log("Child PID: " + to_string(pid));
        exit(EXIT_SUCCESS);
    }
    if (setsid() < 0)
        exit(EXIT_FAILURE);
    
    // Second forking
    pid = fork();
    if (pid < 0) {
        // log("Second forking is unsuccessful");
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        // log("Grandchild PID: " + to_string(pid));
        exit(EXIT_SUCCESS);
    }
    umask(0);
    chdir(WORKING_DIRECTORY.c_str());

    int fileDescriptors = sysconf(_SC_OPEN_MAX);
    while (fileDescriptors >= 0) 
        close(fileDescriptors--);
    openlog("load-reduce-daemon", LOG_PID, LOG_DAEMON);
}

/**
 * @brief Get and sort the running processes by %CPU (descending order)
 * 
 * @return vector<Process> the sorted list of running processes
 */
vector<Process> getProcessesSortedByCpu() {
    vector<Process> expensiveProc;
    FILE* cmdOutput = popen("ps -eo 'pid,pcpu,vsz,ppid,comm'", "r");
    if (!cmdOutput) {
        // log("Error: cannot execute popen(ps) command.");
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
        sscanf(line, "%d %lf %lf %d %s", &curPid, &pcpu, &vsz, &ppid, cmd);
        if (curPid != pid) 
            expensiveProc.push_back(Process(curPid, ppid, pcpu, vsz, cmd));
    }
    pclose(cmdOutput);

    sort(expensiveProc.begin(), expensiveProc.end(), [](const Process &a, const Process &b) -> bool {
        return a.percentCpu > b.percentCpu;
    });
    return expensiveProc;
}

/**
 * @brief Ask ChatGPT about the process information
 * 
 * @param processes a list of processes to be asked
 * @return string ChatGPT response
 */
string getProcInfoFromChatGpt(const vector<pair<Process, int>>& processes) {
    string result = "";
    string cmd = "python3 chatgpt_crawler.py";
    for(const pair<Process, int>& p : processes)
        cmd.append(" " + p.first.name);

    char response[1024];
    FILE* cmdOutput = popen(cmd.c_str(), "r");
    // log("Attempting to crawl data from ChatGPT...");
    if (cmdOutput == nullptr) {
        // log("Failed to crawl data from ChatGPT...");
        return {};
    }
    while (fgets(response, sizeof(response), cmdOutput)) 
        result.append(string(response));
    pclose(cmdOutput);

    reverse(result.begin(), result.end());
    while (result.size() > 0 && (result.back() == '\n' || result.back() == '\r'))
        result.pop_back();
    reverse(result.begin(), result.end());
    while (result.size() > 0 && (result.back() == '\n' || result.back() == '\r'))
        result.pop_back();

    return result;
}