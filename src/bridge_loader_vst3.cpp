//
// Bridge loader — VST3 implementation.
// Uses the VST3 pluginterfaces headers directly (no full SDK dependency).
// The VST3 SDK is GPLv3; this runs in the bridge subprocess so the license
// boundary is at the process/IPC edge.
//

#include "bridge_loader.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

// VST3 pluginterfaces (from steinbergmedia/vst3_pluginterfaces)
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

#ifndef _WIN32
#include <dlfcn.h>
#endif

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

using namespace Steinberg;
using namespace Steinberg::Vst;

// IID definitions — normally provided by the SDK's base library,
// but we're linking against pluginterfaces headers only.
#if !defined(INIT_CLASS_IID)
namespace Steinberg {
    DEF_CLASS_IID(FUnknown)
    DEF_CLASS_IID(IBStream)
    DEF_CLASS_IID(IPluginFactory)
    DEF_CLASS_IID(IPluginFactory2)
    DEF_CLASS_IID(IPluginBase)
    namespace Vst {
        DEF_CLASS_IID(IComponent)
        DEF_CLASS_IID(IAudioProcessor)
        DEF_CLASS_IID(IEditController)
    }
}
#endif

// --- Minimal IBStream implementation for state save/load ---

class MemoryStream : public IBStream {
    std::vector<uint8_t> data;
    int64 pos = 0;
    uint32 refCount = 1;

public:
    tresult PLUGIN_API queryInterface(const TUID, void **) override { return kNoInterface; }
    uint32 PLUGIN_API addRef() override { return ++refCount; }
    uint32 PLUGIN_API release() override {
        if (--refCount == 0) { delete this; return 0; }
        return refCount;
    }

    tresult PLUGIN_API read(void *buffer, int32 numBytes, int32 *numBytesRead) override {
        int32 avail = static_cast<int32>(data.size() - static_cast<size_t>(pos));
        int32 toRead = (numBytes < avail) ? numBytes : avail;
        if (toRead > 0) {
            memcpy(buffer, data.data() + pos, static_cast<size_t>(toRead));
            pos += toRead;
        }
        if (numBytesRead) *numBytesRead = toRead;
        return kResultOk;
    }

    tresult PLUGIN_API write(void *buffer, int32 numBytes, int32 *numBytesWritten) override {
        size_t end = static_cast<size_t>(pos) + static_cast<size_t>(numBytes);
        if (end > data.size()) data.resize(end);
        memcpy(data.data() + pos, buffer, static_cast<size_t>(numBytes));
        pos += numBytes;
        if (numBytesWritten) *numBytesWritten = numBytes;
        return kResultOk;
    }

    tresult PLUGIN_API seek(int64 p, int32 mode, int64 *result) override {
        switch (mode) {
        case kIBSeekSet: pos = p; break;
        case kIBSeekCur: pos += p; break;
        case kIBSeekEnd: pos = static_cast<int64>(data.size()) + p; break;
        }
        if (pos < 0) pos = 0;
        if (result) *result = pos;
        return kResultOk;
    }

    tresult PLUGIN_API tell(int64 *p) override {
        if (p) *p = pos;
        return kResultOk;
    }

    const std::vector<uint8_t> &getData() const { return data; }
    void setData(const uint8_t *d, size_t sz) { data.assign(d, d + sz); pos = 0; }
    void reset() { pos = 0; }
};

// --- Module loading ---

typedef IPluginFactory *(PLUGIN_API *GetFactoryFunc)();
typedef bool (*InitModuleFunc)();
typedef bool (*ExitModuleFunc)();

class Vst3Loader : public BridgeLoader {
    void *lib = nullptr;
    IPluginFactory *factory = nullptr;
    IComponent *component = nullptr;
    IAudioProcessor *processor = nullptr;
    IEditController *controller = nullptr;
    ExitModuleFunc exitModule = nullptr;

    int32_t n_inputs = 0;
    int32_t n_outputs = 0;
    int32_t n_params = 0;
    int32_t n_input_channels = 0;
    int32_t n_output_channels = 0;
    bool is_active = false;
    uint32_t max_block = 512;

