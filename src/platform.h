#pragma once
//
// Keepsake platform abstraction — process spawning, shared memory, pipe I/O.
// Provides a uniform interface across macOS, Linux, and Windows.
//

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <string>

// ============================================================
// Types
// ============================================================

#ifdef _WIN32
#include <windows.h>
typedef HANDLE PlatformPipe;
typedef DWORD  PlatformPid;
#define PLATFORM_INVALID_PIPE INVALID_HANDLE_VALUE
#else
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
typedef int   PlatformPipe;
typedef pid_t PlatformPid;
#define PLATFORM_INVALID_PIPE (-1)
#endif

#ifndef _WIN32
static constexpr int PLATFORM_BRIDGE_IPC_OUT_FD = 4;
#endif

struct PlatformProcess {
    PlatformPid pid;
    PlatformPipe pipe_to;    // host writes commands here (non-RT)
    PlatformPipe pipe_from;  // host reads responses here (non-RT)
    PlatformPipe wake_to;    // host writes 1-byte wake signal (RT, audio thread)
    PlatformPipe wake_from;  // bridge reads wake signal
#ifdef _WIN32
    HANDLE process_handle;
#endif
};

struct PlatformShm {
    std::string name;
    void *ptr = nullptr;
    size_t size = 0;
    bool owner = false;
#ifdef _WIN32
    HANDLE map_handle = INVALID_HANDLE_VALUE;
#else
    int fd = -1;
#endif
};

// ============================================================
// Process spawning
// ============================================================

#ifdef _WIN32

static inline bool platform_spawn(const std::string &binary,
                                    PlatformProcess &proc) {
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE child_stdin_rd, child_stdin_wr;
    HANDLE child_stdout_rd, child_stdout_wr;

    if (!CreatePipe(&child_stdin_rd, &child_stdin_wr, &sa, 0)) return false;
    SetHandleInformation(child_stdin_wr, HANDLE_FLAG_INHERIT, 0);

    if (!CreatePipe(&child_stdout_rd, &child_stdout_wr, &sa, 0)) {
        CloseHandle(child_stdin_rd); CloseHandle(child_stdin_wr);
        return false;
    }
    SetHandleInformation(child_stdout_rd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = child_stdin_rd;
    si.hStdOutput = child_stdout_wr;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION pi = {};
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "\"%s\"", binary.c_str());

    if (!CreateProcessA(nullptr, cmd, nullptr, nullptr, TRUE,
                        0, nullptr, nullptr, &si, &pi)) {
        CloseHandle(child_stdin_rd); CloseHandle(child_stdin_wr);
        CloseHandle(child_stdout_rd); CloseHandle(child_stdout_wr);
        return false;
    }

    CloseHandle(child_stdin_rd);
    CloseHandle(child_stdout_wr);
    CloseHandle(pi.hThread);

    proc.pid = pi.dwProcessId;
    proc.pipe_to = child_stdin_wr;
    proc.pipe_from = child_stdout_rd;
    proc.process_handle = pi.hProcess;
    return true;
}

static inline void platform_kill(PlatformProcess &proc) {
    if (proc.pipe_to != PLATFORM_INVALID_PIPE) {
        CloseHandle(proc.pipe_to); proc.pipe_to = PLATFORM_INVALID_PIPE;
    }
    if (proc.pipe_from != PLATFORM_INVALID_PIPE) {
        CloseHandle(proc.pipe_from); proc.pipe_from = PLATFORM_INVALID_PIPE;
    }
    if (proc.process_handle != INVALID_HANDLE_VALUE) {
        WaitForSingleObject(proc.process_handle, 500);
        TerminateProcess(proc.process_handle, 1);
        WaitForSingleObject(proc.process_handle, 1000);
        CloseHandle(proc.process_handle);
        proc.process_handle = INVALID_HANDLE_VALUE;
    }
}

