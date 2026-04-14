// windows-vst2-probe: minimal standalone Windows VST2 host probe.
//
// This is intentionally separate from Keepsake's bridge/CLAP plumbing so we can
// answer small questions first:
// - Does the plugin load at all?
// - Does effEditGetRect return?
// - Does effEditOpen return?
// - Does behavior change based on parent HWND shape or calling thread?
//
// Usage:
//   windows-vst2-probe <plugin.dll> [--open-editor]
//       [--parent child|top|none]
//       [--load-thread current|worker] [--load-timeout-ms N]
//       [--thread current|worker]
//       [--idle-ms N] [--open-timeout-ms N] [--rect-twice]

#include <vestige/vestige.h>

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

namespace {

using VstEntry = AEffect *(__cdecl *)(audioMasterCallback);

constexpr const char *kWindowClass = "KeepsakeVst2ProbeWindow";

enum class ParentMode {
    None,
    Child,
    Top,
};

enum class OpenThreadMode {
    Current,
    Worker,
};

enum class LoadThreadMode {
    Current,
    Worker,
};

struct ProbeOptions {
    std::string plugin_path;
    bool open_editor = false;
    bool rect_twice = false;
    ParentMode parent_mode = ParentMode::Child;
    LoadThreadMode load_thread_mode = LoadThreadMode::Current;
    OpenThreadMode open_thread_mode = OpenThreadMode::Current;
    int idle_ms = 1500;
    int load_timeout_ms = 5000;
    int open_timeout_ms = 2000;
};

struct ProbeState {
    AEffect *effect = nullptr;
    HMODULE module = nullptr;
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

const char *opcode_name(int32_t opcode) {
    switch (opcode) {
    case audioMasterAutomate: return "audioMasterAutomate";
    case audioMasterVersion: return "audioMasterVersion";
    case audioMasterCurrentId: return "audioMasterCurrentId";
    case audioMasterIdle: return "audioMasterIdle";
    case audioMasterGetTime: return "audioMasterGetTime";
    case audioMasterProcessEvents: return "audioMasterProcessEvents";
    case audioMasterGetSampleRate: return "audioMasterGetSampleRate";
    case audioMasterGetBlockSize: return "audioMasterGetBlockSize";
    case audioMasterGetVendorString: return "audioMasterGetVendorString";
    case audioMasterGetProductString: return "audioMasterGetProductString";
    case audioMasterGetVendorVersion: return "audioMasterGetVendorVersion";
    case audioMasterCanDo: return "audioMasterCanDo";
    case audioMasterGetLanguage: return "audioMasterGetLanguage";
    case audioMasterSizeWindow: return "audioMasterSizeWindow";
    case audioMasterUpdateDisplay: return "audioMasterUpdateDisplay";
    case audioMasterBeginEdit: return "audioMasterBeginEdit";
    case audioMasterEndEdit: return "audioMasterEndEdit";
    default: return "audioMaster?";
    }
}

intptr_t __cdecl host_callback(
    AEffect *effect, int32_t opcode, int32_t index, intptr_t value, void *ptr, float opt) {
    intptr_t result = 0;

    switch (opcode) {
    case audioMasterVersion: result = 2400; break;
    case audioMasterCurrentId: result = effect ? effect->uniqueID : 0; break;
    case audioMasterIdle: result = 1; break;
    case audioMasterGetSampleRate: result = 44100; break;
    case audioMasterGetBlockSize: result = 512; break;
    case audioMasterGetVendorVersion: result = 1; break;
    case audioMasterGetLanguage: result = kVstLangEnglish; break;
    case audioMasterCanDo:
        if (ptr) {
            auto *query = static_cast<const char *>(ptr);
            if (strcmp(query, "sendVstEvents") == 0 ||
                strcmp(query, "sendVstMidiEvent") == 0 ||
                strcmp(query, "receiveVstEvents") == 0 ||
                strcmp(query, "receiveVstMidiEvent") == 0 ||
                strcmp(query, "sizeWindow") == 0) {
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
    default:
        result = 0;
        break;
    }

    std::printf("hostcb opcode=%s idx=%d val=%lld ptr=%p opt=%.3f -> %lld\n",
                opcode_name(opcode),
                opcode == audioMasterSizeWindow ? index : index,
                static_cast<long long>(value),
                ptr,
                opt,
                static_cast<long long>(result));
    return result;
}

LRESULT CALLBACK ProbeWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        return 0;
    default:
        return DefWindowProcA(hwnd, msg, wParam, lParam);
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
        return CreateWindowExA(0,
                               kWindowClass,
                               "Keepsake VST2 Probe",
                               WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                               CW_USEDEFAULT,
                               CW_USEDEFAULT,
                               width,
                               height,
                               nullptr,
                               nullptr,
                               GetModuleHandle(nullptr),
                               nullptr);
    }

    HWND top = CreateWindowExA(0,
                               kWindowClass,
                               "Keepsake VST2 Probe Host",
                               WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                               CW_USEDEFAULT,
                               CW_USEDEFAULT,
                               width + 40,
                               height + 60,
                               nullptr,
                               nullptr,
                               GetModuleHandle(nullptr),
                               nullptr);
    if (!top) return nullptr;

    HWND child = CreateWindowExA(0,
                                 "STATIC",
                                 nullptr,
                                 WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                 0,
                                 0,
                                 width,
                                 height,
                                 top,
                                 nullptr,
                                 GetModuleHandle(nullptr),
                                 nullptr);
    if (!child) {
        std::fprintf(stderr, "failed to create child host window (%lu)\n",
                     static_cast<unsigned long>(GetLastError()));
        DestroyWindow(top);
        return nullptr;
    }

    return child;
}

void pump_messages_and_idle(ProbeState &state, int duration_ms) {
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(duration_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }

        if (state.effect && state.effect->dispatcher) {
            state.effect->dispatcher(state.effect, effEditIdle, 0, 0, nullptr, 0.0f);
        }

        Sleep(16);
    }
}

bool parse_args(int argc, char **argv, ProbeOptions &opts) {
    if (argc < 2) return false;
    opts.plugin_path = argv[1];

    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--open-editor") == 0) {
            opts.open_editor = true;
        } else if (strcmp(argv[i], "--rect-twice") == 0) {
            opts.rect_twice = true;
        } else if (strcmp(argv[i], "--parent") == 0 && i + 1 < argc) {
            const char *value = argv[++i];
            if (strcmp(value, "child") == 0) opts.parent_mode = ParentMode::Child;
            else if (strcmp(value, "top") == 0) opts.parent_mode = ParentMode::Top;
            else if (strcmp(value, "none") == 0) opts.parent_mode = ParentMode::None;
            else return false;
        } else if (strcmp(argv[i], "--load-thread") == 0 && i + 1 < argc) {
            const char *value = argv[++i];
            if (strcmp(value, "current") == 0) opts.load_thread_mode = LoadThreadMode::Current;
            else if (strcmp(value, "worker") == 0) opts.load_thread_mode = LoadThreadMode::Worker;
            else return false;
        } else if (strcmp(argv[i], "--thread") == 0 && i + 1 < argc) {
            const char *value = argv[++i];
            if (strcmp(value, "current") == 0) opts.open_thread_mode = OpenThreadMode::Current;
            else if (strcmp(value, "worker") == 0) opts.open_thread_mode = OpenThreadMode::Worker;
            else return false;
        } else if (strcmp(argv[i], "--idle-ms") == 0 && i + 1 < argc) {
            opts.idle_ms = std::atoi(argv[++i]);
        } else if (strcmp(argv[i], "--load-timeout-ms") == 0 && i + 1 < argc) {
            opts.load_timeout_ms = std::atoi(argv[++i]);
        } else if (strcmp(argv[i], "--open-timeout-ms") == 0 && i + 1 < argc) {
            opts.open_timeout_ms = std::atoi(argv[++i]);
        } else {
            return false;
        }
    }

    return true;
}

