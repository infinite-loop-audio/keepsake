#include "vst2_loader_internal.h"
#include <vestige/vestige.h>
#include <cstring>
#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

// --- Platform-specific library loading ---

#ifdef _WIN32
typedef HMODULE LibHandle;
static LibHandle lib_open(const char *path) { return LoadLibraryA(path); }
static void *lib_sym(LibHandle h, const char *name) { return (void *)GetProcAddress(h, name); }
static void lib_close(LibHandle h) { FreeLibrary(h); }
#else
typedef void *LibHandle;
static LibHandle lib_open(const char *path) { return dlopen(path, RTLD_LAZY | RTLD_LOCAL); }
static void *lib_sym(LibHandle h, const char *name) { return dlsym(h, name); }
static void lib_close(LibHandle h) { dlclose(h); }
#endif

// --- Minimal audioMasterCallback ---
// We only need to respond to version queries during metadata extraction.

static intptr_t __cdecl host_callback(
    AEffect * /*effect*/,
    int32_t opcode,
    int32_t /*index*/,
    intptr_t /*value*/,
    void * /*ptr*/,
    float /*opt*/)
{
    switch (opcode) {
    case audioMasterVersion:
        return 2400; // Report VST 2.4
    case audioMasterCurrentId:
        return 0;
    case audioMasterGetVendorString:
        return 0;
    case audioMasterGetProductString:
        return 0;
    case audioMasterGetVendorVersion:
        return 1000;
    case audioMasterCanDo:
        return 0;
    case audioMasterGetSampleRate:
        return 44100;
    case audioMasterGetBlockSize:
        return 512;
    case audioMasterGetCurrentProcessLevel:
        return 0;
    case audioMasterGetAutomationState:
        return 0;
    default:
        return 0;
    }
}

// --- Entry point function type ---

typedef AEffect *(__cdecl *VstEntryPoint)(audioMasterCallback);

std::string vst2_filename_stem(const std::string &path) {
    size_t last_sep = path.find_last_of("/\\");
    size_t start = (last_sep != std::string::npos) ? last_sep + 1 : 0;
    size_t dot = path.find_last_of('.');
    if (dot != std::string::npos && dot > start) {
        return path.substr(start, dot - start);
    }
    return path.substr(start);
}

bool vst2_bridge_info_is_sane(const Vst2PluginInfo &info) {
    if (info.num_inputs < 0 || info.num_inputs > 64) return false;
    if (info.num_outputs < 0 || info.num_outputs > 64) return false;
    if (info.num_params < 0 || info.num_params > 100000) return false;
    if (info.category < 0 || info.category > 32) return false;
    return true;
}

// --- Public API ---

