#pragma once
//
// Bridge loader — abstract interface for loading plugins in the bridge
// subprocess. Each format (VST2, VST3, AU) implements this interface.
//

#include "ipc.h"
#include <cstdint>
#include <string>
#include <vector>

struct BridgeLoader {
    virtual ~BridgeLoader() = default;

    // Load the plugin from path. Returns true on success.
    virtual bool load(const std::string &path) = 0;

    // Fill in the plugin info response (uniqueID, channels, params, flags,
    // category, vendor_version) and append name\0vendor\0product\0 to
    // extra_strings. Called after load().
    virtual void get_info(IpcPluginInfo &info,
                           std::vector<uint8_t> &extra_strings) = 0;

    // Activate: set sample rate and block size, resume processing.
    virtual void activate(double sample_rate, uint32_t max_frames) = 0;

    // Deactivate: suspend processing.
    virtual void deactivate() = 0;

    // Process audio: read from inputs, write to outputs.
    // Pointers are into shared memory (non-interleaved float channels).
    virtual void process(float **inputs, int num_inputs,
                          float **outputs, int num_outputs,
                          uint32_t num_frames) = 0;

    // Set a parameter value.
    virtual void set_param(uint32_t index, float value) = 0;

    // Get parameter info.
    virtual bool get_param_info(uint32_t index, IpcParamInfoResponse &resp) = 0;

    // Send MIDI events (raw 4-byte MIDI data with delta frame timing).
    virtual void send_midi(int32_t delta_frames, const uint8_t data[4]) = 0;

    // Get state chunk. Returns empty vector if unsupported.
    virtual std::vector<uint8_t> get_chunk() = 0;

    // Set state chunk.
    virtual void set_chunk(const uint8_t *data, size_t size) = 0;

    // Editor support
    virtual bool has_editor() = 0;
    virtual bool get_editor_rect(int &width, int &height) = 0;

    // Open the editor into a parent view (platform-specific handle).
    // On macOS: parent is NSView*. Windows: HWND. Linux: X11 Window.
    virtual bool open_editor(void *parent_view) = 0;

    // Close the editor.
    virtual void close_editor() = 0;

    // Editor idle tick.
    virtual void editor_idle() = 0;

    // Close/unload the plugin.
    virtual void close() = 0;
};

// Create a loader for the given format.
BridgeLoader *create_loader(PluginFormat format);
