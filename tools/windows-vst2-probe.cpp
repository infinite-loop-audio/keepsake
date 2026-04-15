// windows-vst2-probe: tiny standalone Windows VST2 host probe.

#include <vestige/vestige.h>
#include <windows.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

namespace {

using VstEntry = AEffect *(__cdecl *)(audioMasterCallback);
constexpr const char *kWindowClass = "KeepsakeVst2ProbeWindow";

enum class ParentMode { None, Child, Top };
enum class ThreadMode { Current, Worker };

struct ProbeOptions {
    std::string plugin_path;
    bool open_editor = false;
    bool rect_twice = false;
    bool activate = false;
    bool get_chunk = false;
    bool set_chunk = false;
    bool chunk_during_process = false;
    bool open_during_process = false;
    bool editor_chunk_during_process = false;
    bool bridge_host_mode = false;
    bool bridge_gate_mode = false;
    bool bridge_marshal_mode = false;
    bool async_open_marshal_mode = false;
    ParentMode parent_mode = ParentMode::Child;
    ThreadMode load_thread_mode = ThreadMode::Current;
    ThreadMode rect_thread_mode = ThreadMode::Current;
    ThreadMode open_thread_mode = ThreadMode::Current;
    ThreadMode process_thread_mode = ThreadMode::Current;
    ThreadMode chunk_thread_mode = ThreadMode::Current;
    int idle_ms = 1500;
    int load_timeout_ms = 5000;
    int rect_timeout_ms = 5000;
    int open_timeout_ms = 2000;
    int process_timeout_ms = 5000;
    int chunk_timeout_ms = 5000;
    int process_blocks = 0;
    int process_sleep_ms = 0;
    int sample_rate = 44100;
    int block_size = 512;
    int callback_delay_ms = 0;
    int get_time_delay_ms = 0;
    int gate_delay_ms = 0;
};

struct HostConfig {
    bool bridge_host_mode = false;
    int callback_delay_ms = 0;
    int get_time_delay_ms = 0;
    bool editor_open_in_progress = false;
};

HostConfig g_host_config;
std::timed_mutex g_bridge_gate;

struct ProbeState {
    AEffect *effect = nullptr;
    HMODULE module = nullptr;
};

struct PluginInfo {
    std::string name;
    std::string vendor;
    std::string product;
    int inputs = 0;
    int outputs = 0;
    int params = 0;
    unsigned flags = 0;
    unsigned unique_id = 0;
};

enum class ServiceCommandType {
    None,
    QueryRect,
    OpenEditor,
    OpenEditorAsync,
    CloseEditor,
    GetChunk,
    Process,
    Stop,
};

struct ServiceCommand {
    uint64_t id = 0;
    ServiceCommandType type = ServiceCommandType::None;
    HWND parent = nullptr;
    int width = 0;
    int height = 0;
    bool bool_result = false;
    std::vector<uint8_t> chunk;
};

struct BridgeService {
    ProbeOptions opts;
    ProbeState state;
    PluginInfo info;
    std::thread worker;
    std::mutex mutex;
    std::condition_variable cv;
    std::condition_variable done_cv;
    ServiceCommand pending_command;
    bool pending_has_command = false;
    ServiceCommand completed_command;
    uint64_t completed_command_id = 0;
    bool started = false;
    bool start_ok = false;
    bool stop_requested = false;
    uint64_t next_command_id = 1;
    std::atomic<bool> async_open_inflight{false};
    std::atomic<bool> async_open_completed{false};
    std::atomic<bool> async_open_result{false};
    std::atomic<HWND> async_open_parent{nullptr};
};

double now_ms() {
    static LARGE_INTEGER freq = {};
    static bool init = false;
    if (!init) {
        QueryPerformanceFrequency(&freq);
        init = true;
    }
    LARGE_INTEGER counter = {};
    QueryPerformanceCounter(&counter);
    return (1000.0 * static_cast<double>(counter.QuadPart)) /
           static_cast<double>(freq.QuadPart);
}

const char *thread_mode_name(ThreadMode mode) {
    return mode == ThreadMode::Current ? "current" : "worker";
}

const char *opcode_name(int32_t opcode) {
    switch (opcode) {
    case audioMasterAutomate: return "audioMasterAutomate";
    case audioMasterVersion: return "audioMasterVersion";
    case audioMasterCurrentId: return "audioMasterCurrentId";
    case audioMasterIdle: return "audioMasterIdle";
    case audioMasterGetTime: return "audioMasterGetTime";
    case audioMasterGetSampleRate: return "audioMasterGetSampleRate";
    case audioMasterGetBlockSize: return "audioMasterGetBlockSize";
    case audioMasterCanDo: return "audioMasterCanDo";
    case audioMasterSizeWindow: return "audioMasterSizeWindow";
    case audioMasterUpdateDisplay: return "audioMasterUpdateDisplay";
    case audioMasterBeginEdit: return "audioMasterBeginEdit";
    case audioMasterEndEdit: return "audioMasterEndEdit";
    default: return "audioMaster?";
    }
}

template <typename Fn>
auto with_bridge_gate(const ProbeOptions &opts,
                      const char *label,
                      int timeout_ms,
                      Fn &&fn) -> decltype(fn()) {
    using Result = decltype(fn());
    auto maybe_delay = [&](int delay_ms) {
        if (delay_ms > 0) Sleep(static_cast<DWORD>(delay_ms));
    };

    if (!opts.bridge_gate_mode) {
        maybe_delay(opts.gate_delay_ms);
        return fn();
    }

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (!g_bridge_gate.try_lock_until(deadline)) {
        std::printf("[gate] %s waiting timeout=%d ms thread=%lu\n",
                    label, timeout_ms, static_cast<unsigned long>(GetCurrentThreadId()));
        std::fflush(stdout);
        ExitProcess(133);
    }

    maybe_delay(opts.gate_delay_ms);
    if constexpr (std::is_void_v<Result>) {
        fn();
        g_bridge_gate.unlock();
        return;
    } else {
        Result result = fn();
        g_bridge_gate.unlock();
        return result;
    }
}

intptr_t __cdecl host_callback(
    AEffect *effect, int32_t opcode, int32_t index, intptr_t value, void *ptr, float opt) {
    auto maybe_delay = [&](int delay_ms) {
        if (delay_ms > 0) Sleep(static_cast<DWORD>(delay_ms));
    };

    intptr_t result = 0;
    switch (opcode) {
    case audioMasterVersion: result = 2400; break;
    case audioMasterCurrentId: result = effect ? effect->uniqueID : 0; break;
    case audioMasterIdle: result = 1; break;
    case audioMasterGetSampleRate: result = 44100; break;
    case audioMasterGetBlockSize: result = 512; break;
    case audioMasterGetLanguage: result = kVstLangEnglish; break;
    case audioMasterGetVendorVersion: result = 1; break;
    case audioMasterGetCurrentProcessLevel:
        result = g_host_config.bridge_host_mode ? 1 : 0;
        break;
    case audioMasterGetAutomationState:
        result = g_host_config.bridge_host_mode
                     ? (g_host_config.editor_open_in_progress
                            ? (kVstAutomationReading | kVstAutomationWriting)
                            : kVstAutomationReading)
                     : 0;
        break;
    case audioMasterCanDo:
        if (ptr) {
            auto *query = static_cast<const char *>(ptr);
            if (strcmp(query, "sendVstEvents") == 0 ||
                strcmp(query, "sendVstMidiEvent") == 0 ||
                strcmp(query, "receiveVstEvents") == 0 ||
                strcmp(query, "receiveVstMidiEvent") == 0 ||
                strcmp(query, "sizeWindow") == 0 ||
                (g_host_config.bridge_host_mode && strcmp(query, "supportShell") == 0)) {
                result = 1;
            }
        }
        break;
    case audioMasterGetVendorString:
        if (ptr) {
            strncpy(static_cast<char *>(ptr), "Keepsake Probe", 64);
            static_cast<char *>(ptr)[63] = '\0';
            result = 1;
        }
        break;
    case audioMasterGetProductString:
        if (ptr) {
            strncpy(static_cast<char *>(ptr), "windows-vst2-probe", 64);
            static_cast<char *>(ptr)[63] = '\0';
            result = 1;
        }
        break;
    case audioMasterSizeWindow:
    case audioMasterUpdateDisplay:
    case audioMasterBeginEdit:
    case audioMasterEndEdit:
        result = 1;
        break;
    case audioMasterGetTime:
        result = 0;
        maybe_delay(g_host_config.get_time_delay_ms);
        break;
    default:
        break;
    }
    if (opcode != audioMasterGetTime) {
        maybe_delay(g_host_config.callback_delay_ms);
    }
    std::printf("hostcb opcode=%s idx=%d val=%lld ptr=%p opt=%.3f -> %lld\n",
                opcode_name(opcode),
                index,
                static_cast<long long>(value),
                ptr,
                opt,
                static_cast<long long>(result));
    return result;
}

LRESULT CALLBACK ProbeWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_DESTROY: return 0;
    default: return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
}

