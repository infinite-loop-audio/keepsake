//
// Bridge loader — VST2 implementation via VeSTige.
//

#include "bridge_loader.h"
#include "debug_log.h"
#include <vestige/vestige.h>
#include <atomic>
#include <cstdio>
#include <cstring>

// --- VeSTige loader ---

typedef AEffect *(__cdecl *VstEntry)(audioMasterCallback);

extern double s_sample_rate;
extern uint32_t s_max_frames;
extern std::atomic<uint32_t> s_editor_open_edit_depth;
extern std::atomic<bool> s_editor_open_in_progress;
extern std::atomic<int32_t> s_last_automated_param;
extern std::atomic<uint32_t> s_automate_count;
extern std::atomic<uint32_t> s_begin_edit_count;
extern std::atomic<uint32_t> s_end_edit_count;
extern std::atomic<float> s_last_automated_value;
intptr_t __cdecl vst2_host_callback(
    AEffect *, int32_t opcode, int32_t index, intptr_t value, void *ptr, float opt);
void *vst2_open_library(const std::string &path, std::string &load_path);
void *vst2_lookup_entry(void *lib);
void vst2_close_library(void *lib);

class Vst2Loader : public BridgeLoader {
    AEffect *effect = nullptr;
    void *lib = nullptr;
    bool active = false;
    bool editor_rect_cached = false;
    int cached_editor_w = 0;
    int cached_editor_h = 0;

    // MIDI queue
    static constexpr int MAX_MIDI = 512;
    VstMidiEvent midi_queue[MAX_MIDI];
    int midi_count = 0;

public:
    bool load(const std::string &path) override {
        std::string load_path = path;
        lib = vst2_open_library(path, load_path);
        if (!lib) {
            fprintf(stderr, "bridge/vst2: failed to load '%s'\n", load_path.c_str());
            return false;
        }

        auto entry = reinterpret_cast<VstEntry>(vst2_lookup_entry(lib));
        if (!entry) return false;

        effect = entry(vst2_host_callback);
        if (!effect || effect->magic != kEffectMagic) {
            effect = nullptr;
            return false;
        }

        if (effect->dispatcher)
            effect->dispatcher(effect, effOpen, 0, 0, nullptr, 0.0f);

        fprintf(stderr, "bridge/vst2: loaded — in=%d out=%d params=%d\n",
                effect->numInputs, effect->numOutputs, effect->numParams);
        return true;
    }

    void get_info(IpcPluginInfo &info,
                   std::vector<uint8_t> &extra) override {
        info.unique_id = effect->uniqueID;
        info.num_inputs = effect->numInputs;
        info.num_outputs = effect->numOutputs;
        info.num_params = effect->numParams;
        info.flags = effect->flags;

        char namebuf[256] = {}, vendorbuf[256] = {}, productbuf[256] = {};
        if (effect->dispatcher) {
            effect->dispatcher(effect, effGetEffectName, 0, 0, namebuf, 0);
            effect->dispatcher(effect, effGetVendorString, 0, 0, vendorbuf, 0);
            effect->dispatcher(effect, effGetProductString, 0, 0, productbuf, 0);
            info.vendor_version = static_cast<int32_t>(
                effect->dispatcher(effect, effGetVendorVersion, 0, 0, nullptr, 0));
            info.category = static_cast<int32_t>(
                effect->dispatcher(effect, effGetPlugCategory, 0, 0, nullptr, 0));
        }
        if (namebuf[0] == '\0' && productbuf[0] != '\0')
            strncpy(namebuf, productbuf, sizeof(namebuf) - 1);

        auto append_str = [&](const char *s) {
            size_t len = strlen(s);
            extra.insert(extra.end(), s, s + len + 1);
        };
        append_str(namebuf);
        append_str(vendorbuf);
        append_str(productbuf);
    }

    void activate(double sr, uint32_t mf) override {
        s_sample_rate = sr;
        s_max_frames = mf;
        effect->dispatcher(effect, effSetSampleRate, 0, 0, nullptr, static_cast<float>(sr));
        effect->dispatcher(effect, effSetBlockSize, 0, static_cast<intptr_t>(mf), nullptr, 0);
        effect->dispatcher(effect, effMainsChanged, 0, 1, nullptr, 0);
        active = true;
    }

    void deactivate() override {
        if (active && effect)
            effect->dispatcher(effect, effMainsChanged, 0, 0, nullptr, 0);
        active = false;
    }

    void process(float **inputs, int /*num_in*/, float **outputs, int /*num_out*/,
                  uint32_t num_frames) override {
        // Flush MIDI
        if (midi_count > 0 && effect->dispatcher) {
            struct { int n; void *r; VstEvent *e[MAX_MIDI]; } ev;
            ev.n = midi_count; ev.r = nullptr;
            for (int i = 0; i < midi_count; i++)
                ev.e[i] = reinterpret_cast<VstEvent *>(&midi_queue[i]);
            effect->dispatcher(effect, effProcessEvents, 0, 0, &ev, 0);
            midi_count = 0;
        }
        if (effect->processReplacing)
            effect->processReplacing(effect, inputs, outputs, static_cast<int>(num_frames));
    }

    void set_param(uint32_t index, float value) override {
        if (effect->setParameter)
            effect->setParameter(effect, static_cast<int>(index), value);
    }

