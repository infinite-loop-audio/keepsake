#pragma once
//
// Keepsake platform abstraction — plugin-path helpers.
//

static inline const char *platform_vst2_extension() {
#ifdef __APPLE__
    return ".vst";
#elif defined(_WIN32)
    return ".dll";
#else
    return ".so";
#endif
}

static inline bool platform_is_vst2(const std::string &path, bool is_dir) {
#ifdef __APPLE__
    return is_dir && path.size() >= 4 &&
           path.substr(path.size() - 4) == ".vst";
#elif defined(_WIN32)
    return !is_dir && path.size() >= 4 &&
           path.substr(path.size() - 4) == ".dll";
#else
    return !is_dir && path.size() >= 3 &&
           path.substr(path.size() - 3) == ".so";
#endif
}
