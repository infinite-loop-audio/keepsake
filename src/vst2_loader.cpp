#include "vst2_loader.h"
#include <vestige/vestige.h>
#include <cstring>
#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <mach-o/loader.h>
#include <mach-o/fat.h>
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

// --- macOS bundle path resolution ---

#ifdef __APPLE__
// On macOS, VST2 plugins are bundles (.vst directories).
// If the path points to a .vst bundle, resolve to the actual
// Mach-O binary inside Contents/MacOS/.
static std::string resolve_macos_bundle(const std::string &path) {
    // Check if path ends with .vst
    if (path.size() < 4 || path.substr(path.size() - 4) != ".vst") {
        return path; // Not a bundle, use as-is
    }

    // Try CFBundle approach for reliable resolution
    CFURLRef bundle_url = CFURLCreateFromFileSystemRepresentation(
        kCFAllocatorDefault,
        reinterpret_cast<const UInt8 *>(path.c_str()),
        static_cast<CFIndex>(path.size()),
        true);

    if (!bundle_url) {
        return path;
    }

    CFBundleRef bundle = CFBundleCreate(kCFAllocatorDefault, bundle_url);
    CFRelease(bundle_url);

    if (!bundle) {
        return path;
    }

    CFURLRef exec_url = CFBundleCopyExecutableURL(bundle);
    CFRelease(bundle);

    if (!exec_url) {
        return path;
    }

    char resolved[4096];
    Boolean ok = CFURLGetFileSystemRepresentation(exec_url, true,
        reinterpret_cast<UInt8 *>(resolved), sizeof(resolved));
    CFRelease(exec_url);

    if (ok) {
        return std::string(resolved);
    }

    return path;
}
#endif

// --- macOS architecture detection ---

#ifdef __APPLE__
// Check whether a Mach-O binary contains a slice compatible with the
// current process architecture. Returns:
//   "native"  — binary matches or is universal with a matching slice
//   "x86_64"  — binary is x86_64 only (we're running on ARM)
//   "arm64"   — binary is arm64 only (we're running on x86_64)
//   nullptr   — can't determine (not a Mach-O, or read error)
static const char *check_macho_arch(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return nullptr;

    uint32_t magic = 0;
    if (fread(&magic, sizeof(magic), 1, f) != 1) {
        fclose(f);
        return nullptr;
    }

    // Fat (universal) binary — check which architecture slices are present
    if (magic == FAT_MAGIC || magic == FAT_CIGAM ||
        magic == FAT_MAGIC_64 || magic == FAT_CIGAM_64) {
        bool need_swap = (magic == FAT_CIGAM || magic == FAT_CIGAM_64);
        bool is_fat64 = (magic == FAT_MAGIC_64 || magic == FAT_CIGAM_64);

        struct fat_header fh;
        fseek(f, 0, SEEK_SET);
        if (fread(&fh, sizeof(fh), 1, f) != 1) {
            fclose(f);
            return nullptr;
        }

        uint32_t narch = need_swap ? __builtin_bswap32(fh.nfat_arch) : fh.nfat_arch;
        bool has_native = false;
        const char *other_arch = nullptr;

        for (uint32_t i = 0; i < narch && i < 16; i++) {
            cpu_type_t slice_cpu = 0;
            if (is_fat64) {
                struct fat_arch_64 fa;
                if (fread(&fa, sizeof(fa), 1, f) != 1) break;
                slice_cpu = need_swap
                    ? (cpu_type_t)__builtin_bswap32((uint32_t)fa.cputype)
                    : fa.cputype;
            } else {
                struct fat_arch fa;
                if (fread(&fa, sizeof(fa), 1, f) != 1) break;
                slice_cpu = need_swap
                    ? (cpu_type_t)__builtin_bswap32((uint32_t)fa.cputype)
                    : fa.cputype;
            }

#ifdef __arm64__
            if (slice_cpu == CPU_TYPE_ARM64) has_native = true;
            if (slice_cpu == CPU_TYPE_X86_64) other_arch = "x86_64";
            else if (slice_cpu == CPU_TYPE_I386 && !other_arch) other_arch = "i386";
#else
            if (slice_cpu == CPU_TYPE_X86_64) has_native = true;
            if (slice_cpu == CPU_TYPE_ARM64) other_arch = "arm64";
#endif
        }

        fclose(f);
        if (has_native) return "native";
        return other_arch ? other_arch : nullptr;
    }

    // Thin Mach-O — single architecture
    cpu_type_t cputype = 0;
    if (magic == MH_MAGIC_64) {
        struct mach_header_64 header;
        fseek(f, 0, SEEK_SET);
        if (fread(&header, sizeof(header), 1, f) == 1) {
            cputype = header.cputype;
        }
    } else if (magic == MH_MAGIC) {
        struct mach_header header32;
        fseek(f, 0, SEEK_SET);
        if (fread(&header32, sizeof(header32), 1, f) == 1) {
            cputype = header32.cputype;
        }
    }
    fclose(f);

    if (cputype == 0) return nullptr;

#ifdef __arm64__
    if (cputype == CPU_TYPE_ARM64) return "native";
    if (cputype == CPU_TYPE_X86_64) return "x86_64";
    if (cputype == CPU_TYPE_I386) return "i386";
#else
    if (cputype == CPU_TYPE_X86_64) return "native";
    if (cputype == CPU_TYPE_ARM64) return "arm64";
    if (cputype == CPU_TYPE_I386) return "i386";
#endif
    return nullptr;
}
#endif