bool ensure_window_class() {
    static bool registered = false;
    if (registered) return true;
    WNDCLASSA wc = {};
    wc.lpfnWndProc = ProbeWndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = kWindowClass;
    if (!RegisterClassA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        std::fprintf(stderr, "failed to register window class (%lu)\n",
                     static_cast<unsigned long>(GetLastError()));
        return false;
    }
    registered = true;
    return true;
}

HWND create_parent_window(ParentMode mode, int width, int height) {
    if (mode == ParentMode::None) return nullptr;
    if (!ensure_window_class()) return nullptr;
    if (mode == ParentMode::Top) {
        return CreateWindowExA(0, kWindowClass, "Keepsake VST2 Probe",
                               WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
                               width, height, nullptr, nullptr, GetModuleHandle(nullptr), nullptr);
    }
    HWND top = CreateWindowExA(0, kWindowClass, "Keepsake VST2 Probe Host",
                               WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
                               width + 40, height + 60, nullptr, nullptr,
                               GetModuleHandle(nullptr), nullptr);
    if (!top) return nullptr;
    HWND child = CreateWindowExA(0, "STATIC", nullptr,
                                 WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                 0, 0, width, height, top, nullptr,
                                 GetModuleHandle(nullptr), nullptr);
    if (!child) {
        std::fprintf(stderr, "failed to create child host window (%lu)\n",
                     static_cast<unsigned long>(GetLastError()));
        DestroyWindow(top);
        return nullptr;
    }
    return child;
}

template <typename Fn, typename Result>
void run_with_optional_worker(const char *label,
                              ThreadMode mode,
                              int timeout_ms,
                              Fn &&fn,
                              Result &result,
                              unsigned exit_code) {
    std::atomic<bool> done{false};
    auto run = [&]() {
        result = fn();
        done.store(true, std::memory_order_release);
    };
    if (mode == ThreadMode::Current) {
        run();
        return;
    }
    std::thread worker(run);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (!done.load(std::memory_order_acquire)) {
        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            std::printf("[%s] TIMEOUT after %d ms on worker thread\n", label, timeout_ms);
            std::fflush(stdout);
            worker.detach();
            ExitProcess(exit_code);
        }
        Sleep(16);
    }
    worker.join();
}

