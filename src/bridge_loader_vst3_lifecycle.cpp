//
// Bridge loader — VST3 lifecycle/state helpers.
//

#include "bridge_loader_vst3_internal.h"

bool Vst3Loader::create_component() {
    if (factory->createInstance(classID, IComponent::iid, (void **)&component) != kResultOk
        || !component) {
        fprintf(stderr, "bridge/vst3: failed to create component\n");
        return false;
    }

    component->initialize(nullptr);

    if (component->queryInterface(IAudioProcessor::iid, (void **)&processor) != kResultOk
        || !processor) {
        fprintf(stderr, "bridge/vst3: component has no IAudioProcessor\n");
        return false;
    }

    return true;
}

void Vst3Loader::discover_buses() {
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
}

void Vst3Loader::discover_controller() {
    TUID controllerCID = {};
    bool has_separate_controller = false;

    if (component->getControllerClassId(controllerCID) == kResultOk) {
        bool non_zero = false;
        for (int j = 0; j < 16; j++) {
            if (controllerCID[j] != 0) {
                non_zero = true;
                break;
            }
        }
        if (non_zero) {
            if (factory->createInstance(controllerCID, IEditController::iid, (void **)&controller)
                    == kResultOk
                && controller) {
                controller->initialize(nullptr);
                has_separate_controller = true;
            }
        }
    }

    if (!controller) {
        if (component->queryInterface(IEditController::iid, (void **)&controller) == kResultOk
            && controller) {
        }
    }

    if (!controller) return;

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

bool Vst3Loader::load(const std::string &path) {
    if (!vst3_open_module(path, lib)) return false;

    InitModuleFunc initFunc = nullptr;
    GetFactoryFunc getFactory = nullptr;
    if (!vst3_lookup_module_funcs(lib, initFunc, exitModule, getFactory)) {
        fprintf(stderr, "bridge/vst3: no GetPluginFactory in '%s'\n", path.c_str());
        return false;
    }

    if (initFunc) initFunc();

    factory = getFactory();
    if (!factory) {
        fprintf(stderr, "bridge/vst3: GetPluginFactory returned null\n");
        return false;
    }

    PFactoryInfo fi;
    if (factory->getFactoryInfo(&fi) == kResultOk) {
        strncpy(vendorName, fi.vendor, sizeof(vendorName) - 1);
    }

    int32 classCount = factory->countClasses();
    bool found = false;

    for (int32 i = 0; i < classCount; i++) {
        PClassInfo ci;
        if (factory->getClassInfo(i, &ci) != kResultOk) continue;

        if (strcmp(ci.category, "Audio Module Class") != 0) continue;

        memcpy(classID, ci.cid, sizeof(TUID));
        strncpy(pluginName, ci.name, sizeof(pluginName) - 1);
        found = true;

        IPluginFactory2 *factory2 = nullptr;
        if (factory->queryInterface(IPluginFactory2::iid, (void **)&factory2) == kResultOk) {
            PClassInfo2 ci2;
            if (factory2->getClassInfo2(i, &ci2) == kResultOk) {
                if (ci2.vendor[0]) strncpy(vendorName, ci2.vendor, sizeof(vendorName) - 1);
                strncpy(versionStr, ci2.version, sizeof(versionStr) - 1);
                strncpy(categoryStr, ci2.subCategories, sizeof(categoryStr) - 1);
            }
            factory2->release();
        }
        break;
    }

    if (!found) {
        fprintf(stderr, "bridge/vst3: no audio processor found\n");
        return false;
    }

    if (!create_component()) return false;
    discover_buses();
    discover_controller();

    fprintf(stderr, "bridge/vst3: loaded '%s' — in=%d(%dch) out=%d(%dch) params=%d\n", pluginName,
            n_inputs, n_input_channels, n_outputs, n_output_channels, n_params);
    return true;
}

std::vector<uint8_t> Vst3Loader::get_chunk() {
    if (!component) return {};
    MemoryStream *stream = new MemoryStream();
    component->getState(stream);
    auto result = stream->getData();
    stream->release();
    return result;
}

void Vst3Loader::set_chunk(const uint8_t *data, size_t size) {
    if (!component) return;
    MemoryStream *stream = new MemoryStream();
    stream->setData(data, size);
    component->setState(stream);
    stream->release();
}

void Vst3Loader::close() {
    if (is_active) deactivate();
    if (controller) {
        controller->terminate();
        controller->release();
        controller = nullptr;
    }
    if (processor) {
        processor->release();
        processor = nullptr;
    }
    if (component) {
        component->terminate();
        component->release();
        component = nullptr;
    }
    if (factory) {
        factory->release();
        factory = nullptr;
    }
    if (exitModule) exitModule();
    if (lib) {
        vst3_close_module(lib);
        lib = nullptr;
    }
}
