//
// Bridge loader — VST3 runtime methods.
//

#include "bridge_loader_vst3_internal.h"

void Vst3Loader::get_info(IpcPluginInfo &info, std::vector<uint8_t> &extra) {
    memcpy(&info.unique_id, classID, sizeof(int32_t));
    info.num_inputs = n_input_channels;
    info.num_outputs = n_output_channels;
    info.num_params = n_params;
    info.flags = 0;
    info.category = 0;
    info.vendor_version = 0;

    bool is_instrument = (strstr(categoryStr, "Instrument") != nullptr);
    bool is_fx = (strstr(categoryStr, "Fx") != nullptr);
    if (is_instrument) {
        info.category = 2;
        info.flags = 0x100;
    } else if (is_fx) {
        info.category = 1;
    }

    auto append = [&](const char *s) {
        size_t len = strlen(s);
        extra.insert(extra.end(), s, s + len + 1);
    };
    append(pluginName);
    append(vendorName);
    append("");
}

void Vst3Loader::activate(double sample_rate, uint32_t mf) {
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

void Vst3Loader::deactivate() {
    if (is_active && component) {
        processor->setProcessing(false);
        component->setActive(false);
    }
    is_active = false;
}

void Vst3Loader::process(
    float **inputs, int num_in, float **outputs, int num_out, uint32_t num_frames) {
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

void Vst3Loader::set_param(uint32_t index, float value) {
    if (!controller) return;
    controller->setParamNormalized(static_cast<ParamID>(index), static_cast<ParamValue>(value));
}

bool Vst3Loader::get_param_info(uint32_t index, IpcParamInfoResponse &resp) {
    if (!controller || static_cast<int32>(index) >= n_params) return false;

    ParameterInfo pi;
    if (controller->getParameterInfo(static_cast<int32>(index), pi) != kResultOk) return false;

    resp.index = index;
    resp.current_value = static_cast<float>(controller->getParamNormalized(pi.id));

    for (int i = 0; i < 63 && pi.title[i]; i++) {
        resp.name[i] = static_cast<char>(pi.title[i] & 0x7F);
    }
    resp.name[63] = '\0';

    for (int i = 0; i < 15 && pi.units[i]; i++) {
        resp.label[i] = static_cast<char>(pi.units[i] & 0x7F);
    }
    resp.label[15] = '\0';

    return true;
}

void Vst3Loader::send_midi(int32_t delta, const uint8_t data[4]) {
    midi_queue.push_back({delta, {data[0], data[1], data[2], data[3]}});
}

bool Vst3Loader::has_editor() { return false; }

bool Vst3Loader::open_editor(void *) { return false; }

void Vst3Loader::close_editor() {}

void Vst3Loader::editor_idle() {}

bool Vst3Loader::get_editor_rect(int &, int &) { return false; }

BridgeLoader *create_vst3_loader() { return new Vst3Loader(); }