bool parse_args(int argc, char **argv, ProbeOptions &opts) {
    if (argc < 2) return false;
    opts.plugin_path = argv[1];
    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--open-editor") == 0) opts.open_editor = true;
        else if (strcmp(argv[i], "--rect-twice") == 0) opts.rect_twice = true;
        else if (strcmp(argv[i], "--activate") == 0) opts.activate = true;
        else if (strcmp(argv[i], "--get-chunk") == 0) opts.get_chunk = true;
        else if (strcmp(argv[i], "--set-chunk") == 0) opts.set_chunk = true;
        else if (strcmp(argv[i], "--chunk-during-process") == 0) opts.chunk_during_process = true;
        else if (strcmp(argv[i], "--open-during-process") == 0) opts.open_during_process = true;
        else if (strcmp(argv[i], "--editor-chunk-during-process") == 0) opts.editor_chunk_during_process = true;
        else if (strcmp(argv[i], "--bridge-host-mode") == 0) opts.bridge_host_mode = true;
        else if (strcmp(argv[i], "--bridge-gate-mode") == 0) opts.bridge_gate_mode = true;
        else if (strcmp(argv[i], "--bridge-marshal-mode") == 0) opts.bridge_marshal_mode = true;
        else if (strcmp(argv[i], "--async-open-marshal-mode") == 0) opts.async_open_marshal_mode = true;
        else if (strcmp(argv[i], "--parent") == 0 && i + 1 < argc) {
            const char *value = argv[++i];
            if (strcmp(value, "child") == 0) opts.parent_mode = ParentMode::Child;
            else if (strcmp(value, "top") == 0) opts.parent_mode = ParentMode::Top;
            else if (strcmp(value, "none") == 0) opts.parent_mode = ParentMode::None;
            else return false;
        } else if (strcmp(argv[i], "--load-thread") == 0 && i + 1 < argc) {
            const char *value = argv[++i];
            if (strcmp(value, "current") == 0) opts.load_thread_mode = ThreadMode::Current;
            else if (strcmp(value, "worker") == 0) opts.load_thread_mode = ThreadMode::Worker;
            else return false;
        } else if (strcmp(argv[i], "--rect-thread") == 0 && i + 1 < argc) {
            const char *value = argv[++i];
            if (strcmp(value, "current") == 0) opts.rect_thread_mode = ThreadMode::Current;
            else if (strcmp(value, "worker") == 0) opts.rect_thread_mode = ThreadMode::Worker;
            else return false;
        } else if (strcmp(argv[i], "--open-thread") == 0 && i + 1 < argc) {
            const char *value = argv[++i];
            if (strcmp(value, "current") == 0) opts.open_thread_mode = ThreadMode::Current;
            else if (strcmp(value, "worker") == 0) opts.open_thread_mode = ThreadMode::Worker;
            else return false;
        } else if (strcmp(argv[i], "--process-thread") == 0 && i + 1 < argc) {
            const char *value = argv[++i];
            if (strcmp(value, "current") == 0) opts.process_thread_mode = ThreadMode::Current;
            else if (strcmp(value, "worker") == 0) opts.process_thread_mode = ThreadMode::Worker;
            else return false;
        } else if (strcmp(argv[i], "--chunk-thread") == 0 && i + 1 < argc) {
            const char *value = argv[++i];
            if (strcmp(value, "current") == 0) opts.chunk_thread_mode = ThreadMode::Current;
            else if (strcmp(value, "worker") == 0) opts.chunk_thread_mode = ThreadMode::Worker;
            else return false;
        } else if (strcmp(argv[i], "--idle-ms") == 0 && i + 1 < argc) opts.idle_ms = std::atoi(argv[++i]);
        else if (strcmp(argv[i], "--load-timeout-ms") == 0 && i + 1 < argc) opts.load_timeout_ms = std::atoi(argv[++i]);
        else if (strcmp(argv[i], "--rect-timeout-ms") == 0 && i + 1 < argc) opts.rect_timeout_ms = std::atoi(argv[++i]);
        else if (strcmp(argv[i], "--open-timeout-ms") == 0 && i + 1 < argc) opts.open_timeout_ms = std::atoi(argv[++i]);
        else if (strcmp(argv[i], "--process-timeout-ms") == 0 && i + 1 < argc) opts.process_timeout_ms = std::atoi(argv[++i]);
        else if (strcmp(argv[i], "--chunk-timeout-ms") == 0 && i + 1 < argc) opts.chunk_timeout_ms = std::atoi(argv[++i]);
        else if (strcmp(argv[i], "--process-blocks") == 0 && i + 1 < argc) opts.process_blocks = std::atoi(argv[++i]);
        else if (strcmp(argv[i], "--process-sleep-ms") == 0 && i + 1 < argc) opts.process_sleep_ms = std::atoi(argv[++i]);
        else if (strcmp(argv[i], "--sample-rate") == 0 && i + 1 < argc) opts.sample_rate = std::atoi(argv[++i]);
        else if (strcmp(argv[i], "--block-size") == 0 && i + 1 < argc) opts.block_size = std::atoi(argv[++i]);
        else if (strcmp(argv[i], "--callback-delay-ms") == 0 && i + 1 < argc) opts.callback_delay_ms = std::atoi(argv[++i]);
        else if (strcmp(argv[i], "--get-time-delay-ms") == 0 && i + 1 < argc) opts.get_time_delay_ms = std::atoi(argv[++i]);
        else if (strcmp(argv[i], "--gate-delay-ms") == 0 && i + 1 < argc) opts.gate_delay_ms = std::atoi(argv[++i]);
        else return false;
    }
    return true;
}

void print_usage(const char *argv0) {
    std::fprintf(stderr,
                 "usage: %s <plugin.dll> [--open-editor] [--rect-twice]\n"
                 "       [--parent child|top|none]\n"
                 "       [--load-thread current|worker] [--rect-thread current|worker]\n"
                 "       [--open-thread current|worker] [--idle-ms N]\n"
                 "       [--activate] [--sample-rate N] [--block-size N]\n"
                 "       [--process-blocks N] [--process-thread current|worker]\n"
                 "       [--get-chunk] [--set-chunk] [--chunk-thread current|worker]\n"
                 "       [--chunk-during-process]\n"
                 "       [--open-during-process] [--editor-chunk-during-process]\n"
                 "       [--bridge-host-mode] [--callback-delay-ms N] [--get-time-delay-ms N]\n"
                 "       [--bridge-gate-mode] [--gate-delay-ms N]\n"
                 "       [--bridge-marshal-mode] [--async-open-marshal-mode]\n",
                 argv0);
}

bool load_plugin_on_current_thread(const ProbeOptions &opts, ProbeState &state) {
    double t0 = now_ms();
    state.module = LoadLibraryA(opts.plugin_path.c_str());
    if (!state.module) {
        std::fprintf(stderr, "LoadLibrary failed (%lu)\n", static_cast<unsigned long>(GetLastError()));
        return false;
    }
    auto *entry = reinterpret_cast<VstEntry>(GetProcAddress(state.module, "VSTPluginMain"));
    if (!entry) entry = reinterpret_cast<VstEntry>(GetProcAddress(state.module, "main"));
    if (!entry) {
        std::fprintf(stderr, "no VST entry point found\n");
        return false;
    }
    state.effect = entry(host_callback);
    if (!state.effect || state.effect->magic != kEffectMagic) {
        std::fprintf(stderr, "entry returned invalid effect\n");
        return false;
    }
    if (state.effect->dispatcher) {
        with_bridge_gate(opts, "effOpen", opts.load_timeout_ms, [&]() {
            state.effect->dispatcher(state.effect, effOpen, 0, 0, nullptr, 0.0f);
            return 0;
        });
    }
    std::printf("[load] load+effOpen %.1f ms thread=%lu\n",
                now_ms() - t0, static_cast<unsigned long>(GetCurrentThreadId()));
    return true;
}

bool load_plugin(const ProbeOptions &opts, ProbeState &state) {
    bool ok = false;
    run_with_optional_worker("load", opts.load_thread_mode, opts.load_timeout_ms,
                             [&]() { return load_plugin_on_current_thread(opts, state); },
                             ok, 125);
    return ok;
}

PluginInfo collect_plugin_info(const ProbeState &state) {
    PluginInfo info;
    if (!state.effect) return info;
    char name[256] = {}, vendor[256] = {}, product[256] = {};
    if (state.effect->dispatcher) {
        state.effect->dispatcher(state.effect, effGetEffectName, 0, 0, name, 0.0f);
        state.effect->dispatcher(state.effect, effGetVendorString, 0, 0, vendor, 0.0f);
        state.effect->dispatcher(state.effect, effGetProductString, 0, 0, product, 0.0f);
    }
    info.name = name;
    info.vendor = vendor;
    info.product = product;
    info.inputs = state.effect->numInputs;
    info.outputs = state.effect->numOutputs;
    info.params = state.effect->numParams;
    info.flags = static_cast<unsigned>(state.effect->flags);
    info.unique_id = static_cast<unsigned>(state.effect->uniqueID);
    return info;
}

void print_plugin_info(const PluginInfo &info) {
    std::printf("plugin.name=%s\n", info.name.c_str());
    std::printf("plugin.vendor=%s\n", info.vendor.c_str());
    std::printf("plugin.product=%s\n", info.product.c_str());
    std::printf("plugin.inputs=%d outputs=%d params=%d flags=0x%08X uniqueID=0x%08X\n",
                info.inputs, info.outputs, info.params, info.flags, info.unique_id);
}

