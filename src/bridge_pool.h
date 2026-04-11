#pragma once
//
// BridgePool — manages bridge subprocess processes and routes plugin
// instances to the appropriate process based on isolation policy.
//
// Contract ref: docs/contracts/006-process-isolation-policy.md
//

#include "ipc.h"
#include "platform.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>

enum class IsolationMode {
    SHARED,       // All plugins in one bridge (per arch+format)
    PER_BINARY,   // One bridge per unique plugin binary
    PER_INSTANCE, // One bridge per plugin instance
};

struct BridgeProcess {
    PlatformProcess proc;
    bool alive = false;
    int ref_count = 0; // number of instances using this bridge
    std::string key;   // isolation group key
};

class BridgePool {
public:
    // Get or create a bridge process for a plugin. Returns a pointer to the
    // managed BridgeProcess. The caller must not free it.
    BridgeProcess *acquire(const std::string &bridge_binary,
                            const std::string &plugin_path,
                            uint32_t format,
                            IsolationMode mode);

    // Release a bridge process. If ref_count hits zero and mode is not
    // SHARED, the process is killed.
    void release(BridgeProcess *bp);

    // Shut down all bridge processes.
    void shutdown_all();

    // Resolve isolation mode for a plugin (checks config overrides).
    IsolationMode resolve_mode(const std::string &plugin_id,
                                const std::string &plugin_name) const;

    // Set the global default mode.
    void set_default_mode(IsolationMode mode) { default_mode = mode; }

    struct Override {
        std::string match; // plugin ID or glob
        IsolationMode mode;
    };

    void add_override(const std::string &match, IsolationMode mode) {
        overrides.push_back({match, mode});
    }

private:
    std::unordered_map<std::string, BridgeProcess *> pool;
    std::vector<Override> overrides;
    IsolationMode default_mode = IsolationMode::SHARED;
    uint32_t instance_counter = 0;

    std::string make_key(const std::string &bridge_binary,
                          const std::string &plugin_path,
                          uint32_t format,
                          IsolationMode mode);

    bool glob_match(const std::string &pattern,
                     const std::string &text) const;
};
