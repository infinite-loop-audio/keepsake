#pragma once
//
// Keepsake IPC — protocol constants, pipe I/O, and shared memory helpers.
// Shared between the main plugin and the bridge subprocess.
// Contract ref: docs/contracts/004-ipc-bridge-protocol.md
//

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "platform.h"
#ifndef _WIN32
#include <pthread.h>
#endif

// --- Protocol opcodes ---

// Host → Bridge
static constexpr uint32_t IPC_OP_INIT       = 0x01;
static constexpr uint32_t IPC_OP_SET_SHM    = 0x02;
static constexpr uint32_t IPC_OP_ACTIVATE   = 0x03;
static constexpr uint32_t IPC_OP_PROCESS    = 0x04;
static constexpr uint32_t IPC_OP_SET_PARAM  = 0x05;
static constexpr uint32_t IPC_OP_STOP_PROC  = 0x06;
static constexpr uint32_t IPC_OP_START_PROC = 0x07;
static constexpr uint32_t IPC_OP_DEACTIVATE = 0x08;
static constexpr uint32_t IPC_OP_SHUTDOWN   = 0x09;
static constexpr uint32_t IPC_OP_MIDI_EVENT    = 0x0A;
static constexpr uint32_t IPC_OP_GET_PARAM_INFO = 0x0B; // payload: uint32_t index
static constexpr uint32_t IPC_OP_GET_CHUNK      = 0x0C;  // no payload
static constexpr uint32_t IPC_OP_SET_CHUNK      = 0x0D;  // payload: chunk bytes
static constexpr uint32_t IPC_OP_EDITOR_OPEN    = 0x10;  // no payload
static constexpr uint32_t IPC_OP_EDITOR_CLOSE   = 0x11;  // no payload
static constexpr uint32_t IPC_OP_EDITOR_GET_RECT = 0x12; // no payload
static constexpr uint32_t IPC_OP_EDITOR_SET_PARENT  = 0x13; // payload: uint64_t native_handle
static constexpr uint32_t IPC_OP_EDITOR_MOUSE       = 0x14; // payload: IpcMouseEvent
static constexpr uint32_t IPC_OP_EDITOR_KEY          = 0x15; // payload: IpcKeyEvent
static constexpr uint32_t IPC_OP_EDITOR_SET_TRANSIENT = 0x16; // payload: uint64_t native_handle
static constexpr uint32_t IPC_OP_EDITOR_GET_STATUS    = 0x17; // no payload

// Bridge → Host
static constexpr uint32_t IPC_OP_OK           = 0x81;
static constexpr uint32_t IPC_OP_ERROR        = 0x82;
static constexpr uint32_t IPC_OP_PROCESS_DONE = 0x84;
static constexpr uint32_t IPC_MAX_PAYLOAD_BYTES = 64u * 1024u * 1024u;

// --- Message header ---
// All IPC structs are packed to ensure identical layout across 32-bit
// and 64-bit processes communicating over the bridge.

#pragma pack(push, 1)

struct IpcHeader {
    uint32_t opcode;
    uint32_t payload_size;
};

// --- Payload structures ---

struct IpcActivatePayload {
    double sample_rate;
    uint32_t max_frames;
};

struct IpcProcessPayload {
    uint32_t num_frames;
};

struct IpcSetParamPayload {
    uint32_t index;
    float value;
};

struct IpcMidiEventPayload {
    int32_t delta_frames;
    uint8_t data[4];
};

// Plugin format identifier — sent with INIT
enum PluginFormat : uint32_t {
    FORMAT_VST2 = 0,
    FORMAT_VST3 = 1,
    FORMAT_AU   = 2,
};

// Extended INIT payload: [uint32_t format][path bytes...]
// (format prepended before path in the payload)

// Sent as OK payload after successful INIT.
// Fixed-size fields followed by null-terminated strings.
struct IpcPluginInfo {
    int32_t unique_id;
    int32_t num_inputs;
    int32_t num_outputs;
    int32_t num_params;
    int32_t flags;
    int32_t category;
    int32_t vendor_version;
    // Followed by: name\0vendor\0product\0
};

struct IpcEditorStatus {
    uint32_t open;
    uint32_t pending;
};

// --- Pipe I/O helpers ---
// These use PlatformPipe from platform.h for cross-platform support.

// Write a message: [opcode][size][payload]
static inline bool ipc_write_msg(PlatformPipe fd, uint32_t opcode,
                                  const void *payload = nullptr,
                                  uint32_t size = 0) {
    IpcHeader hdr = { opcode, size };
    if (!platform_write(fd, &hdr, sizeof(hdr))) return false;
    if (size > 0 && payload) {
        if (!platform_write(fd, payload, size)) return false;
    }
    return true;
}

