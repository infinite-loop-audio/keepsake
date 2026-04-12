// host-test-threaded: simulates a multithreaded CLAP host.
// Fails if ANY callback blocks the main thread for more than 200ms.
//
// Usage: host-test-threaded <clap-bundle> <plugin-id> [--timeout-ms N]
//
// This reproduces the REAPER lockup: the main thread must stay responsive
// while the plugin loads (potentially slow under Rosetta).

#include <clap/clap.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <sys/time.h>
#include <unistd.h>
#include <atomic>

#include "host_test_support.h"

namespace fs = std::filesystem;

static double now_ms() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

static int g_max_block_ms = 200;
static int g_max_entry_block_ms = 5000;
static int g_max_memory_mb = 2048;
static std::atomic<bool> g_failed{false};

// Call a function and fail if it takes too long
#define TIMED_CALL_LIMIT(name, limit_ms, expr) do { \
    double _t = now_ms(); \
    expr; \
    double _elapsed = now_ms() - _t; \
    printf("  %-25s %6.1f ms %s\n", name, _elapsed, \
           _elapsed > (limit_ms) ? "*** BLOCKED ***" : "OK"); \
    if (_elapsed > (limit_ms)) { \
        fprintf(stderr, "FAIL: %s blocked for %.0fms (max %dms)\n", \
                name, _elapsed, (limit_ms)); \
        g_failed = true; \
    } \
} while(0)

#define TIMED_CALL(name, expr) TIMED_CALL_LIMIT(name, g_max_block_ms, expr)

static clap_host_t g_host = {};

static bool configure_scan_override(const char *plugin_path) {
    if (!plugin_path || !plugin_path[0]) return true;

    char tmpl[] = "/tmp/keepsake-host-test-XXXXXX";
    char *dir = mkdtemp(tmpl);
    if (!dir) return false;

    fs::path src(plugin_path);
    fs::path dst = fs::path(dir) / src.filename();
    std::error_code ec;
    fs::create_directory_symlink(src, dst, ec);
    if (ec) return false;

    return setenv("KEEPSAKE_VST2_PATH", dir, 1) == 0;
}

static int run_worker(int argc, char *argv[]) {
    setvbuf(stdout, nullptr, _IONBF, 0);

    if (argc < 3) {
        fprintf(stderr, "usage: %s <clap-bundle> <plugin-id> [--timeout-ms N] [--entry-timeout-ms N] [--max-memory-mb N]\n",
                argv[0]);
        return 1;
    }

    const char *clap_path = argv[1];
    const char *plugin_id = argv[2];
    const char *vst_path = nullptr;

    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--timeout-ms") == 0 && i + 1 < argc)
            g_max_block_ms = atoi(argv[++i]);
        else if (strcmp(argv[i], "--entry-timeout-ms") == 0 && i + 1 < argc)
            g_max_entry_block_ms = atoi(argv[++i]);
        else if (strcmp(argv[i], "--max-memory-mb") == 0 && i + 1 < argc)
            g_max_memory_mb = atoi(argv[++i]);
        else if (strcmp(argv[i], "--vst-path") == 0 && i + 1 < argc)
            vst_path = argv[++i];
    }

    if (!configure_scan_override(vst_path)) {
        fprintf(stderr, "warning: failed to configure scan override for '%s'\n",
                vst_path ? vst_path : "");
    }

    // Load CLAP
#ifdef __APPLE__
    char lib_path[4096];
    snprintf(lib_path, sizeof(lib_path), "%s/Contents/MacOS/keepsake", clap_path);
#else
    const char *lib_path = clap_path;