    // Plugin identity
    TUID classID = {};
    char pluginName[128] = {};
    char vendorName[128] = {};
    char versionStr[64] = {};
    char categoryStr[64] = {};

    // MIDI event queue
    struct MidiNote { int32_t delta; uint8_t data[4]; };
    std::vector<MidiNote> midi_queue;

    std::string resolve_vst3_binary(const std::string &path) {
#ifdef __APPLE__
        // .vst3 bundle: Contents/MacOS/<name>
        CFURLRef url = CFURLCreateFromFileSystemRepresentation(
            kCFAllocatorDefault,
            reinterpret_cast<const UInt8 *>(path.c_str()),
            static_cast<CFIndex>(path.size()), true);
        if (!url) return path;
        CFBundleRef bundle = CFBundleCreate(kCFAllocatorDefault, url);
        CFRelease(url);
        if (!bundle) return path;
        CFURLRef exec = CFBundleCopyExecutableURL(bundle);
        CFRelease(bundle);
        if (!exec) return path;
        char buf[4096];
        Boolean ok = CFURLGetFileSystemRepresentation(
            exec, true, reinterpret_cast<UInt8 *>(buf), sizeof(buf));
        CFRelease(exec);
        return ok ? std::string(buf) : path;
#elif defined(_WIN32)
        // .vst3 on Windows: <path>/Contents/x86_64-win/<name>.vst3
        // Or just a renamed DLL
        return path;
#else
        // Linux: .vst3 bundle: Contents/x86_64-linux/<name>.so
        // Try the bundle structure first
        std::string stem = path;
        size_t last_slash = stem.find_last_of('/');
        size_t last_dot = stem.find_last_of('.');
        if (last_slash != std::string::npos && last_dot != std::string::npos)
            stem = stem.substr(last_slash + 1, last_dot - last_slash - 1);
        std::string bundle_path = path + "/Contents/x86_64-linux/" + stem + ".so";
        FILE *f = fopen(bundle_path.c_str(), "rb");
        if (f) { fclose(f); return bundle_path; }
        return path;
#endif
    }

public:
    bool load(const std::string &path) override {
        std::string bin_path = resolve_vst3_binary(path);

#ifndef _WIN32
        lib = dlopen(bin_path.c_str(), RTLD_LAZY | RTLD_LOCAL);
#else
        lib = (void *)LoadLibraryA(bin_path.c_str());
#endif
        if (!lib) {
            fprintf(stderr, "bridge/vst3: failed to load '%s'\n", bin_path.c_str());
            return false;
        }

        // Initialize module
#ifndef _WIN32
        auto initFunc = reinterpret_cast<InitModuleFunc>(dlsym(lib, "ModuleEntry"));
        exitModule = reinterpret_cast<ExitModuleFunc>(dlsym(lib, "ModuleExit"));
        auto getFactory = reinterpret_cast<GetFactoryFunc>(dlsym(lib, "GetPluginFactory"));
#else
        auto initFunc = reinterpret_cast<InitModuleFunc>(
            GetProcAddress((HMODULE)lib, "InitDll"));
        exitModule = reinterpret_cast<ExitModuleFunc>(
            GetProcAddress((HMODULE)lib, "ExitDll"));
        auto getFactory = reinterpret_cast<GetFactoryFunc>(
            GetProcAddress((HMODULE)lib, "GetPluginFactory"));
#endif

        if (!getFactory) {
            fprintf(stderr, "bridge/vst3: no GetPluginFactory in '%s'\n", bin_path.c_str());
            return false;
        }

        if (initFunc) initFunc();

        factory = getFactory();
        if (!factory) {
            fprintf(stderr, "bridge/vst3: GetPluginFactory returned null\n");
            return false;
        }

        // Get factory info
        PFactoryInfo fi;
        if (factory->getFactoryInfo(&fi) == kResultOk) {
            strncpy(vendorName, fi.vendor, sizeof(vendorName) - 1);
        }

        // Find the first audio processor class
        int32 classCount = factory->countClasses();
        bool found = false;

        for (int32 i = 0; i < classCount; i++) {
            PClassInfo ci;
            if (factory->getClassInfo(i, &ci) != kResultOk) continue;

            if (strcmp(ci.category, "Audio Module Class") == 0) {
                memcpy(classID, ci.cid, sizeof(TUID));
                strncpy(pluginName, ci.name, sizeof(pluginName) - 1);
                found = true;

                // Try PClassInfo2 for more metadata
                IPluginFactory2 *factory2 = nullptr;
                if (factory->queryInterface(IPluginFactory2::iid, (void **)&factory2) == kResultOk) {
                    PClassInfo2 ci2;
                    if (factory2->getClassInfo2(i, &ci2) == kResultOk) {
                        if (ci2.vendor[0])
                            strncpy(vendorName, ci2.vendor, sizeof(vendorName) - 1);
                        strncpy(versionStr, ci2.version, sizeof(versionStr) - 1);
                        strncpy(categoryStr, ci2.subCategories, sizeof(categoryStr) - 1);
                    }
                    factory2->release();
                }
                break;
            }
        }

        if (!found) {
            fprintf(stderr, "bridge/vst3: no audio processor found\n");
            return false;
        }

        // Create component
        if (factory->createInstance(classID, IComponent::iid, (void **)&component) != kResultOk
            || !component) {
            fprintf(stderr, "bridge/vst3: failed to create component\n");
            return false;
        }

        component->initialize(nullptr);

        // Get IAudioProcessor
        if (component->queryInterface(IAudioProcessor::iid, (void **)&processor) != kResultOk
            || !processor) {
            fprintf(stderr, "bridge/vst3: component has no IAudioProcessor\n");
            return false;
        }

        // Get bus info
        n_inputs = component->getBusCount(kAudio, kInput);
        n_outputs = component->getBusCount(kAudio, kOutput);

        if (n_inputs > 0) {
            BusInfo bi;
            component->getBusInfo(kAudio, kInput, 0, bi);
            n_input_channels = bi.channelCount;
            component->activateBus(kAudio, kInput, 0, true);
        }
        if (n_outputs > 0) {
            BusInfo bi;
            component->getBusInfo(kAudio, kOutput, 0, bi);
            n_output_channels = bi.channelCount;
            component->activateBus(kAudio, kOutput, 0, true);
        }

        // Try to get edit controller for parameters.
        // VST3 has a split component/controller model. The controller may be:
        // 1. A separate class (getControllerClassId returns a valid CID)
        // 2. The same object as the component (queryInterface succeeds)

        TUID controllerCID = {};
        bool has_separate_controller = false;
        if (component->getControllerClassId(controllerCID) == kResultOk) {
            // Check if the CID is non-zero (some plugins return OK but zeros)
            bool non_zero = false;
            for (int j = 0; j < 16; j++) {
                if (controllerCID[j] != 0) { non_zero = true; break; }
            }
            if (non_zero) {
                if (factory->createInstance(controllerCID, IEditController::iid,
                                            (void **)&controller) == kResultOk
                    && controller) {
                    controller->initialize(nullptr);
                    has_separate_controller = true;
                }
            }
        }

        // Fallback: component IS the controller
        if (!controller) {
            if (component->queryInterface(IEditController::iid,
                                           (void **)&controller) == kResultOk
                && controller) {
                // Don't initialize again — already initialized as component
            }
        }

        if (controller) {
            // Sync component state to controller
            if (has_separate_controller) {
                MemoryStream *stream = new MemoryStream();
                if (component->getState(stream) == kResultOk) {
                    stream->reset();
                    controller->setComponentState(stream);
                }
                stream->release();
            }
            n_params = controller->getParameterCount();
        }

        fprintf(stderr, "bridge/vst3: loaded '%s' — in=%d(%dch) out=%d(%dch) params=%d\n",
                pluginName, n_inputs, n_input_channels,
                n_outputs, n_output_channels, n_params);
        return true;
    }