void unload_plugin(ProbeState &state) {
    if (state.effect && state.effect->dispatcher) {
        state.effect->dispatcher(state.effect, effClose, 0, 0, nullptr, 0.0f);
    }
    state.effect = nullptr;
    if (state.module) {
        FreeLibrary(state.module);
        state.module = nullptr;
    }
}

bool query_editor_rect_once(const ProbeOptions &opts,
                            ProbeState &state,
                            const char *label,
                            int &width,
                            int &height) {
    if (!state.effect || !state.effect->dispatcher) return false;
    struct ERect { int16_t top, left, bottom, right; };
    ERect *rect = nullptr;
    double t0 = now_ms();
    with_bridge_gate(opts, label, opts.rect_timeout_ms, [&]() {
        state.effect->dispatcher(state.effect, effEditGetRect, 0, 0, &rect, 0.0f);
        return 0;
    });
    std::printf("[%s] effEditGetRect %.1f ms rect=%p\n", label, now_ms() - t0, rect);
    if (!rect) return false;
    width = rect->right - rect->left;
    height = rect->bottom - rect->top;
    std::printf("[%s] rect size=%dx%d raw=%d,%d,%d,%d\n",
                label, width, height,
                static_cast<int>(rect->left), static_cast<int>(rect->top),
                static_cast<int>(rect->right), static_cast<int>(rect->bottom));
    return width > 0 && height > 0;
}

bool query_editor_rect(const ProbeOptions &opts, ProbeState &state,
                       const char *label, int &width, int &height) {
    bool ok = false;
    run_with_optional_worker(label, opts.rect_thread_mode, opts.rect_timeout_ms,
                             [&]() { return query_editor_rect_once(opts, state, label, width, height); },
                             ok, 126);
    return ok;
}

bool activate_plugin(ProbeState &state, const ProbeOptions &opts) {
    if (!state.effect || !state.effect->dispatcher) return false;
    double t0 = now_ms();
    state.effect->dispatcher(state.effect, effSetSampleRate, 0, 0, nullptr, static_cast<float>(opts.sample_rate));
    state.effect->dispatcher(state.effect, effSetBlockSize, 0, static_cast<intptr_t>(opts.block_size), nullptr, 0.0f);
    intptr_t mains_result = with_bridge_gate(opts, "activate", opts.process_timeout_ms, [&]() {
        return state.effect->dispatcher(state.effect, effMainsChanged, 0, 1, nullptr, 0.0f);
    });
    std::printf("[activate] sr=%d block=%d mains_result=%lld %.1f ms thread=%lu\n",
                opts.sample_rate, opts.block_size, static_cast<long long>(mains_result),
                now_ms() - t0, static_cast<unsigned long>(GetCurrentThreadId()));
    return true;
}

void deactivate_plugin(ProbeState &state) {
    if (!state.effect || !state.effect->dispatcher) return;
    double t0 = now_ms();
    intptr_t mains_result = state.effect->dispatcher(state.effect, effMainsChanged, 0, 0, nullptr, 0.0f);
    std::printf("[deactivate] mains_result=%lld %.1f ms thread=%lu\n",
                static_cast<long long>(mains_result),
                now_ms() - t0,
                static_cast<unsigned long>(GetCurrentThreadId()));
}

bool open_editor(ProbeState &state, HWND parent, const ProbeOptions &opts) {
    if (!state.effect || !state.effect->dispatcher) return false;
    intptr_t result = 0;
    g_host_config.editor_open_in_progress = true;
    run_with_optional_worker("open", opts.open_thread_mode, opts.open_timeout_ms,
                             [&]() {
                                 double t0 = now_ms();
                                 intptr_t open_result = with_bridge_gate(opts, "effEditOpen", opts.open_timeout_ms, [&]() {
                                     return state.effect->dispatcher(state.effect, effEditOpen, 0, 0, parent, 0.0f);
                                 });
                                 std::printf("[open] effEditOpen %.1f ms result=%lld parent=%p thread=%lu\n",
                                             now_ms() - t0, static_cast<long long>(open_result),
                                             parent, static_cast<unsigned long>(GetCurrentThreadId()));
                                 return open_result;
                             },
                             result, 124);
    g_host_config.editor_open_in_progress = false;
    return result != 0;
}

void pump_messages_and_idle(const ProbeOptions &opts, ProbeState &state, int duration_ms) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(duration_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (state.effect && state.effect->dispatcher) {
            with_bridge_gate(opts, "effEditIdle", 1000, [&]() {
                state.effect->dispatcher(state.effect, effEditIdle, 0, 0, nullptr, 0.0f);
                return 0;
            });
        }
        Sleep(16);
    }
}

bool get_chunk_once(const ProbeOptions &opts, ProbeState &state, std::vector<uint8_t> &chunk) {
    if (!state.effect || !state.effect->dispatcher) return false;
    void *chunk_ptr = nullptr;
    double t0 = now_ms();
    intptr_t chunk_size = with_bridge_gate(opts, "effGetChunk", opts.chunk_timeout_ms, [&]() {
        return state.effect->dispatcher(state.effect, effGetChunk, 0, 0, &chunk_ptr, 0.0f);
    });
    std::printf("[chunk:get] size=%lld ptr=%p %.1f ms thread=%lu\n",
                static_cast<long long>(chunk_size), chunk_ptr,
                now_ms() - t0, static_cast<unsigned long>(GetCurrentThreadId()));
    if (chunk_size <= 0 || !chunk_ptr) {
        chunk.clear();
        return false;
    }
    auto *bytes = static_cast<uint8_t *>(chunk_ptr);
    chunk.assign(bytes, bytes + static_cast<size_t>(chunk_size));
    return true;
}

bool get_chunk(const ProbeOptions &opts, ProbeState &state, std::vector<uint8_t> &chunk) {
    bool ok = false;
    run_with_optional_worker("chunk-get", opts.chunk_thread_mode, opts.chunk_timeout_ms,
                             [&]() { return get_chunk_once(opts, state, chunk); }, ok, 127);
    return ok;
}

bool set_chunk(const ProbeOptions &opts, ProbeState &state, const std::vector<uint8_t> &chunk) {
    bool ok = false;
    run_with_optional_worker("chunk-set", opts.chunk_thread_mode, opts.chunk_timeout_ms,
                             [&]() {
                                 if (!state.effect || !state.effect->dispatcher || chunk.empty()) return false;
                                 double t0 = now_ms();
                                 intptr_t result = with_bridge_gate(opts, "effSetChunk", opts.chunk_timeout_ms, [&]() {
                                     return state.effect->dispatcher(
                                         state.effect, effSetChunk, 0, static_cast<intptr_t>(chunk.size()),
                                         const_cast<uint8_t *>(chunk.data()), 0.0f);
                                 });
                                 std::printf("[chunk:set] size=%zu result=%lld %.1f ms thread=%lu\n",
                                             chunk.size(), static_cast<long long>(result),
                                             now_ms() - t0, static_cast<unsigned long>(GetCurrentThreadId()));
                                 return true;
                             },
                             ok, 128);
    return ok;
}

