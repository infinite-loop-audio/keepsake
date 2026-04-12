//
// Bridge loader — VST2 implementation via VeSTige.
//

#include "bridge_loader.h"
#include <vestige/vestige.h>
#include <cstdio>
#include <cstring>
#include <atomic>

#ifndef _WIN32
#include <dlfcn.h>
#else
#include <windows.h>
#endif

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

// --- VeSTige loader ---

typedef AEffect *(__cdecl *VstEntry)(audioMasterCallback);

static double s_sample_rate = 44100.0;
static uint32_t s_max_frames = 512;
static std::atomic<uint32_t> s_editor_open_edit_depth{0};

static intptr_t __cdecl vst2_host_callback(
    AEffect *, int32_t opcode, int32_t index, intptr_t value, void *ptr, float opt) {
    switch (opcode) {
    case audioMasterAutomate:
        return 1;
    case audioMasterVersion:    return 2400;
    case audioMasterCurrentId:  return 0;
    case audioMasterIdle:       return 1;
    case audioMasterWantMidi:   return 1;
    case audioMasterGetTime:    return 0;
    case audioMasterProcessEvents: return 1;
    case audioMasterGetSampleRate: return static_cast<intptr_t>(s_sample_rate);
    case audioMasterGetBlockSize:  return static_cast<intptr_t>(s_max_frames);
    case audioMasterGetInputLatency: return 0;
    case audioMasterGetOutputLatency: return 0;
    case audioMasterGetCurrentProcessLevel: return 0;
    case audioMasterGetAutomationState:
        return kVstAutomationReading |
               (s_editor_open_edit_depth.load() > 0 ? kVstAutomationWriting : 0);
    case audioMasterGetVendorString:
        if (ptr) {
            strncpy(static_cast<char *>(ptr), "Infinite Loop Audio", 64);
            static_cast<char *>(ptr)[63] = '\0';
            return 1;
        }
        return 0;
    case audioMasterGetProductString:
        if (ptr) {
            strncpy(static_cast<char *>(ptr), "Keepsake", 64);
            static_cast<char *>(ptr)[63] = '\0';
            return 1;
        }
        return 0;
    case audioMasterGetVendorVersion: return 1;
    case audioMasterCanDo:
        if (!ptr) return 0;
        if (strcmp(static_cast<const char *>(ptr), "sendVstEvents") == 0) return 1;
        if (strcmp(static_cast<const char *>(ptr), "sendVstMidiEvent") == 0) return 1;
        if (strcmp(static_cast<const char *>(ptr), "receiveVstEvents") == 0) return 1;
        if (strcmp(static_cast<const char *>(ptr), "receiveVstMidiEvent") == 0) return 1;
        if (strcmp(static_cast<const char *>(ptr), "sizeWindow") == 0) return 1;
        if (strcmp(static_cast<const char *>(ptr), "supportShell") == 0) return 1;
        return 0;
    case audioMasterGetLanguage: return kVstLangEnglish;
    case audioMasterSizeWindow: return 1;
    case audioMasterUpdateDisplay: return 1;
    case audioMasterBeginEdit:
        s_editor_open_edit_depth.fetch_add(1);
        return 1;
    case audioMasterEndEdit: {
        uint32_t depth = s_editor_open_edit_depth.load();
        if (depth > 0) s_editor_open_edit_depth.fetch_sub(1);
        return 1;
    }
    case audioMasterIOChanged:  return 1;
    default: return 0;
    }
}

#ifdef __APPLE__
static std::string resolve_vst_bundle(const std::string &path) {
    if (path.size() < 4 || path.substr(path.size() - 4) != ".vst")
        return path;
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(
        kCFAllocatorDefault, reinterpret_cast<const UInt8 *>(path.c_str()),
        static_cast<CFIndex>(path.size()), true);
    if (!url) return path;
    CFBundleRef bundle = CFBundleCreate(kCFAllocatorDefault, url);
    CFRelease(url);
    if (!bundle) return path;
    CFURLRef exec = CFBundleCopyExecutableURL(bundle);
    CFRelease(bundle);
    if (!exec) return path;
    char buf[4096];
    Boolean ok = CFURLGetFileSystemRepresentation(
        exec, true, reinterpret_cast<UInt8 *>(buf), sizeof(buf));
    CFRelease(exec);
    return ok ? std::string(buf) : path;
}
#endif

class Vst2Loader : public BridgeLoader {
    AEffect *effect = nullptr;
    void *lib = nullptr;
    bool active = false;

    // MIDI queue
    static constexpr int MAX_MIDI = 512;
    VstMidiEvent midi_queue[MAX_MIDI];
    int midi_count = 0;

public:
    bool load(const std::string &path) override {
        std::string load_path = path;
#ifdef __APPLE__
        load_path = resolve_vst_bundle(path);
#endif
#ifndef _WIN32
        lib = dlopen(load_path.c_str(), RTLD_LAZY | RTLD_LOCAL);
#else
        lib = (void *)LoadLibraryA(load_path.c_str());
#endif
        if (!lib) {
            fprintf(stderr, "bridge/vst2: failed to load '%s'\n", load_path.c_str());
            return false;
        }

#ifndef _WIN32
        auto entry = reinterpret_cast<VstEntry>(dlsym(lib, "VSTPluginMain"));
        if (!entry) entry = reinterpret_cast<VstEntry>(dlsym(lib, "main"));
#else
        auto entry = reinterpret_cast<VstEntry>(
            GetProcAddress((HMODULE)lib, "VSTPluginMain"));
        if (!entry) entry = reinterpret_cast<VstEntry>(
            GetProcAddress((HMODULE)lib, "main"));
#endif
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
        intptr_t result =
            effect->dispatcher(effect, effEditOpen, 0, 0, parent, 0.0f);
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
        struct ERect { int16_t top, left, bottom, right; };
        ERect *rect = nullptr;
        if (effect->dispatcher)
            effect->dispatcher(effect, effEditGetRect, 0, 0, &rect, 0);
        if (!rect) return false;
        w = rect->right - rect->left;
        h = rect->bottom - rect->top;
        return (w > 0 && h > 0);
    }

    void close() override {
        if (effect) {
            if (active)
                effect->dispatcher(effect, effMainsChanged, 0, 0, nullptr, 0);
            effect->dispatcher(effect, effClose, 0, 0, nullptr, 0);
            effect = nullptr;
        }
#ifndef _WIN32
        if (lib) { dlclose(lib); lib = nullptr; }
#endif
    }

    AEffect *get_effect() { return effect; }
};

BridgeLoader *create_vst2_loader() { return new Vst2Loader(); }
