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

/* Global configuration */
const string WORKING_DIRECTORY = filesystem::current_path().string() + "/workdir";
const string LOG_FILENAME = "log.txt";
const string REPORT_PREF_FILENAME = "report-";
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
 * @brief Make this program become a daemon.
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

/**
 * @brief Get and sort the running processes by %CPU (descending order)
 * 
 * @return vector<Process> the sorted list of running processes
 */
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
 * @brief Attempt to kill the processes using `kill {pid}`
 * 
 * @param processes a list of running processes
 * @param lim upperbound number of processes to be killed
 * @return vector<pair<Process, int>> a list of killed processes & their status (i.e. killed, no permission, not found)
 */
vector<pair<Process, int>> killProcesses(const vector<Process>& processes, const int lim) {
    if (!sessionStatus) 
        return {};

    vector<pair<Process, int>> killed;
    for(int i = 0; i < processes.size() && killed.size() < lim; i++) {
        // int result = kill(processes[i].pid, SIGTERM);
        int result = 0;
        if (result == 0) {
            log("Killed: " + to_string(processes[i].pid) + " - " + processes[i].name);
            killed.push_back({processes[i], 0});
        }
    }
    
    return killed;
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
    log("Attempting to crawl data from ChatGPT...");
    if (cmdOutput == nullptr) {
        sessionStatus = false;
        log("Failed to crawl data from ChatGPT...");
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

/**
 * @brief Export the daemon report to HTML.
 *  The output file is named "export-{daemon-pid}.html"
 * @param killedProcs a list of killed processes & their status (killed/ no permission)
 * @param loadavg the recorded average load (1min-5mins-15mins)
 */
void reportKilledProcs(const vector<pair<Process, int>>& killedProcs, double loadavg[3]) {
    if (!sessionStatus) return;

    auto chatGptResponse = getProcInfoFromChatGpt(killedProcs);
    if (!sessionStatus) return;

    ofstream out(REPORT_PREF_FILENAME + to_string(pid) + ".html");

    time_t currentTime = time(0);
    string timeStr = asctime(localtime(&currentTime));
    out << "<!DOCTYPE html>" << endl << "<html>" << endl;
    out << "<head><title>Report PID " << pid << " - Load Reduce Daemon</title>" << endl;
    out << "<style>" << endl;
    out << "table, th, td { border: 1px solid black; border-collapse: collapse; padding: 0.3rem; text-align: center; }" << endl;
    out << "table td:first-child { text-align: left; }" << endl;
    out << "table { margin-left: auto; margin-right: auto; }" << endl;
    out << "body { padding-left: 1rem; padding-right: 1rem; padding-bottom: 2rem; }" << endl;
    out << "</style></head>" << endl;
    out << "<body>" << endl;
    out << "<h1 style=\"text-align: center;\">Load Reduce Daemon</h1>" << endl;
    out << "<div style=\"text-align: center;\">Current Local Time: " << timeStr << "</div>" << endl;
    out << "<h2>Report</h2>" << endl;
    out << "<div>Average Load:</div><ul>" << endl;
    for(int i = 0; i < 3; i++) {
        if (i == 0) out << "<li>1 minute: " << setprecision(2) << loadavg[i] << "</li>" << endl;
        if (i == 1) out << "<li>5 minutes: " << setprecision(2) << loadavg[i] << "</li>" << endl;
        if (i == 2) out << "<li>15 minutes: <b>" << setprecision(2) << loadavg[i] << "</b></li></ul>" << endl;
    }
    out << "<table><tr><th>Process</th><th>PID</th><th>%CPU</th><th>PPID</th><th>Virtual Memory (in KiB)</th><th>Status</th>" << endl;
    for(const pair<Process, int>& it : killedProcs) {
        Process proc = it.first;
        int status = it.second;
        out << "<tr><td>" << proc.name << "</td><td>" << proc.pid << "</td><td>" << fixed << setprecision(1) << proc.percentCpu 
            << "</td><td>" << proc.ppid << "</td><td>" << setprecision(2) << proc.virtualMem << "</td>";
        out << "<td style=\"background: lightgreen\">Killed</td>";
        out << "</tr>" << endl;
    }
    out << "</table>" << endl;

    out << "<h2>Process Information</h2>" << endl;
    out << "<p>What could these processes do in Linux? Let's hear advice from the famous ChatGPT</p>" << endl;
    out << "<div style=\"margin-left: auto; margin-right: auto; line-height: 1.2rem; width: 90%; max-width: 1100px; background-color: lightgrey; padding: 0.5rem 0.7rem 0.5rem 0.7rem; border-radius: 12px;\">";
    out << "<pre style=\"white-space: pre-wrap;\">" << chatGptResponse << "</pre></div>" << endl;
    // out << "<ul>";
    // for(const string& s : chatGptResponses) 
    //     out << "<li>" << s << "</li>" << endl;
    // out << "</ul>" << endl;
    out << "</body>" << endl << "</html>";
    out.close();
}


int main() {
    // daemonize();

    chrono::minutes sleepPeriod(2);
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
        if (loadavg[2] <= LOAD_THRESHOLD) {
            log("The load is " + to_string(loadavg[2]) + " <= " + to_string(LOAD_THRESHOLD) + ". No further actions needed");
            continue;
        }

        vector<Process> sortedProcsByCpu = getProcessesSortedByCpu();
        vector<pair<Process, int>> killedProcs = killProcesses(sortedProcsByCpu, KILL_PROCESSES_LIMIT);
        reportKilledProcs(killedProcs, loadavg);

        if (!sessionStatus) {
            log("Closing current session.");
            continue;
        }

        return 0;
    }
    return 0;
}