static inline void platform_force_kill(PlatformProcess &proc) {
    if (proc.pipe_to != PLATFORM_INVALID_PIPE) {
        CloseHandle(proc.pipe_to); proc.pipe_to = PLATFORM_INVALID_PIPE;
    }
    if (proc.pipe_from != PLATFORM_INVALID_PIPE) {
        CloseHandle(proc.pipe_from); proc.pipe_from = PLATFORM_INVALID_PIPE;
    }
    if (proc.process_handle != INVALID_HANDLE_VALUE) {
        TerminateProcess(proc.process_handle, 1);
        WaitForSingleObject(proc.process_handle, 1000);
        CloseHandle(proc.process_handle);
        proc.process_handle = INVALID_HANDLE_VALUE;
    }
}

#else // POSIX (macOS + Linux)

#include <signal.h>
#include <sys/wait.h>
#include <spawn.h>

extern char **environ;

static inline bool platform_move_fd(PlatformPipe &fd, int min_fd) {
    if (fd < min_fd) {
        int moved = fcntl(fd, F_DUPFD, min_fd);
        if (moved < 0) return false;
        close(fd);
        fd = moved;
    }
    return true;
}

static inline bool platform_spawn(const std::string &binary,
                                    PlatformProcess &proc) {
    int to_child[2], from_child[2], wake[2];
    if (pipe(to_child) < 0 || pipe(from_child) < 0 || pipe(wake) < 0) {
        perror("keepsake: pipe");
        return false;
    }

    // Keep raw pipe fds away from stdio and fd 4. posix_spawn file actions
    // apply dup2/close to numeric descriptors, so low-fd collisions can
    // accidentally close the bridge IPC target after dup2.
    if (!platform_move_fd(to_child[0], 10) ||
        !platform_move_fd(to_child[1], 10) ||
        !platform_move_fd(from_child[0], 10) ||
        !platform_move_fd(from_child[1], 10) ||
        !platform_move_fd(wake[0], 10) ||
        !platform_move_fd(wake[1], 10)) {
        perror("keepsake: fcntl");
        close(to_child[0]); close(to_child[1]);
        close(from_child[0]); close(from_child[1]);
        close(wake[0]); close(wake[1]);
        return false;
    }

    // Use posix_spawn — fork() deadlocks in multithreaded hosts (REAPER)
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, to_child[0], STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, from_child[1],
                                     PLATFORM_BRIDGE_IPC_OUT_FD);
    posix_spawn_file_actions_adddup2(&actions, STDERR_FILENO, STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, to_child[1]);
    posix_spawn_file_actions_addclose(&actions, from_child[0]);
    posix_spawn_file_actions_addclose(&actions, wake[1]);

    char wake_fd_str[16];
    snprintf(wake_fd_str, sizeof(wake_fd_str), "%d", wake[0]);
    char *argv[] = {
        const_cast<char *>("keepsake-bridge"),
        wake_fd_str,
        nullptr
    };

    pid_t pid;
    int err = posix_spawn(&pid, binary.c_str(), &actions, nullptr,
                           argv, environ);
    posix_spawn_file_actions_destroy(&actions);

    close(to_child[0]);
    close(from_child[1]);
    close(wake[0]);

    if (err != 0) {
        fprintf(stderr, "keepsake: posix_spawn failed: %s\n", strerror(err));
        close(to_child[1]); close(from_child[0]); close(wake[1]);
        return false;
    }

    proc.pid = pid;
    proc.pipe_to = to_child[1];
    proc.pipe_from = from_child[0];
    proc.wake_to = wake[1];
    proc.wake_from = -1;
    return true;
}

static inline void platform_kill(PlatformProcess &proc) {
    if (proc.pipe_to != PLATFORM_INVALID_PIPE) {
        close(proc.pipe_to); proc.pipe_to = PLATFORM_INVALID_PIPE;
    }
    if (proc.pipe_from != PLATFORM_INVALID_PIPE) {
        close(proc.pipe_from); proc.pipe_from = PLATFORM_INVALID_PIPE;
    }
    if (proc.pid > 0) {
        int status;
        pid_t r = waitpid(proc.pid, &status, WNOHANG);
        if (r == 0) {
            kill(proc.pid, SIGTERM);
            usleep(100000);
            r = waitpid(proc.pid, &status, WNOHANG);
            if (r == 0) {
                kill(proc.pid, SIGKILL);
                waitpid(proc.pid, &status, 0);
            }
        }
        proc.pid = -1;
    }
}