    bool get_param_info(uint32_t index, IpcParamInfoResponse &resp) override {
        if (static_cast<int>(index) >= effect->numParams) return false;
        resp.index = index;
        resp.current_value = effect->getParameter
            ? effect->getParameter(effect, static_cast<int>(index)) : 0.0f;
        if (effect->dispatcher) {
            effect->dispatcher(effect, effGetParamName, static_cast<int>(index),
                                0, resp.name, 0);
            effect->dispatcher(effect, effGetParamLabel, static_cast<int>(index),
                                0, resp.label, 0);
        }
        if (resp.name[0] == '\0') {
            snprintf(resp.name, sizeof(resp.name), "Param %u", index + 1);
        }
        return true;
    }

    void send_midi(int32_t delta, const uint8_t data[4]) override {
        if (midi_count >= MAX_MIDI) return;
        auto &m = midi_queue[midi_count];
        memset(&m, 0, sizeof(m));
        m.type = kVstMidiType;
        m.byteSize = sizeof(VstMidiEvent);
        m.deltaFrames = delta;
        memcpy(m.midiData, data, 4);
        midi_count++;
    }

    std::vector<uint8_t> get_chunk() override {
        if (!effect->dispatcher) return {};
        void *chunk = nullptr;
        intptr_t size = effect->dispatcher(effect, effGetChunk, 0, 0, &chunk, 0);
        if (size <= 0 || !chunk) return {};
        return {static_cast<uint8_t *>(chunk),
                static_cast<uint8_t *>(chunk) + size};
    }

    void set_chunk(const uint8_t *data, size_t size) override {
        if (effect->dispatcher)
            effect->dispatcher(effect, effSetChunk, 0,
                                static_cast<intptr_t>(size),
                                const_cast<uint8_t *>(data), 0);
    }

    bool has_editor() override { return (effect->flags & effFlagsHasEditor) != 0; }

    bool open_editor(void *parent) override {
        if (!effect || !effect->dispatcher) return false;
        s_editor_open_edit_depth.store(0);
        s_editor_open_in_progress.store(true);
        s_last_automated_param.store(-1);
        s_last_automated_value.store(0.0f);
        s_automate_count.store(0);
        s_begin_edit_count.store(0);
        s_end_edit_count.store(0);
#ifdef _WIN32
        unsigned long thread_id = static_cast<unsigned long>(GetCurrentThreadId());
#else
        unsigned long thread_id = 0;
#endif
        keepsake_debug_log("bridge/vst2: effEditOpen begin parent=%p effect=%p thread=%lu\n",
                           parent, static_cast<void *>(effect), thread_id);
        intptr_t result =
            effect->dispatcher(effect, effEditOpen, 0, 0, parent, 0.0f);
        keepsake_debug_log("bridge/vst2: effEditOpen end result=%lld parent=%p edit_depth=%d\n",
                           static_cast<long long>(result), parent,
                           s_editor_open_edit_depth.load());
        keepsake_debug_log("bridge/vst2: effEditOpen summary begin=%u automate=%u end=%u last_param=%d last_value=%.3f\n",
                           s_begin_edit_count.load(),
                           s_automate_count.load(),
                           s_end_edit_count.load(),
                           s_last_automated_param.load(),
                           s_last_automated_value.load());
        s_editor_open_in_progress.store(false);
        return result != 0;
    }

    void close_editor() override {
        if (effect && effect->dispatcher)
            effect->dispatcher(effect, effEditClose, 0, 0, nullptr, 0.0f);
    }

    void editor_idle() override {
        if (effect && effect->dispatcher)
            effect->dispatcher(effect, effEditIdle, 0, 0, nullptr, 0.0f);
    }

    bool get_editor_rect(int &w, int &h) override {
        if (editor_rect_cached && cached_editor_w > 0 && cached_editor_h > 0) {
            w = cached_editor_w;
            h = cached_editor_h;
            keepsake_debug_log("bridge/vst2: effEditGetRect cache-hit size=%dx%d thread=%lu\n",
                               w,
                               h,
#ifdef _WIN32
                               static_cast<unsigned long>(GetCurrentThreadId()));
#else
                               0UL);
#endif
            return true;
        }
        struct ERect { int16_t top, left, bottom, right; };
        ERect *rect = nullptr;
        keepsake_debug_log("bridge/vst2: effEditGetRect begin effect=%p thread=%lu default=%dx%d\n",
                           static_cast<void *>(effect),
#ifdef _WIN32
                           static_cast<unsigned long>(GetCurrentThreadId()),
#else
                           0UL,
#endif
                           w, h);
        if (effect->dispatcher)
            effect->dispatcher(effect, effEditGetRect, 0, 0, &rect, 0);
        keepsake_debug_log("bridge/vst2: effEditGetRect end rect=%p\n",
                           static_cast<void *>(rect));
        if (!rect) return false;
        w = rect->right - rect->left;
        h = rect->bottom - rect->top;
        keepsake_debug_log("bridge/vst2: effEditGetRect size=%dx%d raw=%d,%d,%d,%d\n",
                           w, h,
                           static_cast<int>(rect->left),
                           static_cast<int>(rect->top),
                           static_cast<int>(rect->right),
                           static_cast<int>(rect->bottom));
        editor_rect_cached = (w > 0 && h > 0);
        if (editor_rect_cached) {
            cached_editor_w = w;
            cached_editor_h = h;
        }
        return (w > 0 && h > 0);
    }

    void close() override {
        if (effect) {
            if (active)
                effect->dispatcher(effect, effMainsChanged, 0, 0, nullptr, 0);
            effect->dispatcher(effect, effClose, 0, 0, nullptr, 0);
            effect = nullptr;
        }
        if (lib) { vst2_close_library(lib); lib = nullptr; }
    }

    AEffect *get_effect() { return effect; }
};

BridgeLoader *create_vst2_loader() { return new Vst2Loader(); }
