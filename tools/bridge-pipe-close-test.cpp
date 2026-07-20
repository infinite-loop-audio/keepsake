#include "platform.h"

#include <cstdio>

#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <bridge-binary>\n", argv[0]);
        return 2;
    }

    PlatformProcess process = {};
    process.pid = -1;
    process.pipe_to = PLATFORM_INVALID_PIPE;
    process.pipe_from = PLATFORM_INVALID_PIPE;
    process.wake_to = PLATFORM_INVALID_PIPE;
    process.wake_from = PLATFORM_INVALID_PIPE;
    if (!platform_spawn(argv[1], process)) {
        std::fprintf(stderr, "could not start bridge\n");
        return 1;
    }

    close(process.pipe_to);
    process.pipe_to = PLATFORM_INVALID_PIPE;

    int status = 0;
    for (int attempt = 0; attempt < 200; ++attempt) {
        const pid_t result = waitpid(process.pid, &status, WNOHANG);
        if (result == process.pid) {
            process.pid = -1;
            platform_force_kill(process);
            return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : 1;
        }
        if (result < 0) {
            platform_force_kill(process);
            return 1;
        }
        usleep(10000);
    }

    std::fprintf(stderr, "bridge did not exit after its command pipe closed\n");
    platform_force_kill(process);
    return 1;
}
