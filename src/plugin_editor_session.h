#pragma once

#include <cstdint>

struct KeepsakePlugin;

enum class KeepsakeGuiPendingState {
    NotPending,
    Opening,
    Open,
    Failed,
};

bool keepsake_gui_session_is_open_or_pending(const KeepsakePlugin *kp);
bool keepsake_gui_session_is_pending(const KeepsakePlugin *kp);
KeepsakeGuiPendingState keepsake_gui_session_get_pending_state(const KeepsakePlugin *kp);
void keepsake_gui_session_clear_callback_request(KeepsakePlugin *kp);
void keepsake_gui_session_request_callback_once(KeepsakePlugin *kp);
bool keepsake_gui_session_can_host_resize(const KeepsakePlugin *kp);
bool keepsake_gui_session_should_rate_limit_resize(const KeepsakePlugin *kp,
                                                   int32_t host_width,
                                                   int32_t host_height,
                                                   uint64_t now_ms,
                                                   uint64_t min_interval_ms);
bool keepsake_gui_session_should_suppress_poll(const KeepsakePlugin *kp,
                                               uint64_t now_ms,
                                               uint64_t suppress_ms);
void keepsake_gui_session_record_host_resize_request(KeepsakePlugin *kp,
                                                     int32_t raw_width,
                                                     int32_t raw_height,
                                                     int32_t host_width,
                                                     int32_t host_height,
                                                     uint32_t resize_serial,
                                                     uint64_t now_ms,
                                                     bool direct_resize);
void keepsake_gui_session_reset_resize_tracking(KeepsakePlugin *kp);
void keepsake_gui_session_mark_staged(KeepsakePlugin *kp);
void keepsake_gui_session_mark_closed(KeepsakePlugin *kp);
void keepsake_gui_session_mark_pending(KeepsakePlugin *kp);
void keepsake_gui_session_mark_open(KeepsakePlugin *kp);
