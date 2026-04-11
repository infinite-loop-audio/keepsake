// host-test: simulates a CLAP host loading a Keepsake plugin.
// Times each lifecycle call to detect blocking.
//
// Usage: host-test <clap-bundle-path> <plugin-id>

#include <clap/clap.h>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <sys/time.h>
#include <unistd.h>

static double now_ms() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

static clap_host_t g_host = {};

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <clap-bundle> <plugin-id>\n", argv[0]);
        return 1;
    }

    setvbuf(stdout, nullptr, _IONBF, 0); // unbuffered stdout

    const char *clap_path = argv[1];
    const char *plugin_id = argv[2];

    // Load the CLAP bundle
#ifdef __APPLE__
    char lib_path[4096];
    snprintf(lib_path, sizeof(lib_path), "%s/Contents/MacOS/keepsake", clap_path);
#else
    const char *lib_path = clap_path;
#endif

    void *lib = dlopen(lib_path, RTLD_LAZY | RTLD_LOCAL);
    if (!lib) {
        fprintf(stderr, "failed to load: %s\n", dlerror());
        return 1;
    }

    auto *entry = reinterpret_cast<const clap_plugin_entry_t *>(
        dlsym(lib, "clap_entry"));
    if (!entry) { fprintf(stderr, "no clap_entry\n"); return 1; }

    printf("=== Host Test ===\n\n");

    // Init
    double t = now_ms();
    bool ok = entry->init(clap_path);
    printf("[init]             %6.1f ms — %s\n", now_ms() - t, ok ? "OK" : "FAIL");
    if (!ok) return 1;

    // Get factory
    auto *factory = reinterpret_cast<const clap_plugin_factory_t *>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    if (!factory) { fprintf(stderr, "no factory\n"); return 1; }

    printf("[factory]          %u plugins\n", factory->get_plugin_count(factory));

    // Find plugin
    const clap_plugin_descriptor_t *desc = nullptr;
    for (uint32_t i = 0; i < factory->get_plugin_count(factory); i++) {
        auto *d = factory->get_plugin_descriptor(factory, i);
        if (d && strcmp(d->id, plugin_id) == 0) { desc = d; break; }
    }
    if (!desc) {
        fprintf(stderr, "plugin '%s' not found\n", plugin_id);
        // List available
        for (uint32_t i = 0; i < factory->get_plugin_count(factory); i++) {
            auto *d = factory->get_plugin_descriptor(factory, i);
            if (d) printf("  available: %s — %s\n", d->id, d->name);
        }
        entry->deinit();
        return 1;
    }
    printf("[found]            %s — %s\n", desc->id, desc->name);

    // Setup minimal host
    g_host.clap_version = CLAP_VERSION;
    g_host.name = "keepsake-host-test";
    g_host.vendor = "test";
    g_host.url = "";
    g_host.version = "1.0";
    g_host.get_extension = [](const clap_host_t *, const char *) -> const void * {
        return nullptr;
    };
    g_host.request_restart = [](const clap_host_t *) {};
    g_host.request_process = [](const clap_host_t *) {};
    g_host.request_callback = [](const clap_host_t *) {};

    // Create plugin
    t = now_ms();
    auto *plugin = factory->create_plugin(factory, &g_host, plugin_id);
    printf("[create_plugin]    %6.1f ms — %s\n", now_ms() - t,
           plugin ? "OK" : "FAIL");
    if (!plugin) return 1;

    // Init plugin
    t = now_ms();
    ok = plugin->init(plugin);
    printf("[plugin.init]      %6.1f ms — %s\n", now_ms() - t,
           ok ? "OK" : "FAIL");

    // Activate
    t = now_ms();
    ok = plugin->activate(plugin, 44100.0, 32, 512);
    printf("[plugin.activate]  %6.1f ms — %s\n", now_ms() - t,
           ok ? "OK" : "FAIL");

    // Start processing
    t = now_ms();
    ok = plugin->start_processing(plugin);
    printf("[start_processing] %6.1f ms — %s\n", now_ms() - t,
           ok ? "OK" : "FAIL");

    // Wait for bridge to be ready, then process a few buffers
    printf("\n[processing] waiting for bridge...\n");
    float out_l[512] = {}, out_r[512] = {};
    float *out_ptrs[2] = { out_l, out_r };

    clap_audio_buffer_t out_buf = {};
    out_buf.data32 = out_ptrs;
    out_buf.channel_count = 2;

    clap_process_t proc = {};
    proc.frames_count = 512;
    proc.audio_outputs = &out_buf;
    proc.audio_outputs_count = 1;

    for (int i = 0; i < 200; i++) { // ~10 seconds at 512/44100
        memset(out_l, 0, sizeof(out_l));
        memset(out_r, 0, sizeof(out_r));

        t = now_ms();
        auto status = plugin->process(plugin, &proc);
        double elapsed = now_ms() - t;

        // Check for audio
        float peak = 0;
        for (int s = 0; s < 512; s++) {
            float v = out_l[s] > 0 ? out_l[s] : -out_l[s];
            if (v > peak) peak = v;
        }

        if (i < 5 || (i % 50) == 0 || peak > 0.001f) {
            printf("  [%3d] %5.1f ms  status=%d  peak=%.6f%s\n",
                   i, elapsed, status, peak,
                   peak > 0.001f ? " AUDIO!" : "");
        }

        if (peak > 0.001f) {
            printf("\n[SUCCESS] Audio received after %d buffers\n", i);
            break;
        }

        usleep(11600); // ~512/44100 seconds
    }

    // Cleanup
    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    entry->deinit();

    printf("\n=== Done ===\n");
    return 0;
}