bool process_blocks_once(ProbeState &state, const ProbeOptions &opts) {
    if (!state.effect || !state.effect->processReplacing || opts.process_blocks <= 0) return false;
    int num_inputs = state.effect->numInputs > 0 ? state.effect->numInputs : 0;
    int num_outputs = state.effect->numOutputs > 0 ? state.effect->numOutputs : 0;
    std::vector<std::vector<float>> input_storage(static_cast<size_t>(num_inputs), std::vector<float>(static_cast<size_t>(opts.block_size), 0.0f));
    std::vector<std::vector<float>> output_storage(static_cast<size_t>(num_outputs), std::vector<float>(static_cast<size_t>(opts.block_size), 0.0f));
    std::vector<float *> input_ptrs(static_cast<size_t>(num_inputs), nullptr);
    std::vector<float *> output_ptrs(static_cast<size_t>(num_outputs), nullptr);
    for (int i = 0; i < num_inputs; ++i) input_ptrs[static_cast<size_t>(i)] = input_storage[static_cast<size_t>(i)].data();
    for (int i = 0; i < num_outputs; ++i) output_ptrs[static_cast<size_t>(i)] = output_storage[static_cast<size_t>(i)].data();
    double t0 = now_ms();
    for (int block = 0; block < opts.process_blocks; ++block) {
        with_bridge_gate(opts, "processReplacing", opts.process_timeout_ms, [&]() {
            state.effect->processReplacing(state.effect,
                                           input_ptrs.empty() ? nullptr : input_ptrs.data(),
                                           output_ptrs.empty() ? nullptr : output_ptrs.data(),
                                           opts.block_size);
        });
        if (opts.process_sleep_ms > 0) Sleep(static_cast<DWORD>(opts.process_sleep_ms));
    }
    float first_sample = (!output_storage.empty() && !output_storage[0].empty()) ? output_storage[0][0] : 0.0f;
    std::printf("[process] blocks=%d block_size=%d outputs=%d first_sample=%f %.1f ms thread=%lu\n",
                opts.process_blocks, opts.block_size, num_outputs, first_sample,
                now_ms() - t0, static_cast<unsigned long>(GetCurrentThreadId()));
    return true;
}

bool process_blocks(const ProbeOptions &opts, ProbeState &state) {
    bool ok = false;
    run_with_optional_worker("process", opts.process_thread_mode, opts.process_timeout_ms,
                             [&]() { return process_blocks_once(state, opts); }, ok, 129);
    return ok;
}

bool run_chunk_during_process(const ProbeOptions &opts, ProbeState &state) {
    if (opts.process_blocks <= 0) return false;
    std::atomic<bool> started{false};
    std::atomic<bool> done{false};
    bool process_ok = false;
    std::vector<uint8_t> chunk;
    std::thread worker([&]() {
        started.store(true, std::memory_order_release);
        process_ok = process_blocks_once(state, opts);
        done.store(true, std::memory_order_release);
    });
    while (!started.load(std::memory_order_acquire)) Sleep(1);
    Sleep(25);
    bool chunk_ok = get_chunk(opts, state, chunk);
    std::printf("[overlap] chunk_during_process chunk_ok=%d chunk_size=%zu process_thread=worker chunk_thread=%s\n",
                chunk_ok ? 1 : 0, chunk.size(), thread_mode_name(opts.chunk_thread_mode));
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(opts.process_timeout_ms);
    while (!done.load(std::memory_order_acquire)) {
        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            std::printf("[overlap] process worker TIMEOUT after %d ms\n", opts.process_timeout_ms);
            std::fflush(stdout);
            worker.detach();
            ExitProcess(130);
        }
        Sleep(16);
    }
    worker.join();
    std::printf("[overlap] process_ok=%d process_done=1\n", process_ok ? 1 : 0);
    return chunk_ok && process_ok;
}

bool wait_for_worker_done(std::thread &worker,
                          std::atomic<bool> &done,
                          ProbeState &state,
                          int timeout_ms,
                          unsigned exit_code) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (!done.load(std::memory_order_acquire)) {
        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (state.effect && state.effect->dispatcher) {
            state.effect->dispatcher(state.effect, effEditIdle, 0, 0, nullptr, 0.0f);
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            std::printf("[overlap] worker TIMEOUT after %d ms\n", timeout_ms);
            std::fflush(stdout);
            worker.detach();
            ExitProcess(exit_code);
        }
        Sleep(16);
    }
    worker.join();
    return true;
}

bool run_open_during_process(const ProbeOptions &opts,
                             ProbeState &state,
                             int width,
                             int height) {
    if (opts.process_blocks <= 0) return false;
    HWND parent = create_parent_window(opts.parent_mode, width, height);
    std::printf("parent_hwnd=%p\n", parent);
    if (!parent) return false;

    std::atomic<bool> done{false};
    bool process_ok = false;
    std::thread worker([&]() {
        process_ok = process_blocks_once(state, opts);
        done.store(true, std::memory_order_release);
    });

    Sleep(25);
    bool open_ok = open_editor(state, parent, opts);
    std::printf("[overlap] open_during_process editor_open_ok=%d\n", open_ok ? 1 : 0);
    if (open_ok) {
        pump_messages_and_idle(opts, state, opts.idle_ms);
        if (state.effect && state.effect->dispatcher) {
            with_bridge_gate(opts, "effEditClose", opts.open_timeout_ms, [&]() {
                state.effect->dispatcher(state.effect, effEditClose, 0, 0, nullptr, 0.0f);
                return 0;
            });
        }
    }

    wait_for_worker_done(worker, done, state, opts.process_timeout_ms, 131);
    std::printf("[overlap] open_during_process process_ok=%d\n", process_ok ? 1 : 0);

    HWND top = GetAncestor(parent, GA_ROOT);
    if (top) DestroyWindow(top);
    return open_ok && process_ok;
}