void print_usage(const char *argv0) {
    std::fprintf(stderr,
                 "usage: %s <plugin.dll> [--open-editor] [--rect-twice]\n"
                 "       [--parent child|top|none]\n"
                 "       [--load-thread current|worker] [--load-timeout-ms N]\n"
                 "       [--thread current|worker] [--idle-ms N] [--open-timeout-ms N]\n",
                 argv0);
}

bool load_plugin_on_current_thread(const ProbeOptions &opts, ProbeState &state) {
    double t0 = now_ms();
    state.module = LoadLibraryA(opts.plugin_path.c_str());
    if (!state.module) {
        std::fprintf(stderr, "LoadLibrary failed (%lu)\n",
                     static_cast<unsigned long>(GetLastError()));
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
        state.effect->dispatcher(state.effect, effOpen, 0, 0, nullptr, 0.0f);
    }

    double elapsed = now_ms() - t0;
    std::printf("[load] load+effOpen %.1f ms thread=%lu\n",
                elapsed,
                static_cast<unsigned long>(GetCurrentThreadId()));
    return true;
}

bool load_plugin(const ProbeOptions &opts, ProbeState &state) {
    if (opts.load_thread_mode == LoadThreadMode::Current) {
        return load_plugin_on_current_thread(opts, state);
    }

    std::atomic<bool> done{false};
    std::atomic<bool> ok{false};
    std::thread worker([&]() {
        ok.store(load_plugin_on_current_thread(opts, state), std::memory_order_release);
        done.store(true, std::memory_order_release);
    });

    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(opts.load_timeout_ms);
    while (!done.load(std::memory_order_acquire)) {
        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }

        if (std::chrono::steady_clock::now() >= deadline) {
            std::printf("[load] TIMEOUT after %d ms on worker thread\n",
                        opts.load_timeout_ms);
            std::fflush(stdout);
            worker.detach();
            ExitProcess(125);
        }

        Sleep(16);
    }

    worker.join();
    return ok.load(std::memory_order_acquire);
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

