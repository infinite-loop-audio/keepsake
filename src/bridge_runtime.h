#pragma once
//
// keepsake-bridge runtime helpers — instance state and non-GUI IPC handlers.
//

#include "ipc.h"
#include "bridge_loader.h"

#include <unordered_map>
#include <vector>

extern PlatformPipe g_pipe_in;
extern PlatformPipe g_pipe_out;
extern PlatformPipe g_wake_fd;

struct PluginInstance {
    uint32_t id;
    BridgeLoader *loader = nullptr;
    PlatformShm shm;
    int32_t num_inputs = 0;
    int32_t num_outputs = 0;
    uint32_t max_frames = 0;
    volatile bool active = false;
    bool processing = false;
#ifndef _WIN32
    pthread_t audio_thread = 0;
#else
    HANDLE audio_thread = INVALID_HANDLE_VALUE;
#endif
};

extern std::unordered_map<uint32_t, PluginInstance *> g_instances;

PluginInstance *get_instance(uint32_t id);
void destroy_instance(uint32_t id);
void destroy_all_instances();

void bridge_audio_start(PluginInstance *inst);
void bridge_audio_stop(PluginInstance *inst);

void handle_init(uint32_t caller_id, const std::vector<uint8_t> &payload);
void handle_set_shm(PluginInstance *inst, const std::vector<uint8_t> &payload);
void handle_activate(PluginInstance *inst, const std::vector<uint8_t> &payload);
void handle_process(PluginInstance *inst, const std::vector<uint8_t> &payload);
void handle_set_param(PluginInstance *inst, const std::vector<uint8_t> &payload);
void handle_midi_event(PluginInstance *inst, const std::vector<uint8_t> &payload);
void handle_get_param_info(PluginInstance *inst, const std::vector<uint8_t> &payload);
void handle_get_chunk(PluginInstance *inst);
void handle_set_chunk(PluginInstance *inst, const std::vector<uint8_t> &payload);
void handle_editor_get_rect(PluginInstance *inst);
void handle_deactivate(PluginInstance *inst);
