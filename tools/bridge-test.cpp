// bridge-test: test the multi-instance bridge subprocess.
// Spawns keepsake-bridge, loads one or two plugins, processes audio.
//
// Usage: bridge-test <bridge-binary> <plugin-path> [second-plugin-path]

#include "ipc.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>

#ifndef _WIN32
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

static uint32_t detect_format(const char *path) {
    std::string p(path);
    if (p.size() >= 5 && p.substr(p.size() - 5) == ".vst3") return FORMAT_VST3;
    if (p.size() >= 10 && p.substr(p.size() - 10) == ".component") return FORMAT_AU;
    return FORMAT_VST2;
}

// Convenience: send instance-aware command and wait for response
static bool send_wait(int wr, int rd, uint32_t opcode, uint32_t inst_id,
                       const void *data = nullptr, uint32_t size = 0,
                       std::vector<uint8_t> *out_payload = nullptr) {
    if (!ipc_write_instance_msg(wr, opcode, inst_id, data, size)) return false;
    uint32_t op;
    std::vector<uint8_t> payload;
    if (!ipc_read_msg(rd, op, payload, 10000)) return false;
    if (op == IPC_OP_ERROR) {
        std::string msg(payload.begin(), payload.end());
        fprintf(stderr, "  error: %s\n", msg.c_str());
        return false;
    }
    if (out_payload) *out_payload = std::move(payload);
    return (op == IPC_OP_OK || op == IPC_OP_PROCESS_DONE);
}

struct TestInstance {
    uint32_t id = 0;
    int32_t num_in = 0;
    int32_t num_out = 0;
    PlatformShm shm;
    uint32_t max_frames = 256;
};

static bool init_instance(int wr, int rd, const char *path, TestInstance &inst) {
    uint32_t format = detect_format(path);
    size_t path_len = strlen(path);
    std::vector<uint8_t> init_data(4 + path_len);
    memcpy(init_data.data(), &format, 4);
    memcpy(init_data.data() + 4, path, path_len);

    // INIT with instance_id=0 (bridge assigns a real one)
    ipc_write_instance_msg(wr, IPC_OP_INIT, 0, init_data.data(),
                            static_cast<uint32_t>(init_data.size()));

    uint32_t op;
    std::vector<uint8_t> payload;
    if (!ipc_read_msg(rd, op, payload, 10000) || op != IPC_OP_OK) {
        if (op == IPC_OP_ERROR) {
            std::string msg(payload.begin(), payload.end());
            fprintf(stderr, "INIT error: %s\n", msg.c_str());
        }
        return false;
    }

    // Parse response: [instance_id][IpcPluginInfo][strings...]
    if (payload.size() >= 4 + sizeof(IpcPluginInfo)) {
        memcpy(&inst.id, payload.data(), 4);
        IpcPluginInfo info;
        memcpy(&info, payload.data() + 4, sizeof(info));
        inst.num_in = info.num_inputs;
        inst.num_out = info.num_outputs;
        printf("  Instance %u: in=%d out=%d params=%d\n",
               inst.id, inst.num_in, inst.num_out, info.num_params);
    }
    return true;
}

static bool setup_instance(int wr, int rd, TestInstance &inst) {
    // SET_SHM
    size_t shm_size = static_cast<size_t>(inst.num_in + inst.num_out) *
                      inst.max_frames * sizeof(float);
    if (shm_size == 0) shm_size = inst.max_frames * sizeof(float);

    char shm_name[128];
    snprintf(shm_name, sizeof(shm_name), "/keepsake-test-%d-%u", getpid(), inst.id);
    platform_shm_create(inst.shm, shm_name, shm_size);

    uint32_t name_len = static_cast<uint32_t>(strlen(shm_name));
    uint32_t sz32 = static_cast<uint32_t>(shm_size);
    std::vector<uint8_t> shm_data(4 + name_len + 4);
    memcpy(shm_data.data(), &name_len, 4);
    memcpy(shm_data.data() + 4, shm_name, name_len);
    memcpy(shm_data.data() + 4 + name_len, &sz32, 4);

    if (!send_wait(wr, rd, IPC_OP_SET_SHM, inst.id,
                    shm_data.data(), static_cast<uint32_t>(shm_data.size())))
        return false;

    // ACTIVATE
    IpcActivatePayload ap = { 44100.0, inst.max_frames };
    if (!send_wait(wr, rd, IPC_OP_ACTIVATE, inst.id, &ap, sizeof(ap)))
        return false;

    // START_PROC
    return send_wait(wr, rd, IPC_OP_START_PROC, inst.id);
}

