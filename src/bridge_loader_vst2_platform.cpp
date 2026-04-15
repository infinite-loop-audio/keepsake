//
// Bridge loader — VST2 platform/load helpers.
//

#include "bridge_loader.h"
#include "debug_log.h"

#include <vestige/vestige.h>
#include <atomic>
#include <cstdio>
#include <cstring>

#ifndef _WIN32
#include <dlfcn.h>
#else
#include <windows.h>
#endif

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

typedef AEffect *(__cdecl *VstEntry)(audioMasterCallback);

static constexpr intptr_t kKeepsakeVstProcessLevelUser = 1;
static constexpr intptr_t kKeepsakeVstAutomationReadWrite =
    kVstAutomationReading | kVstAutomationWriting;

double s_sample_rate = 44100.0;
uint32_t s_max_frames = 512;
std::atomic<uint32_t> s_editor_open_edit_depth{0};
std::atomic<bool> s_editor_open_in_progress{false};
std::atomic<int32_t> s_last_automated_param{-1};
std::atomic<uint32_t> s_automate_count{0};
std::atomic<uint32_t> s_begin_edit_count{0};
std::atomic<uint32_t> s_end_edit_count{0};
std::atomic<float> s_last_automated_value{0.0f};
VstTimeInfo s_vst_time_info{};

static const char *vst2_host_opcode_name(int32_t opcode) {
    switch (opcode) {
    case audioMasterAutomate: return "audioMasterAutomate";
    case audioMasterVersion: return "audioMasterVersion";
    case audioMasterCurrentId: return "audioMasterCurrentId";
    case audioMasterIdle: return "audioMasterIdle";
    case audioMasterWantMidi: return "audioMasterWantMidi";
    case audioMasterGetTime: return "audioMasterGetTime";
    case audioMasterProcessEvents: return "audioMasterProcessEvents";
    case audioMasterGetSampleRate: return "audioMasterGetSampleRate";
    case audioMasterGetBlockSize: return "audioMasterGetBlockSize";
    case audioMasterGetInputLatency: return "audioMasterGetInputLatency";
    case audioMasterGetOutputLatency: return "audioMasterGetOutputLatency";
    case audioMasterGetCurrentProcessLevel: return "audioMasterGetCurrentProcessLevel";
    case audioMasterGetAutomationState: return "audioMasterGetAutomationState";
    case audioMasterGetVendorString: return "audioMasterGetVendorString";
    case audioMasterGetProductString: return "audioMasterGetProductString";
    case audioMasterGetVendorVersion: return "audioMasterGetVendorVersion";
    case audioMasterCanDo: return "audioMasterCanDo";
    case audioMasterGetLanguage: return "audioMasterGetLanguage";
    case audioMasterSizeWindow: return "audioMasterSizeWindow";
    case audioMasterUpdateDisplay: return "audioMasterUpdateDisplay";
    case audioMasterBeginEdit: return "audioMasterBeginEdit";
    case audioMasterEndEdit: return "audioMasterEndEdit";
    case audioMasterIOChanged: return "audioMasterIOChanged";
    default: return "audioMaster?";
    }
}

static void trace_editor_open_callback(int32_t opcode,
                                       int32_t index,
                                       intptr_t value,
                                       void *ptr,
                                       float opt,
                                       intptr_t result) {
    if (!s_editor_open_in_progress.load()) return;
    if (opcode == audioMasterGetTime) return;
    if (opcode == audioMasterCanDo && ptr) {
        keepsake_debug_log("bridge/vst2: hostcb %s query='%s' -> %lld\n",
                           vst2_host_opcode_name(opcode),
                           static_cast<const char *>(ptr),
                           static_cast<long long>(result));
        return;
    }
    keepsake_debug_log("bridge/vst2: hostcb %s idx=%d val=%lld ptr=%p opt=%.3f -> %lld\n",
                       vst2_host_opcode_name(opcode),
                       opcode == audioMasterSizeWindow ? index : index,
                       static_cast<long long>(value), ptr, opt,
                       static_cast<long long>(result));
}

