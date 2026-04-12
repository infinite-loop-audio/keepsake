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
#include <signal.h>
#include <spawn.h>
#include <sys/mman.h>
#include <sys/wait.h>
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

#include "platform_process.h"
#include "platform_io.h"
#include "platform_shm.h"
#include "platform_plugins.h"