static float process_instance(int wr, int rd, TestInstance &inst) {
    auto *base = static_cast<float *>(inst.shm.ptr);

    // Write sine wave to inputs
    for (int ch = 0; ch < inst.num_in; ch++) {
        float *inp = base + ch * inst.max_frames;
        for (uint32_t i = 0; i < inst.max_frames; i++)
            inp[i] = sinf(2.0f * 3.14159f * 440.0f * float(i) / 44100.0f) * 0.5f;
    }

    IpcProcessPayload pp = { inst.max_frames };
    std::vector<uint8_t> resp;
    if (!send_wait(wr, rd, IPC_OP_PROCESS, inst.id, &pp, sizeof(pp), &resp))
        return -1.0f;

    // Measure output peak
    float *out = base + inst.num_in * inst.max_frames;
    float peak = 0.0f;
    for (uint32_t i = 0; i < inst.max_frames * static_cast<uint32_t>(inst.num_out); i++) {
        float v = fabsf(out[i]);
        if (v > peak) peak = v;
    }
    return peak;
}

static void teardown_instance(int wr, int rd, TestInstance &inst) {
    send_wait(wr, rd, IPC_OP_STOP_PROC, inst.id);
    send_wait(wr, rd, IPC_OP_DEACTIVATE, inst.id);
    // SHUTDOWN this instance (not the whole bridge)
    send_wait(wr, rd, IPC_OP_SHUTDOWN, inst.id);
    platform_shm_close(inst.shm);
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <bridge-binary> <plugin-path> [second-plugin]\n",
                argv[0]);
        return 1;
    }

    const char *bridge_bin = argv[1];
    const char *path1 = argv[2];
    const char *path2 = (argc > 3) ? argv[3] : nullptr;

    // Spawn bridge
    int to_bridge[2], from_bridge[2];
    pipe(to_bridge); pipe(from_bridge);
    pid_t pid = fork();
    if (pid == 0) {
        dup2(to_bridge[0], STDIN_FILENO);
        dup2(from_bridge[1], PLATFORM_BRIDGE_IPC_OUT_FD);
        dup2(STDERR_FILENO, STDOUT_FILENO);
        if (to_bridge[0] != STDIN_FILENO) close(to_bridge[0]);
        close(to_bridge[1]);
        close(from_bridge[0]);
        if (from_bridge[1] != PLATFORM_BRIDGE_IPC_OUT_FD &&
            from_bridge[1] != STDOUT_FILENO) {
            close(from_bridge[1]);
        }
        execl(bridge_bin, "keepsake-bridge", nullptr);
        perror("execl"); _exit(1);
    }
    close(to_bridge[0]); close(from_bridge[1]);
    int wr = to_bridge[1], rd = from_bridge[0];

    printf("=== Multi-instance bridge test (pid=%d) ===\n\n", pid);

    // --- Instance 1 ---
    printf("[1] Loading: %s\n", path1);
    TestInstance inst1;
    if (!init_instance(wr, rd, path1, inst1)) {
        fprintf(stderr, "Instance 1 INIT failed\n");
        kill(pid, SIGKILL); waitpid(pid, nullptr, 0); return 1;
    }
    if (!setup_instance(wr, rd, inst1)) {
        fprintf(stderr, "Instance 1 setup failed\n");
        kill(pid, SIGKILL); waitpid(pid, nullptr, 0); return 1;
    }

    float peak1 = process_instance(wr, rd, inst1);
    printf("[1] Process: peak=%.6f %s\n", peak1,
           peak1 > 0.0001f ? "(AUDIO)" : "(silence)");

    // --- Instance 2 (optional) ---
    TestInstance inst2;
    if (path2) {
        printf("\n[2] Loading: %s\n", path2);
        if (!init_instance(wr, rd, path2, inst2)) {
            fprintf(stderr, "Instance 2 INIT failed\n");
        } else if (!setup_instance(wr, rd, inst2)) {
            fprintf(stderr, "Instance 2 setup failed\n");
        } else {
            float peak2 = process_instance(wr, rd, inst2);
            printf("[2] Process: peak=%.6f %s\n", peak2,
                   peak2 > 0.0001f ? "(AUDIO)" : "(silence)");

            // Process instance 1 again (verify it still works)
            float peak1b = process_instance(wr, rd, inst1);
            printf("[1] Process again: peak=%.6f %s\n", peak1b,
                   peak1b > 0.0001f ? "(AUDIO)" : "(silence)");

            teardown_instance(wr, rd, inst2);
            printf("[2] Destroyed\n");
        }
    }

    // Teardown instance 1
    teardown_instance(wr, rd, inst1);
    printf("[1] Destroyed\n");

    // Shutdown entire bridge
    uint32_t zero = 0;
    ipc_write_instance_msg(wr, IPC_OP_SHUTDOWN, zero);
    uint32_t op;
    std::vector<uint8_t> payload;
    ipc_read_msg(rd, op, payload, 5000);
    printf("\n[BRIDGE SHUTDOWN] %s\n", op == IPC_OP_OK ? "OK" : "FAILED");

    int status;
    waitpid(pid, &status, 0);
    printf("Bridge exited with status %d\n", WEXITSTATUS(status));

    close(wr); close(rd);
    printf("\n=== Test complete ===\n");
    return 0;
}