// Read a message. Returns false on EOF or error.
// timeout_ms: -1 = block, 0 = non-blocking, >0 = timeout
static inline bool ipc_read_msg(PlatformPipe fd, uint32_t &opcode,
                                 std::vector<uint8_t> &payload,
                                 int timeout_ms = -1) {
    if (timeout_ms >= 0) {
        if (!platform_read_ready(fd, timeout_ms)) return false;
    }
    IpcHeader hdr;
    if (!platform_read(fd, &hdr, sizeof(hdr))) return false;
    if (hdr.payload_size > IPC_MAX_PAYLOAD_BYTES) {
        fprintf(stderr, "keepsake: IPC payload too large: %u bytes\n",
                hdr.payload_size);
        return false;
    }
    opcode = hdr.opcode;
    payload.resize(hdr.payload_size);
    if (hdr.payload_size > 0) {
        if (!platform_read(fd, payload.data(), hdr.payload_size)) return false;
    }
    return true;
}

// Convenience writers
static inline bool ipc_write_ok(PlatformPipe fd,
                                 const void *data = nullptr,
                                 uint32_t size = 0) {
    return ipc_write_msg(fd, IPC_OP_OK, data, size);
}

static inline bool ipc_write_error(PlatformPipe fd, const char *msg) {
    return ipc_write_msg(fd, IPC_OP_ERROR, msg,
                          static_cast<uint32_t>(strlen(msg)));
}

static inline bool ipc_write_process_done(PlatformPipe fd) {
    return ipc_write_msg(fd, IPC_OP_PROCESS_DONE);
}

// Sent as OK payload for GET_PARAM_INFO
struct IpcParamInfoResponse {
    uint32_t index;
    float current_value;
    char name[64];
    char label[16]; // unit label (dB, Hz, etc.)
};

// Mouse event forwarding (host → bridge)
struct IpcMouseEvent {
    int32_t x;
    int32_t y;
    int32_t type;     // 0=move, 1=down, 2=up, 3=drag, 4=scroll
    int32_t button;   // 0=left, 1=right, 2=middle
    float scroll_dx;
    float scroll_dy;
};

// Key event forwarding (host → bridge)
struct IpcKeyEvent {
    uint32_t keycode;
    uint32_t modifiers;
    int32_t type;     // 0=down, 1=up
    char character;
    char pad[3];
};

// IOSurface ID response (bridge → host, in OK payload for EDITOR_OPEN)
struct IpcEditorSurface {
    uint32_t surface_id;  // IOSurfaceID for cross-process GPU sharing
    int32_t width;
    int32_t height;
};

// Sent as OK payload for EDITOR_GET_RECT
struct IpcEditorRect {
    int32_t width;
    int32_t height;
};

#pragma pack(pop)

// Static assertions: verify IPC struct sizes are consistent across
// 32-bit and 64-bit builds (no pointer-sized fields, no padding surprises).
static_assert(sizeof(IpcHeader) == 8, "IpcHeader size mismatch");
static_assert(sizeof(IpcActivatePayload) == 12, "IpcActivatePayload size mismatch");
static_assert(sizeof(IpcProcessPayload) == 4, "IpcProcessPayload size mismatch");
static_assert(sizeof(IpcSetParamPayload) == 8, "IpcSetParamPayload size mismatch");
static_assert(sizeof(IpcMidiEventPayload) == 8, "IpcMidiEventPayload size mismatch");
static_assert(sizeof(IpcPluginInfo) == 28, "IpcPluginInfo size mismatch");
static_assert(sizeof(IpcParamInfoResponse) == 88, "IpcParamInfoResponse size mismatch");
static_assert(sizeof(IpcEditorRect) == 8, "IpcEditorRect size mismatch");

// --- Instance-aware message helpers ---
// For multi-instance bridges, the instance_id is the first 4 bytes of
// every payload. These helpers wrap the base functions.

static inline bool ipc_write_instance_msg(PlatformPipe fd, uint32_t opcode,
                                            uint32_t instance_id,
                                            const void *payload = nullptr,
                                            uint32_t size = 0) {
    uint32_t total = 4 + size;
    std::vector<uint8_t> buf(total);
    memcpy(buf.data(), &instance_id, 4);
    if (size > 0 && payload)
        memcpy(buf.data() + 4, payload, size);
    return ipc_write_msg(fd, opcode, buf.data(), total);
}

// Read instance ID from the front of a payload. Modifies payload in-place
// to strip the ID.
static inline uint32_t ipc_extract_instance_id(std::vector<uint8_t> &payload) {
    if (payload.size() < 4) return 0;
    uint32_t id;
    memcpy(&id, payload.data(), 4);
    payload.erase(payload.begin(), payload.begin() + 4);
    return id;
}

