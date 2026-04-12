//
// Bridge loader — VST2 platform/load helpers.
//

#include "bridge_loader.h"

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

double s_sample_rate = 44100.0;
uint32_t s_max_frames = 512;
std::atomic<uint32_t> s_editor_open_edit_depth{0};

intptr_t __cdecl vst2_host_callback(
    AEffect *, int32_t opcode, int32_t index, intptr_t value, void *ptr, float opt) {
    switch (opcode) {
    case audioMasterAutomate: return 1;
    case audioMasterVersion: return 2400;
    case audioMasterCurrentId: return 0;
    case audioMasterIdle: return 1;
    case audioMasterWantMidi: return 1;
    case audioMasterGetTime: return 0;
    case audioMasterProcessEvents: return 1;
    case audioMasterGetSampleRate: return static_cast<intptr_t>(s_sample_rate);
    case audioMasterGetBlockSize: return static_cast<intptr_t>(s_max_frames);
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
    case audioMasterIOChanged: return 1;
    default: return 0;
    }
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