bool vst2_load_metadata(const std::string &path, Vst2PluginInfo &info) {
    info.file_path = path;
    info.binary_arch = vst2_detect_binary_arch(path);

    std::string load_path;
    void *lib_void = nullptr;
    if (!vst2_try_load_library(path, load_path, lib_void)) return false;
    LibHandle lib = reinterpret_cast<LibHandle>(lib_void);

    // Find the VST2 entry point
    VstEntryPoint entry = reinterpret_cast<VstEntryPoint>(lib_sym(lib, "VSTPluginMain"));
    if (!entry) {
        entry = reinterpret_cast<VstEntryPoint>(lib_sym(lib, "main"));
    }
    if (!entry) {
        fprintf(stderr, "keepsake: no VST2 entry point in '%s'\n", load_path.c_str());
        lib_close(lib);
        return false;
    }

    // Call the entry point to get the AEffect
    AEffect *effect = entry(host_callback);
    if (!effect) {
        fprintf(stderr, "keepsake: entry point returned null for '%s'\n", load_path.c_str());
        lib_close(lib);
        return false;
    }

    // Validate magic number
    if (effect->magic != kEffectMagic) {
        fprintf(stderr, "keepsake: bad magic 0x%08X in '%s' (expected 0x%08X)\n",
                effect->magic, load_path.c_str(), kEffectMagic);
        lib_close(lib);
        return false;
    }

    // Read AEffect fields
    info.unique_id = effect->uniqueID;
    info.flags = effect->flags;
    info.num_inputs = effect->numInputs;
    info.num_outputs = effect->numOutputs;
    info.num_params = effect->numParams;
    info.num_programs = effect->numPrograms;

    // Extract metadata via dispatcher
    char buf[256];

    // Effect name (opcode 45)
    memset(buf, 0, sizeof(buf));
    if (effect->dispatcher) {
        effect->dispatcher(effect, effGetEffectName, 0, 0, buf, 0.0f);
    }
    info.name = buf;

    // Fall back to product string (opcode 48) if name is empty
    if (info.name.empty() && effect->dispatcher) {
        memset(buf, 0, sizeof(buf));
        effect->dispatcher(effect, effGetProductString, 0, 0, buf, 0.0f);
        info.name = buf;
    }

    // Fall back to filename stem if still empty
    if (info.name.empty()) {
        info.name = vst2_filename_stem(path);
    }

    // Vendor string (opcode 47)
    memset(buf, 0, sizeof(buf));
    if (effect->dispatcher) {
        effect->dispatcher(effect, effGetVendorString, 0, 0, buf, 0.0f);
    }
    info.vendor = buf;
    if (info.vendor.empty()) {
        info.vendor = "Unknown";
    }

    // Product string (opcode 48)
    memset(buf, 0, sizeof(buf));
    if (effect->dispatcher) {
        effect->dispatcher(effect, effGetProductString, 0, 0, buf, 0.0f);
    }
    info.product = buf;

    // Vendor version (opcode 49)
    info.vendor_version = 0;
    if (effect->dispatcher) {
        info.vendor_version = static_cast<int32_t>(
            effect->dispatcher(effect, effGetVendorVersion, 0, 0, nullptr, 0.0f));
    }

    // Plugin category (opcode 35)
    info.category = kPlugCategUnknown;
    if (effect->dispatcher) {
        info.category = static_cast<int32_t>(
            effect->dispatcher(effect, effGetPlugCategory, 0, 0, nullptr, 0.0f));
    }

    // Close the plugin — we only needed metadata
    if (effect->dispatcher) {
        effect->dispatcher(effect, effClose, 0, 0, nullptr, 0.0f);
    }

    lib_close(lib);

    fprintf(stderr, "keepsake: loaded '%s' — id=0x%08X name='%s' vendor='%s' category=%d\n",
            path.c_str(), static_cast<unsigned>(info.unique_id),
            info.name.c_str(), info.vendor.c_str(), info.category);

    return true;
}

bool vst2_parse_init_response(const std::vector<uint8_t> &payload,
                              Vst2PluginInfo &info) {
    // Response from multi-instance bridge: [instance_id][IpcPluginInfo][strings]
    if (payload.size() < 4 + sizeof(IpcPluginInfo)) return false;

    // Skip instance_id (first 4 bytes)
    size_t off = 4;
    IpcPluginInfo pi;
    memcpy(&pi, payload.data() + off, sizeof(pi));
    off += sizeof(pi);

    info.unique_id = pi.unique_id;
    info.num_inputs = pi.num_inputs;
    info.num_outputs = pi.num_outputs;
    info.num_params = pi.num_params;
    info.flags = pi.flags;
    info.category = pi.category;
    info.vendor_version = pi.vendor_version;

    auto read_str = [&]() -> std::string {
        if (off >= payload.size()) return {};
        const char *s = reinterpret_cast<const char *>(payload.data() + off);
        size_t max_len = payload.size() - off;
        size_t len = strnlen(s, max_len);
        off += len + 1;
        return std::string(s, len);
    };
    info.name = read_str();
    info.vendor = read_str();
    info.product = read_str();
    return true;
}
