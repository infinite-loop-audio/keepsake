#include "config.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace {

void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

void set_config_root(const fs::path &root) {
#ifdef _WIN32
    _putenv_s("APPDATA", root.string().c_str());
#elif defined(__APPLE__)
    setenv("HOME", root.string().c_str(), 1);
#else
    setenv("XDG_CONFIG_HOME", root.string().c_str(), 1);
#endif
}

} // namespace

int main() {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path root =
        fs::temp_directory_path() / ("keepsake-config-cache-test-" + std::to_string(unique));
    const fs::path plugin_path = root / "Fixture.vst";

    try {
        fs::create_directories(root);
        std::ofstream(plugin_path) << "fixture";
        set_config_root(root);

        Vst2PluginInfo plugin{};
        plugin.unique_id = 0x4B535454;
        plugin.vendor_version = 1;
        plugin.category = 1;
        plugin.flags = 0;
        plugin.num_inputs = 2;
        plugin.num_outputs = 2;
        plugin.num_params = 1;
        plugin.name = "Cache Fixture";
        plugin.vendor = "Keepsake";
        plugin.file_path = plugin_path.string();
        plugin.binary_arch = "native";

        cache_save({plugin});
        bool invalidated = true;
        require(cache_load(&invalidated).size() == 1, "fresh cache entry was not loaded");
        require(!invalidated, "fresh cache incorrectly requested a complete rescan");

        const auto original_time = fs::last_write_time(plugin_path);
        fs::last_write_time(plugin_path, original_time + std::chrono::seconds(10));
        invalidated = false;
        require(cache_load(&invalidated).empty(), "modified plugin did not invalidate the cache");
        require(invalidated, "modified plugin did not request a complete rescan");

        cache_save({plugin});
        fs::remove(plugin_path);
        invalidated = false;
        require(cache_load(&invalidated).empty(), "missing plugin did not invalidate the cache");
        require(invalidated, "missing plugin did not request a complete rescan");

        fs::remove_all(root);
        return 0;
    } catch (...) {
        fs::remove_all(root);
        throw;
    }
}
