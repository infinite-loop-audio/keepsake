// clap-scan: minimal CLAP plugin scanner for testing.
// Loads a .clap binary, queries its factory, and prints all descriptors.
//
// Usage: clap-scan <path-to-clap-bundle>
//
// This is a development tool, not part of the plugin itself.

#include <cstdio>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#include <clap/clap.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <path-to-clap-bundle>\n", argv[0]);
        return 1;
    }

    const char *clap_path = argv[1];

    // Try loading: on macOS it's a bundle, elsewhere it's the binary itself.
    void *lib = nullptr;
#ifdef __APPLE__
    char lib_path[4096];
    snprintf(lib_path, sizeof(lib_path), "%s/Contents/MacOS/keepsake", clap_path);
    lib = dlopen(lib_path, RTLD_LAZY | RTLD_LOCAL);
#elif defined(_WIN32)
    lib = reinterpret_cast<void *>(LoadLibraryA(clap_path));
#else
    lib = dlopen(clap_path, RTLD_LAZY | RTLD_LOCAL);
#endif
    if (!lib) {
#ifdef _WIN32
        fprintf(stderr, "failed to load '%s' (GetLastError=%lu)\n",
                clap_path, static_cast<unsigned long>(GetLastError()));
#else
        fprintf(stderr, "failed to load '%s': %s\n", clap_path, dlerror());
#endif
        return 1;
    }

    auto *entry = reinterpret_cast<const clap_plugin_entry_t *>(
#ifdef _WIN32
        GetProcAddress(static_cast<HMODULE>(lib), "clap_entry"));
#else
        dlsym(lib, "clap_entry"));
#endif
    if (!entry) {
        fprintf(stderr, "no clap_entry symbol found\n");
#ifdef _WIN32
        FreeLibrary(static_cast<HMODULE>(lib));
#else
        dlclose(lib);
#endif
        return 1;
    }

    printf("CLAP version: %d.%d.%d\n",
           entry->clap_version.major,
           entry->clap_version.minor,
           entry->clap_version.revision);

    if (!entry->init(clap_path)) {
        fprintf(stderr, "clap_entry.init() failed\n");
#ifdef _WIN32
        FreeLibrary(static_cast<HMODULE>(lib));
#else
        dlclose(lib);
#endif
        return 1;
    }

    auto *factory = reinterpret_cast<const clap_plugin_factory_t *>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    if (!factory) {
        fprintf(stderr, "no plugin factory\n");
        entry->deinit();
#ifdef _WIN32
        FreeLibrary(static_cast<HMODULE>(lib));
#else
        dlclose(lib);
#endif
        return 1;
    }

    uint32_t count = factory->get_plugin_count(factory);
    printf("\nDiscovered %u plugin(s):\n\n", count);

    for (uint32_t i = 0; i < count; i++) {
        const clap_plugin_descriptor_t *desc =
            factory->get_plugin_descriptor(factory, i);
        if (!desc) continue;

        printf("  [%u] %s\n", i, desc->name);
        printf("       id:      %s\n", desc->id);
        printf("       vendor:  %s\n", desc->vendor ? desc->vendor : "(null)");
        printf("       version: %s\n", desc->version ? desc->version : "(null)");

        if (desc->features) {
            printf("       features:");
            for (const char *const *f = desc->features; *f; f++) {
                printf(" %s", *f);
            }
            printf("\n");
        }
        printf("\n");
    }

    entry->deinit();
#ifdef _WIN32
    FreeLibrary(static_cast<HMODULE>(lib));
#else
    dlclose(lib);
#endif
    return 0;
}
