#pragma once
//
// Keepsake platform abstraction — shared memory helpers.
//

#ifdef _WIN32

static inline bool platform_shm_create(PlatformShm &s, const std::string &name,
                                       size_t size) {
    s.map_handle = CreateFileMappingA(
        INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
        0, static_cast<DWORD>(size), name.c_str());
    if (!s.map_handle) return false;

    s.ptr = MapViewOfFile(s.map_handle, FILE_MAP_ALL_ACCESS, 0, 0, size);
    if (!s.ptr) {
        CloseHandle(s.map_handle);
        return false;
    }

    memset(s.ptr, 0, size);
    s.name = name;
    s.size = size;
    s.owner = true;
    return true;
}

static inline bool platform_shm_open(PlatformShm &s, const std::string &name,
                                     size_t size) {
    s.map_handle = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
    if (!s.map_handle) return false;

    s.ptr = MapViewOfFile(s.map_handle, FILE_MAP_ALL_ACCESS, 0, 0, size);
    if (!s.ptr) {
        CloseHandle(s.map_handle);
        return false;
    }

    s.name = name;
    s.size = size;
    s.owner = false;
    return true;
}

static inline void platform_shm_close(PlatformShm &s) {
    if (s.ptr) UnmapViewOfFile(s.ptr);
    if (s.map_handle != INVALID_HANDLE_VALUE) CloseHandle(s.map_handle);
    s.ptr = nullptr;
    s.map_handle = INVALID_HANDLE_VALUE;
    s.owner = false;
}

static inline std::string platform_shm_name(const char *suffix) {
    char buf[128];
    snprintf(buf, sizeof(buf), "Local\\keepsake-%lu-%s",
             static_cast<unsigned long>(GetCurrentProcessId()), suffix);
    return buf;
}

#else

static inline bool platform_shm_create(PlatformShm &s, const std::string &name,
                                       size_t size) {
    shm_unlink(name.c_str());
    int fd = shm_open(name.c_str(), O_CREAT | O_RDWR, 0600);
    if (fd < 0) {
        perror("keepsake: shm_open");
        return false;
    }
    if (ftruncate(fd, static_cast<off_t>(size)) < 0) {
        perror("keepsake: ftruncate");
        close(fd);
        shm_unlink(name.c_str());
        return false;
    }
    void *ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
        perror("keepsake: mmap");
        close(fd);
        shm_unlink(name.c_str());
        return false;
    }
    memset(ptr, 0, size);
    s = { name, ptr, size, true, fd };
    return true;
}

static inline bool platform_shm_open(PlatformShm &s, const std::string &name,
                                     size_t size) {
    int fd = shm_open(name.c_str(), O_RDWR, 0600);
    if (fd < 0) {
        perror("keepsake: shm_open");
        return false;
    }
    void *ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
        perror("keepsake: mmap");
        close(fd);
        return false;
    }
    s = { name, ptr, size, false, fd };
    return true;
}

static inline void platform_shm_close(PlatformShm &s) {
    if (s.ptr && s.ptr != MAP_FAILED) munmap(s.ptr, s.size);
    if (s.fd >= 0) close(s.fd);
    if (s.owner && !s.name.empty()) shm_unlink(s.name.c_str());
    s.ptr = nullptr;
    s.fd = -1;
    s.owner = false;
}

static inline std::string platform_shm_name(const char *suffix) {
    char buf[128];
    snprintf(buf, sizeof(buf), "/keepsake-%d-%s", getpid(), suffix);
    return buf;
}

#endif