bool run_editor_chunk_during_process(const ProbeOptions &opts,
                                     ProbeState &state,
                                     int width,
                                     int height) {
    if (opts.process_blocks <= 0) return false;
    HWND parent = create_parent_window(opts.parent_mode, width, height);
    std::printf("parent_hwnd=%p\n", parent);
    if (!parent) return false;

    bool open_ok = open_editor(state, parent, opts);
    std::printf("[overlap] editor_chunk_during_process editor_open_ok=%d\n", open_ok ? 1 : 0);
    if (!open_ok) {
        HWND top = GetAncestor(parent, GA_ROOT);
        if (top) DestroyWindow(top);
        return false;
    }

    std::atomic<bool> done{false};
    bool process_ok = false;
    std::vector<uint8_t> chunk;
    std::thread worker([&]() {
        process_ok = process_blocks_once(state, opts);
        done.store(true, std::memory_order_release);
    });

    Sleep(25);
    bool chunk_ok = get_chunk(opts, state, chunk);
    std::printf("[overlap] editor_chunk_during_process chunk_ok=%d chunk_size=%zu\n",
                chunk_ok ? 1 : 0,
                chunk.size());

    wait_for_worker_done(worker, done, state, opts.process_timeout_ms, 132);
    if (state.effect && state.effect->dispatcher) {
        with_bridge_gate(opts, "effEditClose", opts.open_timeout_ms, [&]() {
            state.effect->dispatcher(state.effect, effEditClose, 0, 0, nullptr, 0.0f);
            return 0;
        });
    }

    std::printf("[overlap] editor_chunk_during_process process_ok=%d\n", process_ok ? 1 : 0);
    HWND top = GetAncestor(parent, GA_ROOT);
    if (top) DestroyWindow(top);
    return chunk_ok && process_ok;
}

void bridge_service_thread(BridgeService *service) {
    {
        std::lock_guard<std::mutex> lock(service->mutex);
        service->start_ok = load_plugin_on_current_thread(service->opts, service->state);
        if (service->start_ok) {
            service->info = collect_plugin_info(service->state);
            if (service->opts.activate || service->opts.process_blocks > 0 ||
                service->opts.chunk_during_process || service->opts.open_during_process ||
                service->opts.editor_chunk_during_process) {
                activate_plugin(service->state, service->opts);
            }
        }
        service->started = true;
    }
    service->done_cv.notify_all();

    std::unique_lock<std::mutex> lock(service->mutex);
    while (!service->stop_requested) {
        service->cv.wait(lock, [&]() { return service->pending_has_command || service->stop_requested; });
        if (service->stop_requested) break;
        ServiceCommand cmd = std::move(service->pending_command);
        service->pending_has_command = false;
        lock.unlock();

        switch (cmd.type) {
        case ServiceCommandType::QueryRect:
            cmd.bool_result = query_editor_rect_once(service->opts, service->state, "rect-1",
                                                     cmd.width, cmd.height);
            break;
        case ServiceCommandType::OpenEditor:
            cmd.bool_result = open_editor(service->state, cmd.parent, service->opts);
            break;
        case ServiceCommandType::OpenEditorAsync:
            service->async_open_parent.store(cmd.parent, std::memory_order_release);
            service->async_open_inflight.store(true, std::memory_order_release);
            service->async_open_completed.store(false, std::memory_order_release);
            service->async_open_result.store(false, std::memory_order_release);
            cmd.bool_result = true;
            break;
        case ServiceCommandType::CloseEditor:
            if (service->state.effect && service->state.effect->dispatcher) {
                with_bridge_gate(service->opts, "effEditClose", service->opts.open_timeout_ms, [&]() {
                    service->state.effect->dispatcher(service->state.effect, effEditClose, 0, 0, nullptr, 0.0f);
                    return 0;
                });
                cmd.bool_result = true;
            }
            break;
        case ServiceCommandType::GetChunk:
            cmd.bool_result = get_chunk_once(service->opts, service->state, cmd.chunk);
            break;
        case ServiceCommandType::Process:
            cmd.bool_result = process_blocks_once(service->state, service->opts);
            break;
        case ServiceCommandType::Stop:
            service->stop_requested = true;
            break;
        case ServiceCommandType::None:
            break;
        }

        lock.lock();
        service->completed_command = std::move(cmd);
        service->completed_command_id = service->completed_command.id;
        service->done_cv.notify_all();
        if (service->async_open_inflight.load(std::memory_order_acquire)) {
            HWND async_parent = service->async_open_parent.load(std::memory_order_acquire);
            lock.unlock();
            bool async_result = open_editor(service->state, async_parent, service->opts);
            lock.lock();
            service->async_open_result.store(async_result, std::memory_order_release);
            service->async_open_completed.store(true, std::memory_order_release);
            service->async_open_inflight.store(false, std::memory_order_release);
            service->done_cv.notify_all();
        }
    }

    lock.unlock();
    if (service->opts.activate || service->opts.process_blocks > 0 ||
        service->opts.chunk_during_process || service->opts.open_during_process ||
        service->opts.editor_chunk_during_process) {
        deactivate_plugin(service->state);
    }
    unload_plugin(service->state);
}

bool start_bridge_service(BridgeService &service, const ProbeOptions &opts) {
    service.opts = opts;
    service.worker = std::thread(bridge_service_thread, &service);
    std::unique_lock<std::mutex> lock(service.mutex);
    service.done_cv.wait(lock, [&]() { return service.started; });
    return service.start_ok;
}

bool bridge_call(BridgeService &service, ServiceCommand &cmd, int timeout_ms) {
    std::unique_lock<std::mutex> lock(service.mutex);
    while (service.pending_has_command) {
        if (service.done_cv.wait_for(lock, std::chrono::milliseconds(10)) == std::cv_status::timeout) {
            continue;
        }
    }
    cmd.id = service.next_command_id++;
    const uint64_t command_id = cmd.id;
    service.pending_command = cmd;
    service.pending_has_command = true;
    service.cv.notify_one();
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (service.completed_command_id != command_id) {
        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (service.done_cv.wait_until(lock, deadline) == std::cv_status::timeout &&
            service.completed_command_id != command_id) {
            std::printf("[marshal] command timeout type=%d timeout=%d ms\n",
                        static_cast<int>(cmd.type), timeout_ms);
            std::fflush(stdout);
            return false;
        }
    }
    cmd = service.completed_command;
    return true;
}

void bridge_post(BridgeService &service, ServiceCommand &cmd) {
    std::unique_lock<std::mutex> lock(service.mutex);
    while (service.pending_has_command) {
        if (service.done_cv.wait_for(lock, std::chrono::milliseconds(10)) == std::cv_status::timeout) {
            continue;
        }
    }
    cmd.id = service.next_command_id++;
    service.pending_command = cmd;
    service.pending_has_command = true;
    service.cv.notify_one();
}

void stop_bridge_service(BridgeService &service) {
    if (!service.worker.joinable()) return;
    {
        std::lock_guard<std::mutex> lock(service.mutex);
        service.stop_requested = true;
        service.cv.notify_one();
    }
    service.worker.join();
}

