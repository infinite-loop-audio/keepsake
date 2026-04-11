#pragma once

#include <clap/clap.h>

// Initialize the factory: scan for VST2 plugins and build descriptors.
// Called from clap_entry.init().
bool keepsake_factory_init(const char *plugin_path);

// Tear down the factory: free all descriptors.
// Called from clap_entry.deinit().
void keepsake_factory_deinit(void);

// Return the CLAP plugin factory.
// Called from clap_entry.get_factory().
const clap_plugin_factory_t *keepsake_get_plugin_factory(void);

// Look up the VST2 path for a given plugin ID. Returns nullptr if not found.
const char *keepsake_lookup_vst2_path(const char *plugin_id);

// Look up channel counts for a given plugin ID.
bool keepsake_lookup_plugin_info(const char *plugin_id,
                                  int32_t *num_inputs,
                                  int32_t *num_outputs);

// Get the bridge binary path. Set during init.
const char *keepsake_get_bridge_path(void);