static inline void platform_force_kill(PlatformProcess &proc) {
    if (proc.pipe_to != PLATFORM_INVALID_PIPE) {
        close(proc.pipe_to); proc.pipe_to = PLATFORM_INVALID_PIPE;
    }
    if (proc.pipe_from != PLATFORM_INVALID_PIPE) {
        close(proc.pipe_from); proc.pipe_from = PLATFORM_INVALID_PIPE;
    }
    if (proc.wake_to != PLATFORM_INVALID_PIPE) {
        close(proc.wake_to); proc.wake_to = PLATFORM_INVALID_PIPE;
    }
    if (proc.wake_from != PLATFORM_INVALID_PIPE) {
        close(proc.wake_from); proc.wake_from = PLATFORM_INVALID_PIPE;
    }
    if (proc.pid > 0) {
        kill(proc.pid, SIGKILL);
        waitpid(proc.pid, nullptr, 0);
        proc.pid = -1;
    }
}

#endif

static inline bool platform_process_alive(const PlatformProcess &proc) {
#ifdef _WIN32
    if (proc.process_handle == INVALID_HANDLE_VALUE) return false;
    DWORD rc = WaitForSingleObject(proc.process_handle, 0);
    return rc == WAIT_TIMEOUT;
#else
    if (proc.pid <= 0) return false;
    if (kill(proc.pid, 0) == 0) return true;
    return errno == EPERM;
#endif
}

// ============================================================
// Pipe I/O (cross-platform)
// ============================================================

static inline bool platform_write(PlatformPipe p, const void *buf, size_t n) {
#ifdef _WIN32
    const uint8_t *ptr = static_cast<const uint8_t *>(buf);
    size_t rem = n;
    while (rem > 0) {
        DWORD written;
        if (!WriteFile(p, ptr, static_cast<DWORD>(rem), &written, nullptr))
            return false;
        ptr += written;
        rem -= written;
    }
    return true;
#else
    const uint8_t *ptr = static_cast<const uint8_t *>(buf);
    size_t rem = n;
    while (rem > 0) {
        ssize_t w = write(p, ptr, rem);
        if (w <= 0) return false;
        ptr += w;
        rem -= static_cast<size_t>(w);
    }
    return true;
#endif
}

static inline bool platform_read(PlatformPipe p, void *buf, size_t n) {
#ifdef _WIN32
    uint8_t *ptr = static_cast<uint8_t *>(buf);
    size_t rem = n;
    while (rem > 0) {
        DWORD nread;
        if (!ReadFile(p, ptr, static_cast<DWORD>(rem), &nread, nullptr) || nread == 0)
            return false;
        ptr += nread;
        rem -= nread;
    }
    return true;
#else
    uint8_t *ptr = static_cast<uint8_t *>(buf);
    size_t rem = n;
    while (rem > 0) {
        ssize_t r = read(p, ptr, rem);
        if (r <= 0) return false;
        ptr += r;
        rem -= static_cast<size_t>(r);
    }
    return true;
#endif
}

// Timed read: returns false on timeout or error.
static inline bool platform_read_ready(PlatformPipe p, int timeout_ms) {
#ifdef _WIN32
    if (timeout_ms < 0) return true; // block forever handled by ReadFile
    // PeekNamedPipe for anonymous pipes
    DWORD avail = 0;
    DWORD elapsed = 0;
    while (elapsed < static_cast<DWORD>(timeout_ms)) {
        if (PeekNamedPipe(p, nullptr, 0, nullptr, &avail, nullptr) && avail > 0)
            return true;
        Sleep(1);
        elapsed++;
    }
    return false;
#else
    if (timeout_ms < 0) return true;
    struct pollfd pfd = { p, POLLIN, 0 };
    int r = poll(&pfd, 1, timeout_ms);
    return r > 0 && (pfd.revents & POLLIN);
#endif
}

// ============================================================
// Shared memory (cross-platform)
// ============================================================

#ifdef _WIN32