bool run_open_during_process_marshal(const ProbeOptions &opts, BridgeService &service,
                                     int width, int height) {
    HWND parent = create_parent_window(opts.parent_mode, width, height);
    std::printf("parent_hwnd=%p\n", parent);
    if (!parent) return false;

    std::atomic<bool> done{false};
    bool process_ok = false;
    std::thread worker([&]() {
        ServiceCommand cmd;
        cmd.type = ServiceCommandType::Process;
        if (bridge_call(service, cmd, opts.process_timeout_ms + 2000)) {
            process_ok = cmd.bool_result;
        }
        done.store(true, std::memory_order_release);
    });

    Sleep(25);
    ServiceCommand open_cmd;
    open_cmd.type = ServiceCommandType::OpenEditor;
    open_cmd.parent = parent;
    bool open_ok = bridge_call(service, open_cmd, opts.open_timeout_ms + 2000) && open_cmd.bool_result;
    std::printf("[marshal] open_during_process editor_open_ok=%d\n", open_ok ? 1 : 0);
    if (open_ok) {
        Sleep(opts.idle_ms);
        ServiceCommand close_cmd;
        close_cmd.type = ServiceCommandType::CloseEditor;
        bridge_call(service, close_cmd, opts.open_timeout_ms);
    }

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(opts.process_timeout_ms + 2000);
    while (!done.load(std::memory_order_acquire)) {
        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            std::printf("[marshal] open_during_process worker TIMEOUT\n");
            std::fflush(stdout);
            worker.detach();
            ExitProcess(134);
        }
        Sleep(16);
    }
    worker.join();
    std::printf("[marshal] open_during_process process_ok=%d\n", process_ok ? 1 : 0);
    HWND top = GetAncestor(parent, GA_ROOT);
    if (top) DestroyWindow(top);
    return open_ok && process_ok;
}

bool run_editor_chunk_during_process_marshal(const ProbeOptions &opts, BridgeService &service,
                                             int width, int height) {
    HWND parent = create_parent_window(opts.parent_mode, width, height);
    std::printf("parent_hwnd=%p\n", parent);
    if (!parent) return false;

    ServiceCommand open_cmd;
    open_cmd.type = ServiceCommandType::OpenEditor;
    open_cmd.parent = parent;
    bool open_ok = bridge_call(service, open_cmd, opts.open_timeout_ms + 2000) && open_cmd.bool_result;
    std::printf("[marshal] editor_chunk_during_process editor_open_ok=%d\n", open_ok ? 1 : 0);
    if (!open_ok) {
        HWND top = GetAncestor(parent, GA_ROOT);
        if (top) DestroyWindow(top);
        return false;
    }

    std::atomic<bool> done{false};
    bool process_ok = false;
    std::thread worker([&]() {
        ServiceCommand cmd;
        cmd.type = ServiceCommandType::Process;
        if (bridge_call(service, cmd, opts.process_timeout_ms + 2000)) {
            process_ok = cmd.bool_result;
        }
        done.store(true, std::memory_order_release);
    });

    Sleep(25);
    ServiceCommand chunk_cmd;
    chunk_cmd.type = ServiceCommandType::GetChunk;
    bool chunk_ok = bridge_call(service, chunk_cmd, opts.chunk_timeout_ms + 2000) && chunk_cmd.bool_result;
    std::printf("[marshal] editor_chunk_during_process chunk_ok=%d chunk_size=%zu\n",
                chunk_ok ? 1 : 0, chunk_cmd.chunk.size());

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(opts.process_timeout_ms + 2000);
    while (!done.load(std::memory_order_acquire)) {
        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            std::printf("[marshal] editor_chunk_during_process worker TIMEOUT\n");
            std::fflush(stdout);
            worker.detach();
            ExitProcess(135);
        }
        Sleep(16);
    }
    worker.join();

    ServiceCommand close_cmd;
    close_cmd.type = ServiceCommandType::CloseEditor;
    bridge_call(service, close_cmd, opts.open_timeout_ms);

    std::printf("[marshal] editor_chunk_during_process process_ok=%d\n", process_ok ? 1 : 0);
    HWND top = GetAncestor(parent, GA_ROOT);
    if (top) DestroyWindow(top);
    return chunk_ok && process_ok;
}

bool run_open_editor_marshal(const ProbeOptions &opts, BridgeService &service,
                             int width, int height) {
    HWND parent = create_parent_window(opts.parent_mode, width, height);
    std::printf("parent_hwnd=%p\n", parent);
    if (!parent) return false;

    ServiceCommand open_cmd;
    open_cmd.type = ServiceCommandType::OpenEditor;
    open_cmd.parent = parent;
    bool open_ok = bridge_call(service, open_cmd, opts.open_timeout_ms + 2000) && open_cmd.bool_result;
    std::printf("editor_open_ok=%d\n", open_ok ? 1 : 0);
    if (open_ok) {
        Sleep(opts.idle_ms);
        ServiceCommand close_cmd;
        close_cmd.type = ServiceCommandType::CloseEditor;
        bridge_call(service, close_cmd, opts.open_timeout_ms);
    }

    HWND top = GetAncestor(parent, GA_ROOT);
    if (top) DestroyWindow(top);
    return open_ok;
}

bool run_open_editor_async_marshal(const ProbeOptions &opts, BridgeService &service,
                                   int width, int height) {
    HWND parent = create_parent_window(opts.parent_mode, width, height);
    std::printf("parent_hwnd=%p\n", parent);
    if (!parent) return false;

    ServiceCommand open_cmd;
    open_cmd.type = ServiceCommandType::OpenEditorAsync;
    open_cmd.parent = parent;
    bridge_post(service, open_cmd);
    std::printf("[marshal-async] open posted parent=%p\n", parent);

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(opts.idle_ms);
    bool completed = false;
    bool result = false;
    while (std::chrono::steady_clock::now() < deadline) {
        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (service.async_open_completed.load(std::memory_order_acquire)) {
            completed = true;
            result = service.async_open_result.load(std::memory_order_acquire);
            break;
        }
        Sleep(16);
    }

    std::printf("[marshal-async] open_completed=%d open_result=%d observed_ms=%d\n",
                completed ? 1 : 0, result ? 1 : 0, opts.idle_ms);

    if (completed && result) {
        ServiceCommand close_cmd;
        close_cmd.type = ServiceCommandType::CloseEditor;
        bridge_call(service, close_cmd, opts.open_timeout_ms);
    }

    HWND top = GetAncestor(parent, GA_ROOT);
    if (top) DestroyWindow(top);
    return completed && result;
}

}  // namespace

