//
// keepsake-bridge runtime helpers — audio thread lifecycle.
//

#include "bridge_runtime.h"
#include "debug_log.h"

#include <cstdio>

#ifdef _WIN32
#include <process.h>
#endif

static void bridge_process_shm_request(PluginInstance *inst) {
    auto *ctrl = shm_control(inst->shm.ptr);
    shm_store_release(&ctrl->state, SHM_STATE_PROCESSING);

    std::vector<float *> in_ptrs(static_cast<size_t>(inst->num_inputs));
    std::vector<float *> out_ptrs(static_cast<size_t>(inst->num_outputs));

    uint32_t nframes = ctrl->num_frames;
    if (nframes > inst->max_frames) nframes = inst->max_frames;

    keepsake_debug_log("bridge: process request instance=%u frames=%u midi=%u params=%u state=%u\n",
                       inst->id, nframes, ctrl->midi_count, ctrl->param_count,
                       shm_load_acquire(&ctrl->state));

    for (uint32_t p = 0; p < ctrl->param_count && p < 64; p++) {
        inst->loader->set_param(ctrl->params[p].index, ctrl->params[p].value);
    }
    for (uint32_t m = 0; m < ctrl->midi_count && m < SHM_MAX_MIDI_EVENTS; m++) {
        inst->loader->send_midi(ctrl->midi_events[m].delta_frames,
                                ctrl->midi_events[m].data);
    }

    for (int i = 0; i < inst->num_inputs; i++) {
        in_ptrs[static_cast<size_t>(i)] =
            shm_audio_inputs(inst->shm.ptr, i, inst->max_frames);
    }
    for (int i = 0; i < inst->num_outputs; i++) {
        out_ptrs[static_cast<size_t>(i)] =
            shm_audio_outputs(inst->shm.ptr, inst->num_inputs, i, inst->max_frames);
    }

    inst->loader->process(
        inst->num_inputs > 0 ? in_ptrs.data() : nullptr,
        inst->num_inputs,
        inst->num_outputs > 0 ? out_ptrs.data() : nullptr,
        inst->num_outputs,
        nframes);

    shm_store_release(&ctrl->state, SHM_STATE_PROCESS_DONE);
#ifdef _WIN32
    if (inst->shm_done_event != INVALID_HANDLE_VALUE) {
        SetEvent(inst->shm_done_event);
    }
#endif
    keepsake_debug_log("bridge: process done instance=%u state=%u\n",
                       inst->id, shm_load_acquire(&ctrl->state));
}

#ifndef _WIN32

static void *audio_thread_func(void *arg) {
    auto *inst = static_cast<PluginInstance *>(arg);
    auto *ctrl = shm_control(inst->shm.ptr);

    fprintf(stderr, "bridge: audio thread started for instance %u\n", inst->id);

    while (inst->active) {
        pthread_mutex_lock(&ctrl->mutex);

        while (ctrl->state != SHM_STATE_PROCESS_REQUESTED && inst->active) {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_nsec += 50000000;
            if (ts.tv_nsec >= 1000000000) {
                ts.tv_sec++;
                ts.tv_nsec -= 1000000000;
            }
            pthread_cond_timedwait(&ctrl->cond, &ctrl->mutex, &ts);
        }

        if (!inst->active) {
            pthread_mutex_unlock(&ctrl->mutex);
            break;
        }

        bridge_process_shm_request(inst);
        pthread_cond_signal(&ctrl->cond);
        pthread_mutex_unlock(&ctrl->mutex);
    }

    fprintf(stderr, "bridge: audio thread stopped for instance %u\n", inst->id);
    return nullptr;
}

void bridge_audio_start(PluginInstance *inst) {
    if (inst->shm.ptr) {
        pthread_create(&inst->audio_thread, nullptr, audio_thread_func, inst);
    }
}

void bridge_audio_stop(PluginInstance *inst) {
    if (!inst->shm.ptr) return;
    auto *dc = shm_control(inst->shm.ptr);
    pthread_mutex_lock(&dc->mutex);
    pthread_cond_signal(&dc->cond);
    pthread_mutex_unlock(&dc->mutex);
    pthread_join(inst->audio_thread, nullptr);
    inst->audio_thread = 0;
}
#else
static unsigned __stdcall audio_thread_func(void *arg) {
    auto *inst = static_cast<PluginInstance *>(arg);
    auto *ctrl = shm_control(inst->shm.ptr);

    fprintf(stderr, "bridge: audio thread started for instance %u\n", inst->id);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    while (inst->active) {
        if (inst->shm_request_event != INVALID_HANDLE_VALUE) {
            DWORD wait_result = WaitForSingleObject(inst->shm_request_event, 10);
            if (!inst->active) break;
            if (wait_result == WAIT_OBJECT_0 ||
                shm_load_acquire(&ctrl->state) == SHM_STATE_PROCESS_REQUESTED) {
                bridge_process_shm_request(inst);
            }
            continue;
        }
        if (shm_load_acquire(&ctrl->state) == SHM_STATE_PROCESS_REQUESTED) {
            bridge_process_shm_request(inst);
            continue;
        }
        Sleep(1);
    }

    fprintf(stderr, "bridge: audio thread stopped for instance %u\n", inst->id);
    return 0;
}

void bridge_audio_start(PluginInstance *inst) {
    if (!inst->shm.ptr || inst->audio_thread != INVALID_HANDLE_VALUE) return;
    uintptr_t th = _beginthreadex(nullptr, 0, audio_thread_func, inst, 0, nullptr);
    if (th != 0) inst->audio_thread = reinterpret_cast<HANDLE>(th);
}

void bridge_audio_stop(PluginInstance *inst) {
    if (inst->audio_thread == INVALID_HANDLE_VALUE) return;
    if (inst->shm_request_event != INVALID_HANDLE_VALUE) {
        SetEvent(inst->shm_request_event);
    }
    WaitForSingleObject(inst->audio_thread, 1000);
    CloseHandle(inst->audio_thread);
    inst->audio_thread = INVALID_HANDLE_VALUE;
}
#endif
