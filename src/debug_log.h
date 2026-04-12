#pragma once

#include <cstdarg>
#include <cstdio>

#ifdef _WIN32
#include <windows.h>

static inline void keepsake_debug_vlog(const char *fmt, va_list args) {
    va_list stderr_args;
    va_copy(stderr_args, args);
    vfprintf(stderr, fmt, stderr_args);
    va_end(stderr_args);

    char temp_path[MAX_PATH];
    DWORD n = GetTempPathA(MAX_PATH, temp_path);
    if (n == 0 || n >= MAX_PATH) return;

    char log_path[MAX_PATH];
    snprintf(log_path, sizeof(log_path), "%skeepsake-debug.log", temp_path);

    FILE *f = fopen(log_path, "ab");
    if (!f) return;
    vfprintf(f, fmt, args);
    fclose(f);
}

static inline void keepsake_debug_log(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    keepsake_debug_vlog(fmt, args);
    va_end(args);
}
#else
static inline void keepsake_debug_log(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}
#endif