// --- Shared memory process control ---
// The audio hot path uses atomic flags in shared memory instead of pipes.
// Zero syscalls during process: host writes inputs + sets flag, bridge
// processes + sets done flag, host reads outputs.

static constexpr uint32_t SHM_STATE_IDLE              = 0;
static constexpr uint32_t SHM_STATE_PROCESS_REQUESTED = 1;
static constexpr uint32_t SHM_STATE_PROCESS_DONE      = 2;
static constexpr uint32_t SHM_STATE_PROCESSING        = 3;

static constexpr uint32_t SHM_EDITOR_CLOSED  = 0;
static constexpr uint32_t SHM_EDITOR_OPENING = 1;
static constexpr uint32_t SHM_EDITOR_OPEN    = 2;
static constexpr uint32_t SHM_EDITOR_FAILED  = 3;

static constexpr uint32_t SHM_MAX_MIDI_EVENTS = 256;

#pragma pack(push, 1)
struct ShmMidiEvent {
    int32_t delta_frames;
    uint8_t data[4];
};

struct ShmProcessControl {
    volatile uint32_t state;       // SHM_STATE_*
    volatile uint32_t editor_state; // SHM_EDITOR_*
    uint32_t num_frames;           // frames to process this cycle
    uint32_t midi_count;           // number of MIDI events this cycle
    uint32_t param_count;          // number of param changes this cycle
#ifndef _WIN32
    pthread_mutex_t mutex;         // cross-process mutex for sync
    pthread_cond_t cond;           // cross-process condition variable
#endif
    ShmMidiEvent midi_events[SHM_MAX_MIDI_EVENTS];
    IpcSetParamPayload params[64]; // batched param changes
    // Audio buffers follow after this struct
};

// Initialize the mutex/cond in shared memory (called by host after shm create)
static inline bool shm_init_sync(ShmProcessControl *ctrl) {
    ctrl->state = SHM_STATE_IDLE;
    ctrl->editor_state = SHM_EDITOR_CLOSED;
#ifndef _WIN32
    pthread_mutexattr_t mattr;
    pthread_mutexattr_init(&mattr);
    pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&ctrl->mutex, &mattr);
    pthread_mutexattr_destroy(&mattr);

    pthread_condattr_t cattr;
    pthread_condattr_init(&cattr);
    pthread_condattr_setpshared(&cattr, PTHREAD_PROCESS_SHARED);
    pthread_cond_init(&ctrl->cond, &cattr);
    pthread_condattr_destroy(&cattr);
#endif
    return true;
}
#pragma pack(pop)

// Get pointer to audio input buffers (after the control region)
static inline float *shm_audio_inputs(void *shm_ptr, int channel, uint32_t max_frames) {
    auto *base = reinterpret_cast<uint8_t *>(shm_ptr) + sizeof(ShmProcessControl);
    return reinterpret_cast<float *>(base) + channel * max_frames;
}

// Get pointer to audio output buffers
static inline float *shm_audio_outputs(void *shm_ptr, int num_inputs,
                                         int channel, uint32_t max_frames) {
    auto *base = reinterpret_cast<uint8_t *>(shm_ptr) + sizeof(ShmProcessControl);
    return reinterpret_cast<float *>(base) + (num_inputs + channel) * max_frames;
}

// Get the process control struct from shared memory
static inline ShmProcessControl *shm_control(void *shm_ptr) {
    return reinterpret_cast<ShmProcessControl *>(shm_ptr);
}

// Atomic helpers for cross-process shared memory
static inline void shm_store_release(volatile uint32_t *ptr, uint32_t val) {
#ifdef _WIN32
    InterlockedExchange(reinterpret_cast<volatile LONG *>(ptr),
                        static_cast<LONG>(val));
#else
    __atomic_store_n(ptr, val, __ATOMIC_RELEASE);
#endif
}

static inline uint32_t shm_load_acquire(volatile uint32_t *ptr) {
#ifdef _WIN32
    return static_cast<uint32_t>(
        InterlockedCompareExchange(reinterpret_cast<volatile LONG *>(ptr), 0, 0));
#else
    return __atomic_load_n(ptr, __ATOMIC_ACQUIRE);
#endif
}

// Total shared memory size needed
static inline size_t shm_total_size(int num_inputs, int num_outputs, uint32_t max_frames) {
    return sizeof(ShmProcessControl) +
           static_cast<size_t>(num_inputs + num_outputs) * max_frames * sizeof(float);
}

// Shared memory: use PlatformShm and platform_shm_* from platform.h
