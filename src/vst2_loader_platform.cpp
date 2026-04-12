#include "vst2_loader_internal.h"

#include <cstring>
#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <mach-o/fat.h>
#include <mach-o/loader.h>
#endif

#ifdef _WIN32
typedef HMODULE LibHandle;
static LibHandle lib_open(const char *path) { return LoadLibraryA(path); }
static void lib_close(LibHandle h) { FreeLibrary(h); }
#else
typedef void *LibHandle;
static LibHandle lib_open(const char *path) { return dlopen(path, RTLD_LAZY | RTLD_LOCAL); }
#endif

#ifdef __APPLE__
static std::string resolve_macos_bundle(const std::string &path) {
    if (path.size() < 4 || path.substr(path.size() - 4) != ".vst") return path;

    CFURLRef bundle_url = CFURLCreateFromFileSystemRepresentation(
        kCFAllocatorDefault,
        reinterpret_cast<const UInt8 *>(path.c_str()),
        static_cast<CFIndex>(path.size()),
        true);
    if (!bundle_url) return path;

    CFBundleRef bundle = CFBundleCreate(kCFAllocatorDefault, bundle_url);
    CFRelease(bundle_url);
    if (!bundle) return path;

    CFURLRef exec_url = CFBundleCopyExecutableURL(bundle);
    CFRelease(bundle);
    if (!exec_url) return path;

    char resolved[4096];
    Boolean ok = CFURLGetFileSystemRepresentation(exec_url, true,
        reinterpret_cast<UInt8 *>(resolved), sizeof(resolved));
    CFRelease(exec_url);
    return ok ? std::string(resolved) : path;
}

static const char *check_macho_arch(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return nullptr;

    uint32_t magic = 0;
    if (fread(&magic, sizeof(magic), 1, f) != 1) {
        fclose(f);
        return nullptr;
    }

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

    cpu_type_t cputype = 0;
    if (magic == MH_MAGIC_64) {
        struct mach_header_64 header;
        fseek(f, 0, SEEK_SET);
        if (fread(&header, sizeof(header), 1, f) == 1) cputype = header.cputype;
    } else if (magic == MH_MAGIC) {
        struct mach_header header32;
        fseek(f, 0, SEEK_SET);
        if (fread(&header32, sizeof(header32), 1, f) == 1) cputype = header32.cputype;
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

#if defined(_WIN32) || !defined(__APPLE__)
static const char *check_binary_arch(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return nullptr;

    uint8_t header[64];
    size_t n = fread(header, 1, sizeof(header), f);
    fclose(f);
    if (n < 20) return nullptr;

    if (header[0] == 'M' && header[1] == 'Z') {
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

        if (pe_hdr[0] != 'P' || pe_hdr[1] != 'E' || pe_hdr[2] != 0 || pe_hdr[3] != 0) {
            return nullptr;
        }

        uint16_t machine = *reinterpret_cast<uint16_t *>(pe_hdr + 4);
        bool is_native = false;
#if defined(_M_X64) || defined(__x86_64__)
        is_native = (machine == 0x8664);
#elif defined(_M_IX86) || defined(__i386__)
        is_native = (machine == 0x014C);
#elif defined(_M_ARM64) || defined(__aarch64__)
        is_native = (machine == 0xAA64);
#endif
        if (is_native) return "native";
        if (machine == 0x014C) return "x86";
        if (machine == 0x8664) return "x86_64";
        if (machine == 0xAA64) return "arm64";
        return nullptr;
    }

    if (header[0] == 0x7F && header[1] == 'E' &&
        header[2] == 'L' && header[3] == 'F') {
        uint16_t e_machine = *reinterpret_cast<uint16_t *>(header + 18);
        bool is_native = false;
#if defined(__x86_64__)
        is_native = (e_machine == 62);
#elif defined(__i386__)
        is_native = (e_machine == 3);
#elif defined(__aarch64__)
        is_native = (e_machine == 183);
#endif
        if (is_native) return "native";
        if (e_machine == 3) return "x86";
        if (e_machine == 62) return "x86_64";
        if (e_machine == 183) return "arm64";
    }

    return nullptr;
}
#endif

bool vst2_resolve_load_path(const std::string &path, std::string &load_path) {
    load_path = path;
#ifdef __APPLE__
    load_path = resolve_macos_bundle(path);
#endif
    return true;
}

bool vst2_try_load_library(const std::string &path, std::string &load_path, void *&lib) {
    vst2_resolve_load_path(path, load_path);
    lib = reinterpret_cast<void *>(lib_open(load_path.c_str()));
    if (lib) return true;

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
