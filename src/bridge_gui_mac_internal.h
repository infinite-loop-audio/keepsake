#pragma once

#import <Cocoa/Cocoa.h>

#include "bridge_gui.h"

static const CGFloat HEADER_HEIGHT = 28.0;
static const int DEFAULT_EDITOR_WIDTH = 960;
static const int DEFAULT_EDITOR_HEIGHT = 640;
static const int EDITOR_OPEN_TIMEOUT_MS = 5000;

extern NSWindow *g_window;
extern NSWindow *g_parentless_plugin_window;
extern NSView *g_header;
extern NSView *g_editor_container;
extern id g_frame_change_observer;
extern BridgeLoader *g_active_loader;
extern bool g_editor_open;
extern bool g_iosurface_mode;
extern int g_current_width;
extern int g_current_height;

static inline void gui_pump_pending_events(NSDate *until_date) {
    @autoreleasepool {
        NSEvent *event;
        while ((event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                           untilDate:until_date
                                              inMode:NSDefaultRunLoopMode
                                             dequeue:YES])) {
            [NSApp sendEvent:event];
        }
    }
}

bool gui_open_windowed_editor(BridgeLoader *loader, const EditorHeaderInfo &header);
void gui_close_window_state();
void gui_capture_iosurface_if_needed();
void gui_close_iosurface_state();
