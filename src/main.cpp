//
// Keepsake — CLAP plugin entry point
//
// This file defines the clap_entry symbol that CLAP hosts look for
// when scanning .clap binaries. It delegates to the factory for
// plugin enumeration and instantiation.
//

#include "factory.h"
#include <clap/clap.h>
#include <cstring>

static bool s_initialized = false;

static bool keepsake_init(const char *plugin_path) {
    if (s_initialized) return true;
    s_initialized = keepsake_factory_init(plugin_path);
    return s_initialized;
}

static void keepsake_deinit(void) {
    if (!s_initialized) return;
    keepsake_factory_deinit();
    s_initialized = false;
}

static const void *keepsake_get_factory(const char *factory_id) {
    if (strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID) == 0) {
        return keepsake_get_plugin_factory();
    }
    return nullptr;
}

extern "C" {

CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    .clap_version = CLAP_VERSION,
    .init = keepsake_init,
    .deinit = keepsake_deinit,
    .get_factory = keepsake_get_factory,
};

} // extern "C"
