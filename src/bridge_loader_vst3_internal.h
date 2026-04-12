#pragma once

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
#else
#include <windows.h>
#endif

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

using namespace Steinberg;
using namespace Steinberg::Vst;

class MemoryStream : public IBStream {
    std::vector<uint8_t> data;
    int64 pos = 0;
    uint32 refCount = 1;

public:
    tresult PLUGIN_API queryInterface(const TUID, void **) override;
    uint32 PLUGIN_API addRef() override;
    uint32 PLUGIN_API release() override;
    tresult PLUGIN_API read(void *buffer, int32 numBytes, int32 *numBytesRead) override;
    tresult PLUGIN_API write(void *buffer, int32 numBytes, int32 *numBytesWritten) override;
    tresult PLUGIN_API seek(int64 p, int32 mode, int64 *result) override;
    tresult PLUGIN_API tell(int64 *p) override;

    const std::vector<uint8_t> &getData() const;
    void setData(const uint8_t *d, size_t sz);
    void reset();
};

typedef IPluginFactory *(PLUGIN_API *GetFactoryFunc)();
typedef bool (*InitModuleFunc)();
typedef bool (*ExitModuleFunc)();

bool vst3_resolve_binary_path(const std::string &path, std::string &resolved_path);
bool vst3_open_module(const std::string &path, void *&lib);
void vst3_close_module(void *lib);
bool vst3_lookup_module_funcs(
    void *lib, InitModuleFunc &init_func, ExitModuleFunc &exit_func, GetFactoryFunc &factory_func);

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

    TUID classID = {};
    char pluginName[128] = {};
    char vendorName[128] = {};
    char versionStr[64] = {};
    char categoryStr[64] = {};

    struct MidiNote {
        int32_t delta;
        uint8_t data[4];
    };
    std::vector<MidiNote> midi_queue;

public:
    bool load(const std::string &path) override;
    void get_info(IpcPluginInfo &info, std::vector<uint8_t> &extra) override;
    void activate(double sample_rate, uint32_t mf) override;
    void deactivate() override;
    void process(float **inputs, int num_in, float **outputs, int num_out, uint32_t num_frames)
        override;
    void set_param(uint32_t index, float value) override;
    bool get_param_info(uint32_t index, IpcParamInfoResponse &resp) override;
    void send_midi(int32_t delta, const uint8_t data[4]) override;
    std::vector<uint8_t> get_chunk() override;
    void set_chunk(const uint8_t *data, size_t size) override;
    bool has_editor() override;
    bool open_editor(void *parent_view) override;
    void close_editor() override;
    void editor_idle() override;
    bool get_editor_rect(int &width, int &height) override;
    void close() override;

private:
    bool create_component();
    void discover_buses();
    void discover_controller();
};
