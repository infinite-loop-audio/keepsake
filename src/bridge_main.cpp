//
// keepsake-bridge — multi-instance plugin server subprocess.
// Hosts one or more plugin instances, dispatched by instance ID.
//
// Contract refs:
//   docs/contracts/004-ipc-bridge-protocol.md
//   docs/contracts/006-process-isolation-policy.md
//

#include "ipc.h"
#include "bridge_runtime.h"
#include "bridge_gui.h"
#include "debug_log.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#ifndef _WIN32
#include <poll.h>
#else
#include <windows.h>
#include <process.h>
#define getpid _getpid
#endif

// Bridge I/O: command pipe on stdin + dedicated IPC out handle/pipe.
#ifdef _WIN32
PlatformPipe g_pipe_in  = GetStdHandle(STD_INPUT_HANDLE);
PlatformPipe g_pipe_out = PLATFORM_INVALID_PIPE;
PlatformPipe g_wake_fd  = PLATFORM_INVALID_PIPE;
#else
PlatformPipe g_pipe_in  = STDIN_FILENO;
PlatformPipe g_pipe_out = PLATFORM_BRIDGE_IPC_OUT_FD;
PlatformPipe g_wake_fd  = 3;
#endif

// --- Main loop ---

int main(int argc, char *argv[]) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    gui_init();

    // Wake pipe / IPC handle passed by the host.
    bool has_wake_pipe = false;
#ifdef _WIN32
    if (argc > 1) {
        uintptr_t ipc_handle = static_cast<uintptr_t>(_strtoui64(argv[1], nullptr, 10));
        if (ipc_handle != 0) {
            g_pipe_out = reinterpret_cast<HANDLE>(ipc_handle);
        }
    }
    if (g_pipe_out == PLATFORM_INVALID_PIPE) {
        g_pipe_out = GetStdHandle(STD_OUTPUT_HANDLE);
    }
#else
    g_wake_fd = -1;
    if (argc > 1) {
        int fd = atoi(argv[1]);
        if (fd > 0) {
            struct pollfd test_pfd = { fd, POLLIN, 0 };
            int r = poll(&test_pfd, 1, 0);
            if (r >= 0 && !(test_pfd.revents & POLLNVAL)) {
                g_wake_fd = fd;
                has_wake_pipe = true;
            }
        }
    }