#endif

    void *lib = dlopen(lib_path, RTLD_LAZY | RTLD_LOCAL);
    if (!lib) { fprintf(stderr, "failed to load: %s\n", dlerror()); return 1; }

    auto *entry = reinterpret_cast<const clap_plugin_entry_t *>(
        dlsym(lib, "clap_entry"));
    if (!entry) { fprintf(stderr, "no clap_entry\n"); return 1; }

    printf("=== Threaded Host Test (max block: %dms) ===\n\n", g_max_block_ms);

    // Init
    bool init_ok = false;
    TIMED_CALL_LIMIT("entry.init", g_max_entry_block_ms,
                     init_ok = entry->init(clap_path));
    if (!init_ok) { fprintf(stderr, "init failed\n"); return 1; }

    auto *factory = reinterpret_cast<const clap_plugin_factory_t *>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    if (!factory) { fprintf(stderr, "no factory\n"); return 1; }

    // Find plugin
    const clap_plugin_descriptor_t *desc = nullptr;
    for (uint32_t i = 0; i < factory->get_plugin_count(factory); i++) {
        auto *d = factory->get_plugin_descriptor(factory, i);
        if (d && strcmp(d->id, plugin_id) == 0) { desc = d; break; }
    }
    if (!desc) {
        fprintf(stderr, "plugin '%s' not found. Available:\n", plugin_id);
        for (uint32_t i = 0; i < factory->get_plugin_count(factory); i++) {
            auto *d = factory->get_plugin_descriptor(factory, i);
            if (d) printf("  %s — %s\n", d->id, d->name);
        }
        entry->deinit(); return 1;
    }
    printf("  Plugin: %s — %s\n\n", desc->id, desc->name);

    // Host
    g_host.clap_version = CLAP_VERSION;
    g_host.name = "keepsake-threaded-test";
    g_host.vendor = "test"; g_host.url = ""; g_host.version = "1.0";
    g_host.get_extension = [](const clap_host_t *, const char *) -> const void * { return nullptr; };
    g_host.request_restart = [](const clap_host_t *) {};
    g_host.request_process = [](const clap_host_t *) {};
    g_host.request_callback = [](const clap_host_t *) {};

    // --- Main thread lifecycle (must not block) ---
    printf("[Main thread — lifecycle calls]\n");

    const clap_plugin_t *plugin = nullptr;
    TIMED_CALL("create_plugin", plugin = factory->create_plugin(factory, &g_host, plugin_id));
    if (!plugin) { fprintf(stderr, "create_plugin failed\n"); goto done; }

    { bool ok;
    TIMED_CALL("plugin.init", ok = plugin->init(plugin));
    if (!ok) { fprintf(stderr, "init returned false (plugin failed to load)\n"); goto cleanup; }

    // Query extensions (REAPER does this immediately)
    printf("\n[Main thread — extension queries]\n");
    const char *exts[] = {
        "clap.params", "clap.gui", "clap.audio-ports", "clap.state",
        "clap.latency", "clap.note-ports", nullptr
    };
    for (int i = 0; exts[i]; i++) {
        const void *ext = nullptr;
        TIMED_CALL(exts[i], ext = plugin->get_extension(plugin, exts[i]));
    }

    // Activate
    printf("\n[Main thread — activation]\n");
    TIMED_CALL("activate", ok = plugin->activate(plugin, 44100, 32, 512));
    if (!ok) { fprintf(stderr, "activate returned false\n"); goto cleanup; }
    TIMED_CALL("start_processing", ok = plugin->start_processing(plugin));
    if (ok) {
        // Process on "audio thread"
        printf("\n[Audio thread — processing]\n");
        float out_l[512] = {}, out_r[512] = {};
        float *out_ptrs[2] = { out_l, out_r };
        clap_audio_buffer_t out_buf = {};
        out_buf.data32 = out_ptrs; out_buf.channel_count = 2;
        clap_process_t proc = {};
        proc.frames_count = 512;
        proc.audio_outputs = &out_buf; proc.audio_outputs_count = 1;

        for (int i = 0; i < 100; i++) {
            memset(out_l, 0, sizeof(out_l)); memset(out_r, 0, sizeof(out_r));
            auto status = plugin->process(plugin, &proc);
            float peak = 0;
            for (int s = 0; s < 512; s++) {
                float v = out_l[s] > 0 ? out_l[s] : -out_l[s];
                if (v > peak) peak = v;
            }
            if (i < 3 || peak > 0.001f) {
                printf("  [%3d] status=%d peak=%.6f%s\n", i, status, peak,
                       peak > 0.001f ? " AUDIO!" : "");
            }
            if (peak > 0.001f) break;
            usleep(11600);
        }

        printf("\n[Main thread — cleanup]\n");
        TIMED_CALL("stop_processing", plugin->stop_processing(plugin));
    } else {
        fprintf(stderr, "start_processing returned false\n");
    }

    TIMED_CALL("deactivate", plugin->deactivate(plugin));
    }

cleanup:
    TIMED_CALL("destroy", plugin->destroy(plugin));

done:
    TIMED_CALL("deinit", entry->deinit());

    printf("\n=== %s ===\n", g_failed ? "FAILED — main thread blocked" : "PASSED");
    return g_failed ? 1 : 0;
}

int main(int argc, char *argv[]) {
    setvbuf(stdout, nullptr, _IONBF, 0);

    if (argc < 3) {
        fprintf(stderr, "usage: %s <clap-bundle> <plugin-id> [--timeout-ms N] [--entry-timeout-ms N] [--max-memory-mb N]\n",
                argv[0]);
        return 1;
    }

    g_max_block_ms = 200;
    g_max_entry_block_ms = 5000;
    g_max_memory_mb = 2048;
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--timeout-ms") == 0 && i + 1 < argc)
            g_max_block_ms = atoi(argv[++i]);
        else if (strcmp(argv[i], "--entry-timeout-ms") == 0 && i + 1 < argc)
            g_max_entry_block_ms = atoi(argv[++i]);
        else if (strcmp(argv[i], "--max-memory-mb") == 0 && i + 1 < argc)
            g_max_memory_mb = atoi(argv[++i]);
    }

    return host_test_support::run_supervised(argc, argv, g_max_memory_mb,
                                             run_worker);
}