// --- Windows PE architecture detection ---

#if defined(_WIN32) || !defined(__APPLE__)
// Check PE or ELF binary architecture. Returns:
//   "native"  — matches current process
//   "x86"     — 32-bit x86
//   "x86_64"  — 64-bit x86_64
//   "arm64"   — 64-bit ARM
//   nullptr   — can't determine
static const char *check_binary_arch(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return nullptr;

    uint8_t header[64];
    size_t n = fread(header, 1, sizeof(header), f);
    fclose(f);
    if (n < 20) return nullptr;

    // Check for PE (Windows .dll)
    if (header[0] == 'M' && header[1] == 'Z') {
        // MZ header — PE offset at 0x3C
        if (n < 0x40) return nullptr;
        uint32_t pe_offset = *reinterpret_cast<uint32_t *>(header + 0x3C);

        FILE *f2 = fopen(path, "rb");
        if (!f2) return nullptr;
        if (fseek(f2, static_cast<long>(pe_offset), SEEK_SET) != 0) {
            fclose(f2);
            return nullptr;
        }
        uint8_t pe_hdr[6];
        if (fread(pe_hdr, 1, 6, f2) != 6) {
            fclose(f2);
            return nullptr;
        }
        fclose(f2);

        // Verify PE signature
        if (pe_hdr[0] != 'P' || pe_hdr[1] != 'E' ||
            pe_hdr[2] != 0 || pe_hdr[3] != 0)
            return nullptr;

        uint16_t machine = *reinterpret_cast<uint16_t *>(pe_hdr + 4);
        bool is_native = false;

#if defined(_M_X64) || defined(__x86_64__)
        is_native = (machine == 0x8664); // IMAGE_FILE_MACHINE_AMD64
#elif defined(_M_IX86) || defined(__i386__)
        is_native = (machine == 0x014C); // IMAGE_FILE_MACHINE_I386
#elif defined(_M_ARM64) || defined(__aarch64__)
        is_native = (machine == 0xAA64); // IMAGE_FILE_MACHINE_ARM64
#endif
        if (is_native) return "native";
        if (machine == 0x014C) return "x86";
        if (machine == 0x8664) return "x86_64";
        if (machine == 0xAA64) return "arm64";
        return nullptr;
    }

    // Check for ELF (Linux .so)
    if (header[0] == 0x7F && header[1] == 'E' &&
        header[2] == 'L' && header[3] == 'F') {
        uint8_t ei_class = header[4]; // 1 = 32-bit, 2 = 64-bit
        // e_machine at offset 18 (both 32 and 64 bit ELF)
        uint16_t e_machine = *reinterpret_cast<uint16_t *>(header + 18);

        bool is_native = false;
#if defined(__x86_64__)
        is_native = (e_machine == 62); // EM_X86_64
#elif defined(__i386__)
        is_native = (e_machine == 3); // EM_386
#elif defined(__aarch64__)
        is_native = (e_machine == 183); // EM_AARCH64
#endif
        if (is_native) return "native";
        if (e_machine == 3) return "x86";
        if (e_machine == 62) return "x86_64";
        if (e_machine == 183) return "arm64";
        (void)ei_class;
        return nullptr;
    }

    return nullptr;
}
#endif