intptr_t __cdecl vst2_host_callback(
    AEffect *effect, int32_t opcode, int32_t index, intptr_t value, void *ptr, float opt) {
    intptr_t result = 0;
    switch (opcode) {
    case audioMasterAutomate:
        s_last_automated_param.store(index);
        s_last_automated_value.store(opt);
        s_automate_count.fetch_add(1);
        result = 1;
        break;
    case audioMasterVersion: result = 2400; break;
    case audioMasterCurrentId:
        result = effect ? effect->uniqueID : 0;
        break;
    case audioMasterIdle: result = 1; break;
    case audioMasterWantMidi: result = 1; break;
    case audioMasterGetTime: result = reinterpret_cast<intptr_t>(&s_vst_time_info); break;
    case audioMasterProcessEvents: result = 1; break;
    case audioMasterGetSampleRate: result = static_cast<intptr_t>(s_sample_rate); break;
    case audioMasterGetBlockSize: result = static_cast<intptr_t>(s_max_frames); break;
    case audioMasterGetInputLatency: result = 0; break;
    case audioMasterGetOutputLatency: result = 0; break;
    case audioMasterGetCurrentProcessLevel: result = kKeepsakeVstProcessLevelUser; break;
    case audioMasterGetAutomationState:
        if (s_editor_open_in_progress.load()) result = kKeepsakeVstAutomationReadWrite;
        else result = kVstAutomationReading;
        break;
    case audioMasterGetVendorString:
        if (ptr) {
            strncpy(static_cast<char *>(ptr), "Infinite Loop Audio", 64);
            static_cast<char *>(ptr)[63] = '\0';
            result = 1;
            break;
        }
        result = 0;
        break;
    case audioMasterGetProductString:
        if (ptr) {
            strncpy(static_cast<char *>(ptr), "Keepsake", 64);
            static_cast<char *>(ptr)[63] = '\0';
            result = 1;
            break;
        }
        result = 0;
        break;
    case audioMasterGetVendorVersion: result = 1; break;
    case audioMasterCanDo:
        if (!ptr) {
            result = 0;
            break;
        }
        if (strcmp(static_cast<const char *>(ptr), "sendVstEvents") == 0) result = 1;
        else if (strcmp(static_cast<const char *>(ptr), "sendVstMidiEvent") == 0) result = 1;
        else if (strcmp(static_cast<const char *>(ptr), "receiveVstEvents") == 0) result = 1;
        else if (strcmp(static_cast<const char *>(ptr), "receiveVstMidiEvent") == 0) result = 1;
        else if (strcmp(static_cast<const char *>(ptr), "sizeWindow") == 0) result = 1;
        else if (strcmp(static_cast<const char *>(ptr), "supportShell") == 0) result = 1;
        else result = 0;
        break;
    case audioMasterGetLanguage: result = kVstLangEnglish; break;
    case audioMasterSizeWindow: result = 1; break;
    case audioMasterUpdateDisplay: result = 1; break;
    case audioMasterBeginEdit:
        s_editor_open_edit_depth.fetch_add(1);
        s_begin_edit_count.fetch_add(1);
        result = 1;
        break;
    case audioMasterEndEdit: {
        uint32_t depth = s_editor_open_edit_depth.load();
        if (depth > 0) s_editor_open_edit_depth.fetch_sub(1);
        s_end_edit_count.fetch_add(1);
        result = 1;
        break;
    }
    case audioMasterIOChanged: result = 1; break;
    default: result = 0; break;
    }
    trace_editor_open_callback(opcode, index, value, ptr, opt, result);
    return result;
}

#ifdef __APPLE__
static std::string resolve_vst_bundle(const std::string &path) {
    if (path.size() < 4 || path.substr(path.size() - 4) != ".vst") return path;
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

void *vst2_open_library(const std::string &path, std::string &load_path) {
    load_path = path;
#ifdef __APPLE__
    load_path = resolve_vst_bundle(path);
#endif
#ifndef _WIN32
    return dlopen(load_path.c_str(), RTLD_LAZY | RTLD_LOCAL);
#else
    return (void *)LoadLibraryA(load_path.c_str());
#endif
}

void *vst2_lookup_entry(void *lib) {
#ifndef _WIN32
    auto entry = reinterpret_cast<VstEntry>(dlsym(lib, "VSTPluginMain"));
    if (!entry) entry = reinterpret_cast<VstEntry>(dlsym(lib, "main"));
#else
    auto entry = reinterpret_cast<VstEntry>(GetProcAddress((HMODULE)lib, "VSTPluginMain"));
    if (!entry) entry = reinterpret_cast<VstEntry>(GetProcAddress((HMODULE)lib, "main"));
#endif
    return reinterpret_cast<void *>(entry);
}

void vst2_close_library(void *lib) {
#ifndef _WIN32
    if (lib) dlclose(lib);
#else
    (void)lib;
#endif
}
