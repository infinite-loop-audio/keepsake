//
// Bridge loader factory — creates the appropriate loader for a plugin format.
//

#include "bridge_loader.h"

// Forward declarations — each loader registers via its own .cpp/.mm
extern BridgeLoader *create_au_loader();

// VST2 loader is compiled in bridge_loader_vst2.cpp
class Vst2Loader;

BridgeLoader *create_loader(PluginFormat format) {
    switch (format) {
    case FORMAT_VST2: {
        // Defined in bridge_loader_vst2.cpp — return via extern creation
        extern BridgeLoader *create_vst2_loader();
        return create_vst2_loader();
    }
    case FORMAT_VST3: {
        extern BridgeLoader *create_vst3_loader();
        return create_vst3_loader();
    }
    case FORMAT_AU:
        return create_au_loader();
    default:
        return nullptr;
    }
}