// --- Filename stem extraction ---

static std::string filename_stem(const std::string &path) {
    size_t last_sep = path.find_last_of("/\\");
    size_t start = (last_sep != std::string::npos) ? last_sep + 1 : 0;
    size_t dot = path.find_last_of('.');
    if (dot != std::string::npos && dot > start) {
        return path.substr(start, dot - start);
    }
    return path.substr(start);
}

static bool bridge_info_is_sane(const Vst2PluginInfo &info) {
    if (info.num_inputs < 0 || info.num_inputs > 64) return false;
    if (info.num_outputs < 0 || info.num_outputs > 64) return false;
    if (info.num_params < 0 || info.num_params > 100000) return false;
    if (info.category < 0 || info.category > 32) return false;
    return true;
}

// --- Public API ---

bool vst2_load_metadata(const std::string &path, Vst2PluginInfo &info) {
    info.file_path = path;

    // Resolve the loadable binary path
    std::string load_path = path;
#ifdef __APPLE__
    load_path = resolve_macos_bundle(path);
#endif

    // Load the shared library
    LibHandle lib = lib_open(load_path.c_str());
    if (!lib) {
        // Check if this is an architecture mismatch
        const char *arch = nullptr;
#ifdef __APPLE__
        arch = check_macho_arch(load_path.c_str());
#else
        arch = check_binary_arch(load_path.c_str());
#endif
        if (arch && strcmp(arch, "native") != 0) {
            fprintf(stderr, "keepsake: skipping '%s' — %s binary "
                    "(needs subprocess bridge for cross-architecture loading)\n",
                    path.c_str(), arch);
        } else {
#ifndef _WIN32
            const char *err = dlerror();
#else
            const char *err = "LoadLibrary failed";
#endif
            fprintf(stderr, "keepsake: failed to load '%s': %s\n",
                    load_path.c_str(), err ? err : "unknown error");
        }
        return false;
    }

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
        info.name = filename_stem(path);
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

// --- Cross-architecture metadata extraction via bridge subprocess ---

#include "ipc.h"
#include "platform.h"

bool vst2_load_metadata_via_bridge(const std::string &path,
                                    const std::string &bridge_binary,
                                    Vst2PluginInfo &info) {
    PlatformProcess proc;
    if (!platform_spawn(bridge_binary, proc)) {
        fprintf(stderr, "keepsake: failed to spawn bridge '%s' for scan\n",
                bridge_binary.c_str());
        return false;
    }

    // Send INIT with [format][path]
    uint32_t fmt = FORMAT_VST2;
    std::vector<uint8_t> init_payload(4 + path.size());
    memcpy(init_payload.data(), &fmt, 4);
    memcpy(init_payload.data() + 4, path.data(), path.size());
    ipc_write_instance_msg(proc.pipe_to, IPC_OP_INIT, 0,
                            init_payload.data(),
                            static_cast<uint32_t>(init_payload.size()));

    uint32_t op;
    std::vector<uint8_t> payload;
    if (!ipc_read_msg(proc.pipe_from, op, payload, 15000) || op != IPC_OP_OK) {
        fprintf(stderr, "keepsake: bridge scan failed for '%s'\n", path.c_str());
        ipc_write_instance_msg(proc.pipe_to, IPC_OP_SHUTDOWN, 0);
        platform_kill(proc);
        return false;
    }

    // Parse plugin info from OK payload: [instance_id][IpcPluginInfo][strings]
    if (payload.size() >= 4 + sizeof(IpcPluginInfo)) {
        size_t off = 4;
        IpcPluginInfo pi;
        memcpy(&pi, payload.data() + off, sizeof(pi));
        info.unique_id = pi.unique_id;
        info.num_inputs = pi.num_inputs;
        info.num_outputs = pi.num_outputs;
        info.num_params = pi.num_params;
        info.flags = pi.flags;
        info.category = pi.category;
        info.vendor_version = pi.vendor_version;

        // Parse null-terminated strings after the fixed fields
        off += sizeof(IpcPluginInfo);
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
    }

    if (!bridge_info_is_sane(info)) {
        fprintf(stderr,
                "keepsake: rejecting corrupt bridge scan metadata for '%s' (category=%d in=%d out=%d params=%d)\n",
                path.c_str(), info.category, info.num_inputs,
                info.num_outputs, info.num_params);
        ipc_write_msg(proc.pipe_to, IPC_OP_SHUTDOWN);
        ipc_read_msg(proc.pipe_from, op, payload, 5000);
        platform_kill(proc);
        return false;
    }

    info.file_path = path;
    info.needs_cross_arch = true;

    // Fallbacks per contract 002
    if (info.name.empty()) info.name = filename_stem(path);
    if (info.vendor.empty()) info.vendor = "Unknown";

    // Shutdown the bridge
    ipc_write_msg(proc.pipe_to, IPC_OP_SHUTDOWN);
    ipc_read_msg(proc.pipe_from, op, payload, 5000);
    platform_kill(proc);

    fprintf(stderr, "keepsake: scanned via bridge '%s' — name='%s' in=%d out=%d\n",
            path.c_str(), info.name.c_str(), info.num_inputs, info.num_outputs);

    return true;
}

// --- General-purpose bridge scanner (any format, crash-safe) ---

static bool parse_init_response(const std::vector<uint8_t> &payload,
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

bool scan_plugin_via_bridge(const std::string &path,
                             const std::string &bridge_binary,
                             uint32_t format,
                             Vst2PluginInfo &info) {
    PlatformProcess proc;
    if (!platform_spawn(bridge_binary, proc)) {
        fprintf(stderr, "keepsake: failed to spawn scanner bridge\n");
        return false;
    }

    // Send INIT with [instance_id=0][format][path]
    std::vector<uint8_t> init_payload(4 + path.size());
    memcpy(init_payload.data(), &format, 4);
    memcpy(init_payload.data() + 4, path.data(), path.size());

    ipc_write_instance_msg(proc.pipe_to, IPC_OP_INIT, 0,
                            init_payload.data(),
                            static_cast<uint32_t>(init_payload.size()));

    uint32_t op;
    std::vector<uint8_t> payload;
    // Cold cross-arch plugin discovery can take several seconds.
    // Give bridge-based metadata scans enough time to return once,
    // rather than timing out and making the host blacklist the CLAP.
    if (!ipc_read_msg(proc.pipe_from, op, payload, 15000)) {
        fprintf(stderr, "keepsake: scan timeout for '%s'\n", path.c_str());
        platform_kill(proc);
        return false;
    }

    if (op == IPC_OP_ERROR) {
        std::string msg(payload.begin(), payload.end());
        fprintf(stderr, "keepsake: scan error for '%s': %s\n",
                path.c_str(), msg.c_str());
        platform_kill(proc);
        return false;
    }

    if (op != IPC_OP_OK || !parse_init_response(payload, info)) {
        fprintf(stderr, "keepsake: scan failed for '%s' (op=0x%02X)\n",
                path.c_str(), op);
        platform_kill(proc);
        return false;
    }

    if (!bridge_info_is_sane(info)) {
        fprintf(stderr,
                "keepsake: rejecting corrupt scan metadata for '%s' (category=%d in=%d out=%d params=%d)\n",
                path.c_str(), info.category, info.num_inputs,
                info.num_outputs, info.num_params);
        platform_kill(proc);
        return false;
    }

    info.file_path = path;
    info.format = format;
    if (info.name.empty()) info.name = filename_stem(path);
    if (info.vendor.empty()) info.vendor = "Unknown";

    // Shutdown the bridge
    ipc_write_instance_msg(proc.pipe_to, IPC_OP_SHUTDOWN, 0);
    ipc_read_msg(proc.pipe_from, op, payload, 5000);
    platform_kill(proc);

    fprintf(stderr, "keepsake: scanned '%s' — name='%s' vendor='%s' format=%u\n",
            path.c_str(), info.name.c_str(), info.vendor.c_str(), format);
    return true;
}
