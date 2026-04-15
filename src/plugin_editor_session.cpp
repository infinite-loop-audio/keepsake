#include "plugin_editor_session.h"

#include "plugin.h"
#include "ipc.h"

bool keepsake_gui_session_is_open_or_pending(const KeepsakePlugin *kp) {
    return kp && (kp->editor_open || kp->editor_open_pending);
}

bool keepsake_gui_session_is_pending(const KeepsakePlugin *kp) {
    return kp && kp->editor_open_pending;
}

KeepsakeGuiPendingState keepsake_gui_session_get_pending_state(const KeepsakePlugin *kp) {
    if (!kp || !kp->editor_open_pending) {
        return KeepsakeGuiPendingState::NotPending;
    }
    if (!kp->shm.ptr) {
        return KeepsakeGuiPendingState::Failed;
    }

    const uint32_t editor_state =
        shm_load_acquire(&shm_control(kp->shm.ptr)->editor_state);
    switch (editor_state) {
    case SHM_EDITOR_OPENING:
        return KeepsakeGuiPendingState::Opening;
    case SHM_EDITOR_OPEN:
        return KeepsakeGuiPendingState::Open;
    default:
        return KeepsakeGuiPendingState::Failed;
    }
}

void keepsake_gui_session_clear_callback_request(KeepsakePlugin *kp) {
    if (!kp) return;
    kp->gui_callback_requested.store(false, std::memory_order_release);
}

void keepsake_gui_session_request_callback_once(KeepsakePlugin *kp) {
    if (!kp || !kp->host || !kp->host->request_callback) return;
    bool expected = false;
    if (kp->gui_callback_requested.compare_exchange_strong(expected, true,
                                                           std::memory_order_acq_rel)) {
        kp->host->request_callback(kp->host);
    }
}

bool keepsake_gui_session_can_host_resize(const KeepsakePlugin *kp) {
    return kp && kp->editor_open && !kp->editor_open_pending && !kp->gui_is_floating &&
           kp->host_gui && kp->host_gui->request_resize;
}

bool keepsake_gui_session_should_rate_limit_resize(const KeepsakePlugin *kp,
                                                   int32_t host_width,
                                                   int32_t host_height,
                                                   uint64_t now_ms,
                                                   uint64_t min_interval_ms) {
    if (!kp) return true;
    if (host_width == kp->last_requested_gui_width &&
        host_height == kp->last_requested_gui_height) {
        return true;
    }
    return (now_ms - kp->last_host_resize_request_ms) < min_interval_ms;
}

bool keepsake_gui_session_should_suppress_poll(const KeepsakePlugin *kp,
                                               uint64_t now_ms,
                                               uint64_t suppress_ms) {
    if (!kp) return false;
    return kp->saw_direct_editor_resize &&
           (now_ms - kp->last_direct_resize_request_ms) < suppress_ms;
}

void keepsake_gui_session_record_host_resize_request(KeepsakePlugin *kp,
                                                     int32_t raw_width,
                                                     int32_t raw_height,
                                                     int32_t host_width,
                                                     int32_t host_height,
                                                     uint32_t resize_serial,
                                                     uint64_t now_ms,
                                                     bool direct_resize) {
    if (!kp) return;
    kp->editor_width = raw_width;
    kp->editor_height = raw_height;
    kp->last_requested_gui_width = host_width;
    kp->last_requested_gui_height = host_height;
    kp->last_host_resize_request_ms = now_ms;
    if (resize_serial != 0) {
        kp->last_seen_editor_resize_serial = resize_serial;
    }
    if (direct_resize) {
        kp->saw_direct_editor_resize = true;
        kp->last_direct_resize_request_ms = now_ms;
    }
}

void keepsake_gui_session_reset_resize_tracking(KeepsakePlugin *kp) {
    if (!kp) return;
    kp->last_requested_gui_width = 0;
    kp->last_requested_gui_height = 0;
    kp->last_seen_editor_resize_serial = 0;
    kp->saw_direct_editor_resize = false;
    kp->last_direct_resize_request_ms = 0;
    kp->last_gui_poll_ms = 0;
    kp->last_host_resize_request_ms = 0;
}

static void keepsake_gui_session_store_editor_state(KeepsakePlugin *kp, uint32_t state) {
    if (!kp || !kp->shm.ptr) return;
    shm_store_release(&shm_control(kp->shm.ptr)->editor_state, state);
}

void keepsake_gui_session_mark_staged(KeepsakePlugin *kp) {
    if (!kp) return;
    kp->editor_open = false;
    kp->editor_open_pending = false;
    keepsake_gui_session_reset_resize_tracking(kp);
    keepsake_gui_session_store_editor_state(kp, SHM_EDITOR_CLOSED);
}

void keepsake_gui_session_mark_closed(KeepsakePlugin *kp) {
    keepsake_gui_session_mark_staged(kp);
}

void keepsake_gui_session_mark_pending(KeepsakePlugin *kp) {
    if (!kp) return;
    kp->editor_open = false;
    kp->editor_open_pending = true;
    keepsake_gui_session_reset_resize_tracking(kp);
    keepsake_gui_session_store_editor_state(kp, SHM_EDITOR_OPENING);
}

void keepsake_gui_session_mark_open(KeepsakePlugin *kp) {
    if (!kp) return;
    kp->editor_open = true;
    kp->editor_open_pending = false;
    if (kp->shm.ptr) {
        kp->last_seen_editor_resize_serial =
            shm_load_acquire(&shm_control(kp->shm.ptr)->editor_resize_serial);
    } else {
        kp->last_seen_editor_resize_serial = 0;
    }
    kp->saw_direct_editor_resize = false;
    keepsake_gui_session_store_editor_state(kp, SHM_EDITOR_OPEN);
}