static inline bool platform_shm_create(PlatformShm &s, const std::string &name,
                                         size_t size) {
    s.map_handle = CreateFileMappingA(
        INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
        0, static_cast<DWORD>(size), name.c_str());
    if (!s.map_handle) return false;

    s.ptr = MapViewOfFile(s.map_handle, FILE_MAP_ALL_ACCESS, 0, 0, size);
    if (!s.ptr) { CloseHandle(s.map_handle); return false; }

    memset(s.ptr, 0, size);
    s.name = name;
    s.size = size;
    s.owner = true;
    return true;
}

static inline bool platform_shm_open(PlatformShm &s, const std::string &name,
                                       size_t size) {
    s.map_handle = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
    if (!s.map_handle) return false;

    s.ptr = MapViewOfFile(s.map_handle, FILE_MAP_ALL_ACCESS, 0, 0, size);
    if (!s.ptr) { CloseHandle(s.map_handle); return false; }

    s.name = name;
    s.size = size;
    s.owner = false;
    return true;
}

static inline void platform_shm_close(PlatformShm &s) {
    if (s.ptr) UnmapViewOfFile(s.ptr);
    if (s.map_handle != INVALID_HANDLE_VALUE) CloseHandle(s.map_handle);
    s.ptr = nullptr;
    s.map_handle = INVALID_HANDLE_VALUE;
    s.owner = false;
}

#else // POSIX

#include <fcntl.h>
#include <sys/mman.h>

static inline bool platform_shm_create(PlatformShm &s, const std::string &name,
                                         size_t size) {
    shm_unlink(name.c_str());
    int fd = shm_open(name.c_str(), O_CREAT | O_RDWR, 0600);
    if (fd < 0) { perror("keepsake: shm_open"); return false; }
    if (ftruncate(fd, static_cast<off_t>(size)) < 0) {
        perror("keepsake: ftruncate");
        close(fd); shm_unlink(name.c_str());
        return false;
    }
    void *ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
        perror("keepsake: mmap");
        close(fd); shm_unlink(name.c_str());
        return false;
    }
    memset(ptr, 0, size);
    s = { name, ptr, size, true, fd };
    return true;
}

static inline bool platform_shm_open(PlatformShm &s, const std::string &name,
                                       size_t size) {
    int fd = shm_open(name.c_str(), O_RDWR, 0600);
    if (fd < 0) { perror("keepsake: shm_open"); return false; }
    void *ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
        perror("keepsake: mmap"); close(fd);
        return false;
    }
    s = { name, ptr, size, false, fd };
    return true;
}

static inline void platform_shm_close(PlatformShm &s) {
    if (s.ptr && s.ptr != MAP_FAILED) munmap(s.ptr, s.size);
    if (s.fd >= 0) close(s.fd);
    if (s.owner && !s.name.empty()) shm_unlink(s.name.c_str());
    s.ptr = nullptr;
    s.fd = -1;
    s.owner = false;
}

#endif

// ============================================================
// Shared memory naming
// ============================================================

#ifdef _WIN32
// Windows: no leading slash, use "Local\" prefix for session-local
static inline std::string platform_shm_name(const char *suffix) {
    char buf[128];
    snprintf(buf, sizeof(buf), "Local\\keepsake-%lu-%s",
             static_cast<unsigned long>(GetCurrentProcessId()), suffix);
    return buf;
}
#else
static inline std::string platform_shm_name(const char *suffix) {
    char buf[128];
    snprintf(buf, sizeof(buf), "/keepsake-%d-%s", getpid(), suffix);
    return buf;
}
#endif

// ============================================================
// VST2 plugin file extension
// ============================================================

static inline const char *platform_vst2_extension() {
#ifdef __APPLE__
    return ".vst";
#elif defined(_WIN32)
    return ".dll";
#else
    return ".so";
#endif
}

// Is this path a VST2 plugin candidate?
static inline bool platform_is_vst2(const std::string &path, bool is_dir) {
#ifdef __APPLE__
    // macOS: .vst bundles are directories
    return is_dir && path.size() >= 4 &&
           path.substr(path.size() - 4) == ".vst";
#elif defined(_WIN32)
    return !is_dir && path.size() >= 4 &&
           path.substr(path.size() - 4) == ".dll";
#else
    return !is_dir && path.size() >= 3 &&
           path.substr(path.size() - 3) == ".so";
#endif
}