    void get_info(IpcPluginInfo &info, std::vector<uint8_t> &extra) override {
        // Use first 4 bytes of TUID as unique ID
        memcpy(&info.unique_id, classID, sizeof(int32_t));
        info.num_inputs = n_input_channels;
        info.num_outputs = n_output_channels;
        info.num_params = n_params;
        info.flags = 0;
        info.category = 0;
        info.vendor_version = 0;

        // Determine category from subcategories string
        bool is_instrument = (strstr(categoryStr, "Instrument") != nullptr);
        bool is_fx = (strstr(categoryStr, "Fx") != nullptr);
        if (is_instrument) { info.category = 2; info.flags = 0x100; }
        else if (is_fx) { info.category = 1; }

        auto append = [&](const char *s) {
            size_t len = strlen(s);
            extra.insert(extra.end(), s, s + len + 1);
        };
        append(pluginName);
        append(vendorName);
        append(""); // product
    }

    void activate(double sample_rate, uint32_t mf) override {
        max_block = mf;
        ProcessSetup setup;
        setup.processMode = kRealtime;
        setup.symbolicSampleSize = kSample32;
        setup.maxSamplesPerBlock = static_cast<int32>(mf);
        setup.sampleRate = sample_rate;
        processor->setupProcessing(setup);
        component->setActive(true);
        is_active = true;
    }

