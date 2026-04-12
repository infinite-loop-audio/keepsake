//
// Bridge loader — VST3 module/platform helpers.
//

#include "bridge_loader_vst3_internal.h"

#if !defined(INIT_CLASS_IID)
namespace Steinberg {
    DEF_CLASS_IID(FUnknown)
    DEF_CLASS_IID(IBStream)
    DEF_CLASS_IID(IPluginFactory)
    DEF_CLASS_IID(IPluginFactory2)
    DEF_CLASS_IID(IPluginBase)
    namespace Vst {
        DEF_CLASS_IID(IComponent)
        DEF_CLASS_IID(IAudioProcessor)
        DEF_CLASS_IID(IEditController)
    }
}
#endif

tresult PLUGIN_API MemoryStream::queryInterface(const TUID, void **) { return kNoInterface; }

uint32 PLUGIN_API MemoryStream::addRef() { return ++refCount; }

uint32 PLUGIN_API MemoryStream::release() {
    if (--refCount == 0) {
        delete this;
        return 0;
    }
    return refCount;
}

tresult PLUGIN_API MemoryStream::read(void *buffer, int32 numBytes, int32 *numBytesRead) {
    int32 avail = static_cast<int32>(data.size() - static_cast<size_t>(pos));
    int32 toRead = (numBytes < avail) ? numBytes : avail;
    if (toRead > 0) {
        memcpy(buffer, data.data() + pos, static_cast<size_t>(toRead));
        pos += toRead;
    }
    if (numBytesRead) *numBytesRead = toRead;
    return kResultOk;
}

tresult PLUGIN_API MemoryStream::write(void *buffer, int32 numBytes, int32 *numBytesWritten) {
    size_t end = static_cast<size_t>(pos) + static_cast<size_t>(numBytes);
    if (end > data.size()) data.resize(end);
    memcpy(data.data() + pos, buffer, static_cast<size_t>(numBytes));
    pos += numBytes;
    if (numBytesWritten) *numBytesWritten = numBytes;
    return kResultOk;
}

tresult PLUGIN_API MemoryStream::seek(int64 p, int32 mode, int64 *result) {
    switch (mode) {
    case kIBSeekSet: pos = p; break;
    case kIBSeekCur: pos += p; break;
    case kIBSeekEnd: pos = static_cast<int64>(data.size()) + p; break;
    }
    if (pos < 0) pos = 0;
    if (result) *result = pos;
    return kResultOk;
}

tresult PLUGIN_API MemoryStream::tell(int64 *p) {
    if (p) *p = pos;
    return kResultOk;
}

const std::vector<uint8_t> &MemoryStream::getData() const { return data; }

void MemoryStream::setData(const uint8_t *d, size_t sz) {
    data.assign(d, d + sz);
    pos = 0;
}

void MemoryStream::reset() { pos = 0; }

bool vst3_resolve_binary_path(const std::string &path, std::string &resolved_path) {
    resolved_path = path;
#ifdef __APPLE__
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(
        kCFAllocatorDefault, reinterpret_cast<const UInt8 *>(path.c_str()),
        static_cast<CFIndex>(path.size()), true);
    if (!url) return true;
    CFBundleRef bundle = CFBundleCreate(kCFAllocatorDefault, url);
    CFRelease(url);
    if (!bundle) return true;
    CFURLRef exec = CFBundleCopyExecutableURL(bundle);
    CFRelease(bundle);
    if (!exec) return true;
    char buf[4096];
    Boolean ok = CFURLGetFileSystemRepresentation(
        exec, true, reinterpret_cast<UInt8 *>(buf), sizeof(buf));
    CFRelease(exec);
    if (ok) resolved_path = buf;
    return true;
#elif defined(_WIN32)
    return true;
#else
    std::string stem = path;
    size_t last_slash = stem.find_last_of('/');
    size_t last_dot = stem.find_last_of('.');
    if (last_slash != std::string::npos && last_dot != std::string::npos) {
        stem = stem.substr(last_slash + 1, last_dot - last_slash - 1);
    }
    std::string bundle_path = path + "/Contents/x86_64-linux/" + stem + ".so";
    FILE *f = fopen(bundle_path.c_str(), "rb");
    if (f) {
        fclose(f);
        resolved_path = bundle_path;
    }
    return true;
#endif
}

bool vst3_open_module(const std::string &path, void *&lib) {
    std::string resolved_path;
    if (!vst3_resolve_binary_path(path, resolved_path)) return false;
#ifndef _WIN32
    lib = dlopen(resolved_path.c_str(), RTLD_LAZY | RTLD_LOCAL);
#else
    lib = (void *)LoadLibraryA(resolved_path.c_str());
#endif
    if (!lib) {
        fprintf(stderr, "bridge/vst3: failed to load '%s'\n", resolved_path.c_str());
        return false;
    }
    return true;
}

void vst3_close_module(void *lib) {
#ifndef _WIN32
    if (lib) dlclose(lib);
#else
    if (lib) FreeLibrary((HMODULE)lib);
#endif
}

bool vst3_lookup_module_funcs(
    void *lib, InitModuleFunc &init_func, ExitModuleFunc &exit_func, GetFactoryFunc &factory_func) {
#ifndef _WIN32
    init_func = reinterpret_cast<InitModuleFunc>(dlsym(lib, "ModuleEntry"));
    exit_func = reinterpret_cast<ExitModuleFunc>(dlsym(lib, "ModuleExit"));
    factory_func = reinterpret_cast<GetFactoryFunc>(dlsym(lib, "GetPluginFactory"));
#else
    init_func = reinterpret_cast<InitModuleFunc>(GetProcAddress((HMODULE)lib, "InitDll"));
    exit_func = reinterpret_cast<ExitModuleFunc>(GetProcAddress((HMODULE)lib, "ExitDll"));
    factory_func =
        reinterpret_cast<GetFactoryFunc>(GetProcAddress((HMODULE)lib, "GetPluginFactory"));
#endif
    return factory_func != nullptr;
}
