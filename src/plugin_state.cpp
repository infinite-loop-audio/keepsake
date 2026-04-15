//
// Keepsake CLAP plugin — state extension.
//

#include "plugin_internal.h"

bool keepsake_state_save(const clap_plugin_t *plugin,
                          const clap_ostream_t *stream) {
    auto *kp = get(plugin);
    if (kp->crashed || !kp->bridge_ok) return false;
    if (kp->processing || keepsake_gui_session_is_open_or_pending(kp)) {
        return false;
    }

    std::vector<uint8_t> chunk_data;
    if (!send_and_wait(kp, IPC_OP_GET_CHUNK, nullptr, 0, &chunk_data))
        return false;

    if (chunk_data.empty()) return true;

    uint32_t size = static_cast<uint32_t>(chunk_data.size());
    if (stream->write(stream, &size, sizeof(size)) != sizeof(size))
        return false;
    if (stream->write(stream, chunk_data.data(), size) !=
        static_cast<int64_t>(size))
        return false;

    return true;
}

bool keepsake_state_load(const clap_plugin_t *plugin,
                          const clap_istream_t *stream) {
    auto *kp = get(plugin);
    if (kp->crashed || !kp->bridge_ok) return false;
    if (kp->processing || keepsake_gui_session_is_open_or_pending(kp)) {
        return false;
    }

    uint32_t size = 0;
    if (stream->read(stream, &size, sizeof(size)) != sizeof(size))
        return false;
    if (size == 0) return true;
    if (size > 64 * 1024 * 1024) return false;

    std::vector<uint8_t> chunk(size);
    if (stream->read(stream, chunk.data(), size) != static_cast<int64_t>(size))
        return false;

    return send_and_wait(kp, IPC_OP_SET_CHUNK, chunk.data(), size);
}

extern const clap_plugin_state_t keepsake_state_ext;
const clap_plugin_state_t keepsake_state_ext = {
    .save = keepsake_state_save,
    .load = keepsake_state_load,
};
