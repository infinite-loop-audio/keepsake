#pragma once
//
// Keepsake platform abstraction — pipe I/O.
//

static inline bool platform_write(PlatformPipe p, const void *buf, size_t n) {
#ifdef _WIN32
    const uint8_t *ptr = static_cast<const uint8_t *>(buf);
    size_t rem = n;
    while (rem > 0) {
        DWORD written;
        if (!WriteFile(p, ptr, static_cast<DWORD>(rem), &written, nullptr))
            return false;
        ptr += written;
        rem -= written;
    }
    return true;
#else
    const uint8_t *ptr = static_cast<const uint8_t *>(buf);
    size_t rem = n;
    while (rem > 0) {
        ssize_t w = write(p, ptr, rem);
        if (w <= 0) return false;
        ptr += w;
        rem -= static_cast<size_t>(w);
    }
    return true;
#endif
}

static inline bool platform_read(PlatformPipe p, void *buf, size_t n) {
#ifdef _WIN32
    uint8_t *ptr = static_cast<uint8_t *>(buf);
    size_t rem = n;
    while (rem > 0) {
        DWORD nread;
        if (!ReadFile(p, ptr, static_cast<DWORD>(rem), &nread, nullptr) || nread == 0)
            return false;
        ptr += nread;
        rem -= nread;
    }
    return true;
#else
    uint8_t *ptr = static_cast<uint8_t *>(buf);
    size_t rem = n;
    while (rem > 0) {
        ssize_t r = read(p, ptr, rem);
        if (r <= 0) return false;
        ptr += r;
        rem -= static_cast<size_t>(r);
    }
    return true;
#endif
}

static inline bool platform_read_ready(PlatformPipe p, int timeout_ms) {
#ifdef _WIN32
    if (timeout_ms < 0) return true;
    DWORD avail = 0;
    DWORD elapsed = 0;
    while (elapsed < static_cast<DWORD>(timeout_ms)) {
        if (PeekNamedPipe(p, nullptr, 0, nullptr, &avail, nullptr) && avail > 0)
            return true;
        Sleep(1);
        elapsed++;
    }
    return false;
#else
    if (timeout_ms < 0) return true;
    struct pollfd pfd = { p, POLLIN, 0 };
    int r = poll(&pfd, 1, timeout_ms);
    return r > 0 && (pfd.revents & POLLIN);
#endif
}