    void deactivate() override {
        if (is_active && component) {
            processor->setProcessing(false);
            component->setActive(false);
        }
        is_active = false;
    }

    void process(float **inputs, int num_in, float **outputs, int num_out,
                  uint32_t num_frames) override {
        ProcessData data = {};
        data.processMode = kRealtime;
        data.symbolicSampleSize = kSample32;
        data.numSamples = static_cast<int32>(num_frames);

        AudioBusBuffers in_bus = {};
        AudioBusBuffers out_bus = {};

        if (num_in > 0 && inputs) {
            in_bus.numChannels = static_cast<int32>(num_in);
            in_bus.channelBuffers32 = inputs;
            data.numInputs = 1;
            data.inputs = &in_bus;
        }
        if (num_out > 0 && outputs) {
            out_bus.numChannels = static_cast<int32>(num_out);
            out_bus.channelBuffers32 = outputs;
            data.numOutputs = 1;
            data.outputs = &out_bus;
        }

        processor->process(data);
    }

    void set_param(uint32_t index, float value) override {
        if (controller)
            controller->setParamNormalized(static_cast<ParamID>(index),
                                            static_cast<ParamValue>(value));
    }

    bool get_param_info(uint32_t index, IpcParamInfoResponse &resp) override {
        if (!controller || static_cast<int32>(index) >= n_params) return false;
        ParameterInfo pi;
        if (controller->getParameterInfo(static_cast<int32>(index), pi) != kResultOk)
            return false;

        resp.index = index;
        resp.current_value = static_cast<float>(
            controller->getParamNormalized(pi.id));

        // Convert UTF-16 title to ASCII
        for (int i = 0; i < 63 && pi.title[i]; i++)
            resp.name[i] = static_cast<char>(pi.title[i] & 0x7F);
        resp.name[63] = '\0';

        for (int i = 0; i < 15 && pi.units[i]; i++)
            resp.label[i] = static_cast<char>(pi.units[i] & 0x7F);
        resp.label[15] = '\0';

        return true;
    }

    void send_midi(int32_t delta, const uint8_t data[4]) override {
        midi_queue.push_back({delta, {data[0], data[1], data[2], data[3]}});
    }

    std::vector<uint8_t> get_chunk() override {
        if (!component) return {};
        MemoryStream *stream = new MemoryStream();
        component->getState(stream);
        auto result = stream->getData();
        stream->release();
        return result;
    }

    void set_chunk(const uint8_t *data, size_t size) override {
        if (!component) return;
        MemoryStream *stream = new MemoryStream();
        stream->setData(data, size);
        component->setState(stream);
        stream->release();
    }

    bool has_editor() override { return false; } // VST3 GUI deferred
    bool open_editor(void *) override { return false; }
    void close_editor() override {}
    void editor_idle() override {}
    bool get_editor_rect(int &, int &) override { return false; }

    void close() override {
        if (is_active) deactivate();
        if (controller) { controller->terminate(); controller->release(); controller = nullptr; }
        if (processor) { processor->release(); processor = nullptr; }
        if (component) { component->terminate(); component->release(); component = nullptr; }
        if (factory) { factory->release(); factory = nullptr; }
        if (exitModule) exitModule();
#ifndef _WIN32
        if (lib) { dlclose(lib); lib = nullptr; }
#endif
    }
};

BridgeLoader *create_vst3_loader() { return new Vst3Loader(); }