#endif

    fprintf(stderr, "bridge: started (pid=%d) wake=%s\n", getpid(),
            has_wake_pipe ? "pipe" : "poll");

    uint64_t last_gui_idle_us = 0;
    auto now_us = []() -> uint64_t {
#ifdef _WIN32
        return static_cast<uint64_t>(GetTickCount64()) * 1000;
#else
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<uint64_t>(ts.tv_sec) * 1000000 +
               static_cast<uint64_t>(ts.tv_nsec) / 1000;
#endif
    };

    // Audio processing runs on a dedicated thread per active instance.
    // The thread blocks on pthread_cond_wait (zero CPU) and wakes
    // instantly when the host signals. No polling, no pipes.
    //
    // The main thread handles: pipe commands + GUI only.

    while (true) {
        // GUI throttle
        if (gui_is_open()) {
            uint64_t now = now_us();
            if (now - last_gui_idle_us >= 16000) {
                gui_idle(nullptr);
                last_gui_idle_us = now;
            }
        }

        // Command pipe (blocking when idle, short timeout when GUI open)
        int pipe_timeout = gui_is_open() ? 16 : -1;

#ifndef _WIN32
        struct pollfd pfd = { g_pipe_in, POLLIN, 0 };
        int pr = poll(&pfd, 1, pipe_timeout);
        if (pr <= 0) continue;
        if (!(pfd.revents & POLLIN)) continue;
#else
        if (!platform_read_ready(g_pipe_in, pipe_timeout)) continue;
#endif

        // Read IPC message from command pipe
        uint32_t opcode;
        std::vector<uint8_t> payload;
        if (!ipc_read_msg(g_pipe_in, opcode, payload)) {
            fprintf(stderr, "bridge: command pipe closed, exiting\n");
            break;
        }

        // Extract instance ID from payload (first 4 bytes)
        uint32_t instance_id = ipc_extract_instance_id(payload);

        // INIT is special — creates a new instance (instance_id from caller is 0)
        if (opcode == IPC_OP_INIT) {
            handle_init(instance_id, payload);
            continue;
        }

        // SHUTDOWN with instance_id 0 = exit entire process
        if (opcode == IPC_OP_SHUTDOWN && instance_id == 0) {
            ipc_write_ok(g_pipe_out);
            break;
        }

        // SHUTDOWN with specific instance = destroy just that instance
        if (opcode == IPC_OP_SHUTDOWN) {
            destroy_instance(instance_id);
            ipc_write_ok(g_pipe_out);
            fprintf(stderr, "bridge: destroyed instance %u (%zu remaining)\n",
                    instance_id, g_instances.size());
            continue;
        }

        // All other opcodes need a valid instance
        auto *inst = get_instance(instance_id);
        if (!inst) {
            ipc_write_error(g_pipe_out, "unknown instance");
            continue;
        }

        switch (opcode) {
        case IPC_OP_SET_SHM:    handle_set_shm(inst, payload); break;
        case IPC_OP_ACTIVATE:   handle_activate(inst, payload); break;
        case IPC_OP_START_PROC:
            inst->processing = true;
            ipc_write_ok(g_pipe_out);
            break;
        case IPC_OP_PROCESS:
            // Legacy pipe-based process (fallback, normally handled via shm)
            handle_process(inst, payload);
            break;
        case IPC_OP_SET_PARAM:  handle_set_param(inst, payload); break;
        case IPC_OP_MIDI_EVENT: handle_midi_event(inst, payload); break;
        case IPC_OP_GET_PARAM_INFO: handle_get_param_info(inst, payload); break;
        case IPC_OP_GET_CHUNK:  handle_get_chunk(inst); break;
        case IPC_OP_SET_CHUNK:  handle_set_chunk(inst, payload); break;
        case IPC_OP_EDITOR_OPEN:
            if (inst->loader && inst->loader->has_editor()) {
                EditorHeaderInfo hdr;
                hdr.format = "VST2";
#if defined(__x86_64__)
                hdr.architecture = "x86_64 (Rosetta)";
#elif defined(__aarch64__)
                hdr.architecture = "ARM64";
#elif defined(__i386__)
                hdr.architecture = "32-bit";
#else
                hdr.architecture = "native";
#endif
                hdr.isolation = "per-instance";
                IpcPluginInfo pi2 = {};
                std::vector<uint8_t> extra2;
                inst->loader->get_info(pi2, extra2);
                if (!extra2.empty())
                    hdr.plugin_name = std::string(
                        reinterpret_cast<const char *>(extra2.data()));
                else
                    hdr.plugin_name = "Plugin";

                if (gui_open_editor(inst->loader, hdr)) {
                    ipc_write_ok(g_pipe_out);
                } else {
                keepsake_debug_log("bridge: editor open FAILED instance=%u\n",
                                   instance_id);
                ipc_write_error(g_pipe_out, "EDITOR_OPEN: failed");
                }
            } else {
                ipc_write_error(g_pipe_out, "EDITOR_OPEN: no editor");
            }
            break;
        case IPC_OP_EDITOR_CLOSE:
            gui_close_editor(inst->loader);
            ipc_write_ok(g_pipe_out);
            break;
        case IPC_OP_EDITOR_MOUSE:
            if (gui_is_iosurface_mode() && payload.size() >= sizeof(IpcMouseEvent)) {
                IpcMouseEvent mev;
                memcpy(&mev, payload.data(), sizeof(mev));
                gui_forward_mouse(mev);
            }
            break;
        case IPC_OP_EDITOR_KEY:
            if (gui_is_iosurface_mode() && payload.size() >= sizeof(IpcKeyEvent)) {
                IpcKeyEvent kev;
                memcpy(&kev, payload.data(), sizeof(kev));
                gui_forward_key(kev);
            }
            break;
        case IPC_OP_EDITOR_GET_RECT: handle_editor_get_rect(inst); break;
        case IPC_OP_EDITOR_SET_PARENT:
            if (inst->loader && payload.size() >= 8) {
                uint64_t handle;
                memcpy(&handle, payload.data(), 8);
                keepsake_debug_log("bridge: EDITOR_SET_PARENT handle=%p instance=%u\n",
                                   reinterpret_cast<void *>(static_cast<uintptr_t>(handle)),
                                   instance_id);
                if (gui_open_editor_embedded(inst->loader, handle))
                    ipc_write_ok(g_pipe_out);
                else
                    ipc_write_error(g_pipe_out, "EDITOR_SET_PARENT: failed");
            } else {
                ipc_write_error(g_pipe_out, "EDITOR_SET_PARENT: not ready");
            }
            break;
        case IPC_OP_EDITOR_SET_TRANSIENT:
            if (payload.size() >= 8) {
                uint64_t handle;
                memcpy(&handle, payload.data(), 8);
                keepsake_debug_log("bridge: EDITOR_SET_TRANSIENT handle=%p instance=%u\n",
                                   reinterpret_cast<void *>(static_cast<uintptr_t>(handle)),
                                   instance_id);
                gui_set_editor_transient(handle);
                ipc_write_ok(g_pipe_out);
            } else {
                ipc_write_error(g_pipe_out, "EDITOR_SET_TRANSIENT: bad payload");
            }
            break;
        case IPC_OP_STOP_PROC:
            inst->processing = false;
            ipc_write_ok(g_pipe_out);
            break;
        case IPC_OP_DEACTIVATE: handle_deactivate(inst); break;
        default:
            ipc_write_error(g_pipe_out, "unknown opcode");
            break;
        }
    }

    // Cleanup all instances
    if (gui_is_open()) gui_close_editor(nullptr);
    destroy_all_instances();

    fprintf(stderr, "bridge: exiting cleanly\n");
    return 0;
}