int main(int argc, char **argv) {
    ProbeOptions opts;
    if (!parse_args(argc, argv, opts)) {
        print_usage(argv[0]);
        return 1;
    }

    std::printf("windows-vst2-probe\n");
    std::printf("plugin=%s\n", opts.plugin_path.c_str());
    std::printf("open_editor=%d\n", opts.open_editor ? 1 : 0);
    std::printf("rect_twice=%d\n", opts.rect_twice ? 1 : 0);
    std::printf("activate=%d\n", opts.activate ? 1 : 0);
    std::printf("get_chunk=%d\n", opts.get_chunk ? 1 : 0);
    std::printf("set_chunk=%d\n", opts.set_chunk ? 1 : 0);
    std::printf("chunk_during_process=%d\n", opts.chunk_during_process ? 1 : 0);
    std::printf("open_during_process=%d\n", opts.open_during_process ? 1 : 0);
    std::printf("editor_chunk_during_process=%d\n", opts.editor_chunk_during_process ? 1 : 0);
    std::printf("bridge_host_mode=%d\n", opts.bridge_host_mode ? 1 : 0);
    std::printf("bridge_gate_mode=%d\n", opts.bridge_gate_mode ? 1 : 0);
    std::printf("bridge_marshal_mode=%d\n", opts.bridge_marshal_mode ? 1 : 0);
    std::printf("async_open_marshal_mode=%d\n", opts.async_open_marshal_mode ? 1 : 0);
    std::printf("parent_mode=%s\n", opts.parent_mode == ParentMode::Child ? "child" : opts.parent_mode == ParentMode::Top ? "top" : "none");
    std::printf("load_thread_mode=%s\n", thread_mode_name(opts.load_thread_mode));
    std::printf("rect_thread_mode=%s\n", thread_mode_name(opts.rect_thread_mode));
    std::printf("open_thread_mode=%s\n", thread_mode_name(opts.open_thread_mode));
    std::printf("process_thread_mode=%s\n", thread_mode_name(opts.process_thread_mode));
    std::printf("chunk_thread_mode=%s\n", thread_mode_name(opts.chunk_thread_mode));
    std::printf("sample_rate=%d\n", opts.sample_rate);
    std::printf("block_size=%d\n", opts.block_size);
    std::printf("process_blocks=%d\n", opts.process_blocks);
    std::printf("process_sleep_ms=%d\n", opts.process_sleep_ms);
    std::printf("callback_delay_ms=%d\n", opts.callback_delay_ms);
    std::printf("get_time_delay_ms=%d\n", opts.get_time_delay_ms);
    std::printf("gate_delay_ms=%d\n", opts.gate_delay_ms);

    g_host_config.bridge_host_mode = opts.bridge_host_mode;
    g_host_config.callback_delay_ms = opts.callback_delay_ms;
    g_host_config.get_time_delay_ms = opts.get_time_delay_ms;
    g_host_config.editor_open_in_progress = false;

    int width = 640;
    int height = 480;
    bool rect_ok = false;

    if (opts.bridge_marshal_mode) {
        BridgeService service;
        if (!start_bridge_service(service, opts)) {
            stop_bridge_service(service);
            return 1;
        }
        print_plugin_info(service.info);

        ServiceCommand rect_cmd;
        rect_cmd.type = ServiceCommandType::QueryRect;
        rect_ok = bridge_call(service, rect_cmd, opts.rect_timeout_ms + 2000) && rect_cmd.bool_result;
        if (rect_ok) {
            width = rect_cmd.width;
            height = rect_cmd.height;
        }
        if (opts.rect_twice) {
            ServiceCommand rect2_cmd;
            rect2_cmd.type = ServiceCommandType::QueryRect;
            bridge_call(service, rect2_cmd, opts.rect_timeout_ms + 2000);
        }

        if (opts.open_during_process) {
            bool overlap_ok = run_open_during_process_marshal(
                opts, service, rect_ok ? width : 640, rect_ok ? height : 480);
            std::printf("open_during_process_ok=%d\n", overlap_ok ? 1 : 0);
        }

        if (opts.open_editor && !opts.open_during_process && !opts.editor_chunk_during_process) {
            if (opts.async_open_marshal_mode) {
                bool open_ok = run_open_editor_async_marshal(
                    opts, service, rect_ok ? width : 640, rect_ok ? height : 480);
                std::printf("editor_open_async_ok=%d\n", open_ok ? 1 : 0);
            } else {
                run_open_editor_marshal(opts, service, rect_ok ? width : 640, rect_ok ? height : 480);
            }
        }

        if (opts.editor_chunk_during_process) {
            bool overlap_ok = run_editor_chunk_during_process_marshal(
                opts, service, rect_ok ? width : 640, rect_ok ? height : 480);
            std::printf("editor_chunk_during_process_ok=%d\n", overlap_ok ? 1 : 0);
        }

        stop_bridge_service(service);
    } else {
        ProbeState state;
        if (!load_plugin(opts, state)) {
            unload_plugin(state);
            return 1;
        }
        print_plugin_info(collect_plugin_info(state));

        if (opts.activate || opts.process_blocks > 0 || opts.chunk_during_process) {
            activate_plugin(state, opts);
        }

        rect_ok = query_editor_rect(opts, state, "rect-1", width, height);
        if (opts.rect_twice) {
            int rect2w = width;
            int rect2h = height;
            query_editor_rect(opts, state, "rect-2", rect2w, rect2h);
        }

        if (opts.open_editor && !opts.open_during_process && !opts.editor_chunk_during_process) {
            HWND parent = create_parent_window(opts.parent_mode, rect_ok ? width : 640, rect_ok ? height : 480);
            std::printf("parent_hwnd=%p\n", parent);
            bool open_ok = open_editor(state, parent, opts);
            std::printf("editor_open_ok=%d\n", open_ok ? 1 : 0);
            if (open_ok) {
                pump_messages_and_idle(opts, state, opts.idle_ms);
                if (state.effect && state.effect->dispatcher) {
                    state.effect->dispatcher(state.effect, effEditClose, 0, 0, nullptr, 0.0f);
                }
            }
            if (parent) {
                HWND top = GetAncestor(parent, GA_ROOT);
                if (top) DestroyWindow(top);
            }
        }

        std::vector<uint8_t> chunk;
        if (opts.get_chunk) {
            bool chunk_ok = get_chunk(opts, state, chunk);
            std::printf("chunk_get_ok=%d chunk_size=%zu\n", chunk_ok ? 1 : 0, chunk.size());
            if (chunk_ok && opts.set_chunk) {
                bool set_ok = set_chunk(opts, state, chunk);
                std::printf("chunk_set_ok=%d\n", set_ok ? 1 : 0);
            }
        }

        if (opts.process_blocks > 0 && !opts.chunk_during_process) {
            bool process_ok = process_blocks(opts, state);
            std::printf("process_ok=%d\n", process_ok ? 1 : 0);
        }

        if (opts.chunk_during_process) {
            bool overlap_ok = run_chunk_during_process(opts, state);
            std::printf("chunk_during_process_ok=%d\n", overlap_ok ? 1 : 0);
        }

        if (opts.open_during_process) {
            bool overlap_ok = run_open_during_process(opts, state, rect_ok ? width : 640, rect_ok ? height : 480);
            std::printf("open_during_process_ok=%d\n", overlap_ok ? 1 : 0);
        }

        if (opts.editor_chunk_during_process) {
            bool overlap_ok = run_editor_chunk_during_process(opts, state, rect_ok ? width : 640, rect_ok ? height : 480);
            std::printf("editor_chunk_during_process_ok=%d\n", overlap_ok ? 1 : 0);
        }

        if (opts.activate || opts.process_blocks > 0 || opts.chunk_during_process ||
            opts.open_during_process || opts.editor_chunk_during_process) {
            deactivate_plugin(state);
        }

        unload_plugin(state);
    }

    std::printf("done\n");
    return 0;
}
