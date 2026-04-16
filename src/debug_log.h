#pragma once

#include <cstdarg>
#include <cstdio>

#ifndef KEEPSAKE_BUILD_ID
#define KEEPSAKE_BUILD_ID "unknown"
#endif

#ifdef _WIN32
#include <windows.h>

static inline void keepsake_debug_vlog(const char *fmt, va_list args) {
    va_list stderr_args;
    va_copy(stderr_args, args);
    vfprintf(stderr, fmt, stderr_args);
    fflush(stderr);
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

static inline void keepsake_debug_log_build_once(const char *prefix) {
    static bool logged = false;
    if (logged) return;
    logged = true;
    keepsake_debug_log("%s build=%s\n", prefix, KEEPSAKE_BUILD_ID);
}
#else
static inline void keepsake_debug_vlog(const char *fmt, va_list args) {
    va_list stderr_args;
    va_copy(stderr_args, args);
    vfprintf(stderr, fmt, stderr_args);
    fflush(stderr);
    va_end(stderr_args);

    const char *tmpdir = getenv("TMPDIR");
    if (!tmpdir || tmpdir[0] == '\0') tmpdir = "/tmp";

    char log_path[4096];
    snprintf(log_path, sizeof(log_path), "%s%skeepsake-debug.log",
             tmpdir,
             tmpdir[strlen(tmpdir) - 1] == '/' ? "" : "/");

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

static inline void keepsake_debug_log_build_once(const char *prefix) {
    static bool logged = false;
    if (logged) return;
    logged = true;
    keepsake_debug_log("%s build=%s\n", prefix, KEEPSAKE_BUILD_ID);
}
#endif
