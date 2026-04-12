#pragma once

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#elif defined(__linux__)
#include <fstream>
#include <sstream>
#include <sys/resource.h>
#endif

namespace host_test_support {

static constexpr const char *kWorkerEnv = "KEEPSAKE_HOST_TEST_WORKER";
static constexpr int kMemoryPollIntervalUs = 50 * 1000;

static inline bool in_worker_mode() {
    const char *value = getenv(kWorkerEnv);
    return value && strcmp(value, "1") == 0;
}

static inline bool apply_best_effort_memory_limit_mb(int mb) {
    if (mb <= 0) return true;

#if defined(__APPLE__)
    (void)mb;
    return true;
#else
    rlim_t bytes = static_cast<rlim_t>(mb) * 1024u * 1024u;
    struct rlimit lim = { bytes, bytes };
    bool ok = true;
#ifdef RLIMIT_AS
    if (setrlimit(RLIMIT_AS, &lim) != 0) ok = false;
#endif
#ifdef RLIMIT_DATA
    if (setrlimit(RLIMIT_DATA, &lim) != 0) ok = false;
#endif
#ifdef RLIMIT_RSS
    if (setrlimit(RLIMIT_RSS, &lim) != 0) ok = false;
#endif
    return ok;
#endif
}

static inline bool read_process_memory_bytes(pid_t pid, uint64_t &bytes_out) {
#ifdef __APPLE__
    rusage_info_v4 info = {};
    if (proc_pid_rusage(pid, RUSAGE_INFO_V4,
                        reinterpret_cast<rusage_info_t *>(&info)) != 0) {
        return false;
    }
    bytes_out = info.ri_phys_footprint > 0 ? info.ri_phys_footprint
                                           : info.ri_resident_size;
    return true;
#elif defined(__linux__)
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    std::ifstream f(path);
    if (!f.is_open()) return false;

    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("VmRSS:", 0) != 0) continue;
        std::istringstream ss(line.substr(6));
        uint64_t kb = 0;
        ss >> kb;
        bytes_out = kb * 1024u;
        return true;
    }
    return false;
#else
    (void)pid;
    (void)bytes_out;
    return false;
#endif
}

static inline int decode_wait_status(int status) {
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 1;
}

static inline int supervise_worker(pid_t pid, int max_memory_mb) {
    const uint64_t max_bytes = static_cast<uint64_t>(max_memory_mb) * 1024u * 1024u;
    uint64_t peak_bytes = 0;

    for (;;) {
        int status = 0;
        pid_t waited = waitpid(pid, &status, WNOHANG);
        if (waited == pid) return decode_wait_status(status);
        if (waited < 0) {
            fprintf(stderr, "host-test: waitpid failed: %s\n", strerror(errno));
            return 1;
        }

        uint64_t current_bytes = 0;
        if (read_process_memory_bytes(pid, current_bytes)) {
            if (current_bytes > peak_bytes) peak_bytes = current_bytes;
            if (max_memory_mb > 0 && current_bytes > max_bytes) {
                fprintf(stderr,
                        "FAIL: worker memory exceeded %d MB (current %.1f MB, peak %.1f MB); killing test process\n",
                        max_memory_mb,
                        current_bytes / (1024.0 * 1024.0),
                        peak_bytes / (1024.0 * 1024.0));
                kill(pid, SIGKILL);
                waitpid(pid, &status, 0);
                return 124;
            }
        }

        usleep(kMemoryPollIntervalUs);
    }
}

static inline int run_supervised(int argc, char *argv[], int max_memory_mb,
                                 int (*worker_main)(int, char **)) {
    if (in_worker_mode()) {
        if (!apply_best_effort_memory_limit_mb(max_memory_mb)) {
            fprintf(stderr,
                    "warning: failed to apply best-effort memory limit (%d MB)\n",
                    max_memory_mb);
        }
        return worker_main(argc, argv);
    }

    if (max_memory_mb <= 0) return worker_main(argc, argv);

    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "host-test: fork failed: %s\n", strerror(errno));
        return 1;
    }
    if (pid == 0) {
        setenv(kWorkerEnv, "1", 1);
        execvp(argv[0], argv);
        fprintf(stderr, "host-test: exec failed: %s\n", strerror(errno));
        _exit(127);
    }

    fprintf(stderr, "host-test: supervising worker pid=%d with %d MB cap\n",
            static_cast<int>(pid), max_memory_mb);
    return supervise_worker(pid, max_memory_mb);
}

} // namespace host_test_support