bool query_editor_rect(ProbeState &state, const char *label, int &width, int &height) {
    if (!state.effect || !state.effect->dispatcher) return false;

    struct ERect { int16_t top, left, bottom, right; };
    ERect *rect = nullptr;
    double t0 = now_ms();
    state.effect->dispatcher(state.effect, effEditGetRect, 0, 0, &rect, 0.0f);
    double elapsed = now_ms() - t0;

    std::printf("[%s] effEditGetRect %.1f ms rect=%p\n", label, elapsed, rect);
    if (!rect) return false;

    width = rect->right - rect->left;
    height = rect->bottom - rect->top;
    std::printf("[%s] rect size=%dx%d raw=%d,%d,%d,%d\n",
                label,
                width,
                height,
                static_cast<int>(rect->left),
                static_cast<int>(rect->top),
                static_cast<int>(rect->right),
                static_cast<int>(rect->bottom));
    return width > 0 && height > 0;
}

bool open_editor(ProbeState &state, HWND parent, const ProbeOptions &opts) {
    if (!state.effect || !state.effect->dispatcher) return false;

    std::atomic<bool> done{false};
    std::atomic<intptr_t> result{0};

    auto run_open = [&]() {
        double t0 = now_ms();
        intptr_t open_result = state.effect->dispatcher(state.effect,
                                                        effEditOpen,
                                                        0,
                                                        0,
                                                        parent,
                                                        0.0f);
        double elapsed = now_ms() - t0;
        std::printf("[open] effEditOpen %.1f ms result=%lld parent=%p thread=%lu\n",
                    elapsed,
                    static_cast<long long>(open_result),
                    parent,
                    static_cast<unsigned long>(GetCurrentThreadId()));
        result.store(open_result, std::memory_order_release);
        done.store(true, std::memory_order_release);
    };

    if (opts.open_thread_mode == OpenThreadMode::Current) {
        run_open();
        return result.load(std::memory_order_acquire) != 0;
    }

    std::thread worker(run_open);
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(opts.open_timeout_ms);
    while (!done.load(std::memory_order_acquire)) {
        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }

        if (std::chrono::steady_clock::now() >= deadline) {
            std::printf("[open] TIMEOUT after %d ms on worker thread\n",
                        opts.open_timeout_ms);
            std::fflush(stdout);
            worker.detach();
            ExitProcess(124);
        }

        Sleep(16);
    }

    worker.join();
    return result.load(std::memory_order_acquire) != 0;
}

void print_plugin_info(ProbeState &state) {
    if (!state.effect) return;

    char name[256] = {};
    char vendor[256] = {};
    char product[256] = {};

    if (state.effect->dispatcher) {
        state.effect->dispatcher(state.effect, effGetEffectName, 0, 0, name, 0.0f);
        state.effect->dispatcher(state.effect, effGetVendorString, 0, 0, vendor, 0.0f);
        state.effect->dispatcher(state.effect, effGetProductString, 0, 0, product, 0.0f);
    }

    std::printf("plugin.name=%s\n", name);
    std::printf("plugin.vendor=%s\n", vendor);
    std::printf("plugin.product=%s\n", product);
    std::printf("plugin.inputs=%d outputs=%d params=%d flags=0x%08X uniqueID=0x%08X\n",
                state.effect->numInputs,
                state.effect->numOutputs,
                state.effect->numParams,
                static_cast<unsigned>(state.effect->flags),
                static_cast<unsigned>(state.effect->uniqueID));
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
    std::printf("parent_mode=%s\n",
                opts.parent_mode == ParentMode::Child ? "child" :
                opts.parent_mode == ParentMode::Top ? "top" : "none");
    std::printf("load_thread_mode=%s\n",
                opts.load_thread_mode == LoadThreadMode::Current ? "current" : "worker");
    std::printf("thread_mode=%s\n",
                opts.open_thread_mode == OpenThreadMode::Current ? "current" : "worker");

    ProbeState state;
    if (!load_plugin(opts, state)) {
        unload_plugin(state);
        return 1;
    }

    print_plugin_info(state);

    int width = 640;
    int height = 480;
    bool rect_ok = query_editor_rect(state, "rect-1", width, height);
    if (opts.rect_twice) {
        int rect2w = width;
        int rect2h = height;
        query_editor_rect(state, "rect-2", rect2w, rect2h);
    }

    if (opts.open_editor) {
        HWND parent = create_parent_window(opts.parent_mode,
                                           rect_ok ? width : 640,
                                           rect_ok ? height : 480);
        std::printf("parent_hwnd=%p\n", parent);

        bool open_ok = open_editor(state, parent, opts);
        std::printf("editor_open_ok=%d\n", open_ok ? 1 : 0);

        if (open_ok) {
            pump_messages_and_idle(state, opts.idle_ms);
            if (state.effect && state.effect->dispatcher) {
                state.effect->dispatcher(state.effect, effEditClose, 0, 0, nullptr, 0.0f);
            }
        }

        if (parent) {
            HWND top = GetAncestor(parent, GA_ROOT);
            if (top) DestroyWindow(top);
        }
    }

    unload_plugin(state);
    std::printf("done\n");
    return 0;
}
