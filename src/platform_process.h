#pragma once
//
// Keepsake platform abstraction — process spawning and lifecycle.
//

#ifdef _WIN32

static inline bool platform_spawn(const std::string &binary,
                                  PlatformProcess &proc) {
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE child_stdin_rd, child_stdin_wr;
    HANDLE ipc_rd, ipc_wr;

    if (!CreatePipe(&child_stdin_rd, &child_stdin_wr, &sa, 0)) return false;
    SetHandleInformation(child_stdin_wr, HANDLE_FLAG_INHERIT, 0);

    if (!CreatePipe(&ipc_rd, &ipc_wr, &sa, 0)) {
        CloseHandle(child_stdin_rd);
        CloseHandle(child_stdin_wr);
        return false;
    }
    SetHandleInformation(ipc_rd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = child_stdin_rd;
    si.hStdOutput = GetStdHandle(STD_ERROR_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION pi = {};
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "\"%s\" %llu", binary.c_str(),
             static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(ipc_wr)));

    if (!CreateProcessA(nullptr, cmd, nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(child_stdin_rd);
        CloseHandle(child_stdin_wr);
        CloseHandle(ipc_rd);
        CloseHandle(ipc_wr);
        return false;
    }

    CloseHandle(child_stdin_rd);
    CloseHandle(ipc_wr);
    CloseHandle(pi.hThread);

    proc.pid = pi.dwProcessId;
    proc.pipe_to = child_stdin_wr;
    proc.pipe_from = ipc_rd;
    proc.process_handle = pi.hProcess;
    return true;
}

static inline void platform_kill(PlatformProcess &proc) {
    if (proc.pipe_to != PLATFORM_INVALID_PIPE) {
        CloseHandle(proc.pipe_to);
        proc.pipe_to = PLATFORM_INVALID_PIPE;
    }
    if (proc.pipe_from != PLATFORM_INVALID_PIPE) {
        CloseHandle(proc.pipe_from);
        proc.pipe_from = PLATFORM_INVALID_PIPE;
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
        CloseHandle(proc.pipe_to);
        proc.pipe_to = PLATFORM_INVALID_PIPE;
    }
    if (proc.pipe_from != PLATFORM_INVALID_PIPE) {
        CloseHandle(proc.pipe_from);
        proc.pipe_from = PLATFORM_INVALID_PIPE;
    }
    if (proc.process_handle != INVALID_HANDLE_VALUE) {
        TerminateProcess(proc.process_handle, 1);
        WaitForSingleObject(proc.process_handle, 1000);
        CloseHandle(proc.process_handle);
        proc.process_handle = INVALID_HANDLE_VALUE;
    }
}

#else

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

    if (!platform_move_fd(to_child[0], 10) ||
        !platform_move_fd(to_child[1], 10) ||
        !platform_move_fd(from_child[0], 10) ||
        !platform_move_fd(from_child[1], 10) ||
        !platform_move_fd(wake[0], 10) ||
        !platform_move_fd(wake[1], 10)) {
        perror("keepsake: fcntl");
        close(to_child[0]);
        close(to_child[1]);
        close(from_child[0]);
        close(from_child[1]);
        close(wake[0]);
        close(wake[1]);
        return false;
    }

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
    int err = posix_spawn(&pid, binary.c_str(), &actions, nullptr, argv, environ);
    posix_spawn_file_actions_destroy(&actions);

    close(to_child[0]);
    close(from_child[1]);
    close(wake[0]);

    if (err != 0) {
        fprintf(stderr, "keepsake: posix_spawn failed: %s\n", strerror(err));
        close(to_child[1]);
        close(from_child[0]);
        close(wake[1]);
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
        close(proc.pipe_to);
        proc.pipe_to = PLATFORM_INVALID_PIPE;
    }
    if (proc.pipe_from != PLATFORM_INVALID_PIPE) {
        close(proc.pipe_from);
        proc.pipe_from = PLATFORM_INVALID_PIPE;
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
        close(proc.pipe_to);
        proc.pipe_to = PLATFORM_INVALID_PIPE;
    }
    if (proc.pipe_from != PLATFORM_INVALID_PIPE) {
        close(proc.pipe_from);
        proc.pipe_from = PLATFORM_INVALID_PIPE;
    }
    if (proc.wake_to != PLATFORM_INVALID_PIPE) {
        close(proc.wake_to);
        proc.wake_to = PLATFORM_INVALID_PIPE;
    }
    if (proc.wake_from != PLATFORM_INVALID_PIPE) {
        close(proc.wake_from);
        proc.wake_from = PLATFORM_INVALID_PIPE;
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
