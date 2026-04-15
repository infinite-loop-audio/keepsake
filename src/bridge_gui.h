#pragma once
//
// Bridge GUI — platform-specific editor hosting with header bar.
// The bridge opens the plugin editor in a floating window with a
// Keepsake toolbar showing plugin info and isolation controls.
//

#include "bridge_loader.h"
#include <string>

// Metadata displayed in the header bar
struct EditorHeaderInfo {
    std::string plugin_name;
    std::string format;        // "VST2", "VST3", "AU"
    std::string architecture;  // "native", "Rosetta", "32-bit"
    std::string isolation;     // "shared", "per-binary", "per-instance"
};

// Initialize the GUI subsystem. Call once at bridge startup.
void gui_init();

// Open the editor in a floating window with header bar.
bool gui_open_editor(BridgeLoader *loader, const EditorHeaderInfo &header);

// Open the editor embedded in a host-provided parent window.
// native_handle is platform-specific: HWND on Windows, X11 Window on Linux.
// On macOS, embedded mode is not supported — returns false (use floating).
bool gui_open_editor_embedded(BridgeLoader *loader, uint64_t native_handle);
bool gui_stage_editor_parent(BridgeLoader *loader, uint64_t native_handle);
bool gui_has_pending_work();
void gui_get_editor_status(bool &open, bool &pending);
void gui_set_status_shm(void *shm_ptr);
void gui_publish_resize_request(int width, int height);

// Set the transient/owner window for a floating editor.
void gui_set_editor_transient(uint64_t native_handle);

// Close the editor window.
void gui_close_editor(BridgeLoader *loader);

// Get the editor size.
bool gui_get_editor_rect(BridgeLoader *loader, int &width, int &height);

// Process pending GUI events and call editor idle.
void gui_idle(BridgeLoader *loader);

// Is the editor currently open?
bool gui_is_open();

// --- IOSurface embedded mode (macOS only) ---

// Open editor into an IOSurface-backed offscreen view.
// Returns the IOSurfaceID (or 0 on failure) which the host uses to
// composite the editor into its window.
uint32_t gui_open_editor_iosurface(BridgeLoader *loader, int width, int height);

// Forward a mouse event to the offscreen editor view.
struct IpcMouseEvent;
void gui_forward_mouse(const IpcMouseEvent &event);

// Forward a key event to the offscreen editor view.
struct IpcKeyEvent;
void gui_forward_key(const IpcKeyEvent &event);

// Is the editor in IOSurface mode?
bool gui_is_iosurface_mode();
