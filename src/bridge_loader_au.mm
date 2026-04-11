//
// Bridge loader — AU v2 implementation via AudioToolbox.
// macOS only.
//

#include "bridge_loader.h"

#ifdef __APPLE__
#import <AudioToolbox/AudioToolbox.h>
#import <CoreFoundation/CoreFoundation.h>
#include <cstdio>
#include <cstring>

class AULoader : public BridgeLoader {
    AudioComponentInstance unit = nullptr;
    AudioComponent component = nullptr;
    int32_t num_inputs = 0;
    int32_t num_outputs = 0;
    int32_t num_params_count = 0;
    bool is_active = false;
    uint32_t block_size = 512;

    // AU component identifiers
    OSType comp_type = 0;
    OSType comp_subtype = 0;
    OSType comp_manufacturer = 0;

    // Cached metadata
    char name_buf[256] = {};
    char vendor_buf[256] = {};

    static void fourcc_to_str(OSType cc, char *out) {
        out[0] = static_cast<char>((cc >> 24) & 0xFF);
        out[1] = static_cast<char>((cc >> 16) & 0xFF);
        out[2] = static_cast<char>((cc >> 8) & 0xFF);
        out[3] = static_cast<char>(cc & 0xFF);
        out[4] = '\0';
    }

public:
    bool load(const std::string &path) override {
        // Path encodes the AU component description:
        // "type:subtype:manufacturer" as 4-char codes
        // e.g., "aufx:RvRb:Appl"
        if (path.size() < 14 || path[4] != ':' || path[9] != ':') {
            fprintf(stderr, "bridge/au: invalid AU path format '%s'\n",
                    path.c_str());
            return false;
        }

        auto parse_fourcc = [](const char *s) -> OSType {
            return (static_cast<OSType>(s[0]) << 24) |
                   (static_cast<OSType>(s[1]) << 16) |
                   (static_cast<OSType>(s[2]) << 8) |
                   static_cast<OSType>(s[3]);
        };

        comp_type = parse_fourcc(path.c_str());
        comp_subtype = parse_fourcc(path.c_str() + 5);
        comp_manufacturer = parse_fourcc(path.c_str() + 10);

        AudioComponentDescription desc = {};
        desc.componentType = comp_type;
        desc.componentSubType = comp_subtype;
        desc.componentManufacturer = comp_manufacturer;

        component = AudioComponentFindNext(nullptr, &desc);
        if (!component) {
            fprintf(stderr, "bridge/au: component not found\n");
            return false;
        }

        OSStatus err = AudioComponentInstanceNew(component, &unit);
        if (err != noErr || !unit) {
            fprintf(stderr, "bridge/au: failed to create instance (%d)\n", (int)err);
            return false;
        }

        // Get component name
        CFStringRef cf_name = nullptr;
        AudioComponentCopyName(component, &cf_name);
        if (cf_name) {
            CFStringGetCString(cf_name, name_buf, sizeof(name_buf),
                                kCFStringEncodingUTF8);
            CFRelease(cf_name);

            // Name is usually "Manufacturer: Plugin Name" — split on ": "
            char *sep = strstr(name_buf, ": ");
            if (sep) {
                *sep = '\0';
                strncpy(vendor_buf, name_buf, sizeof(vendor_buf) - 1);
                memmove(name_buf, sep + 2, strlen(sep + 2) + 1);
            }
        }

        // Query channel configuration
        UInt32 prop_size = 0;
        AudioUnitGetPropertyInfo(unit, kAudioUnitProperty_SupportedNumChannels,
                                  kAudioUnitScope_Global, 0, &prop_size, nullptr);
        // Default to stereo if can't query
        num_inputs = (comp_type == kAudioUnitType_MusicDevice) ? 0 : 2;
        num_outputs = 2;

        // Count parameters
        AudioUnitParameterID *param_ids = nullptr;
        UInt32 param_size = 0;
        AudioUnitGetPropertyInfo(unit, kAudioUnitProperty_ParameterList,
                                  kAudioUnitScope_Global, 0, &param_size, nullptr);
        num_params_count = static_cast<int32_t>(param_size / sizeof(AudioUnitParameterID));

        fprintf(stderr, "bridge/au: loaded '%s' — vendor='%s' in=%d out=%d params=%d\n",
                name_buf, vendor_buf, num_inputs, num_outputs, num_params_count);
        return true;
    }

    void get_info(IpcPluginInfo &info,
                   std::vector<uint8_t> &extra) override {
        // Encode unique ID from type+subtype+manufacturer
        info.unique_id = static_cast<int32_t>(comp_subtype);
        info.num_inputs = num_inputs;
        info.num_outputs = num_outputs;
        info.num_params = num_params_count;
        info.flags = (comp_type == kAudioUnitType_MusicDevice) ? 0x100 : 0; // synth flag
        info.category = (comp_type == kAudioUnitType_MusicDevice) ? 2 : 1;
        info.vendor_version = 0;

        auto append_str = [&](const char *s) {
            size_t len = strlen(s);
            extra.insert(extra.end(), s, s + len + 1);
        };
        append_str(name_buf);
        append_str(vendor_buf);
        append_str(""); // product
    }

    void activate(double sample_rate, uint32_t max_frames) override {
        block_size = max_frames;

        AudioStreamBasicDescription fmt = {};
        fmt.mSampleRate = sample_rate;
        fmt.mFormatID = kAudioFormatLinearPCM;
        fmt.mFormatFlags = kAudioFormatFlagIsFloat |
                            kAudioFormatFlagIsNonInterleaved |
                            kAudioFormatFlagIsPacked;
        fmt.mBitsPerChannel = 32;
        fmt.mChannelsPerFrame = static_cast<UInt32>(num_outputs);
        fmt.mFramesPerPacket = 1;
        fmt.mBytesPerFrame = sizeof(Float32);
        fmt.mBytesPerPacket = sizeof(Float32);

        AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat,
                              kAudioUnitScope_Output, 0,
                              &fmt, sizeof(fmt));

        if (num_inputs > 0) {
            fmt.mChannelsPerFrame = static_cast<UInt32>(num_inputs);
            AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat,
                                  kAudioUnitScope_Input, 0,
                                  &fmt, sizeof(fmt));
        }

        UInt32 bs = max_frames;
        AudioUnitSetProperty(unit, kAudioUnitProperty_MaximumFramesPerSlice,
                              kAudioUnitScope_Global, 0, &bs, sizeof(bs));

        AudioUnitInitialize(unit);
        is_active = true;
    }

    void deactivate() override {
        if (is_active && unit)
            AudioUnitUninitialize(unit);
        is_active = false;
    }

    void process(float **inputs, int n_in, float **outputs, int n_out,
                  uint32_t num_frames) override {
        AudioBufferList out_bufs;
        out_bufs.mNumberBuffers = static_cast<UInt32>(n_out);
        for (int i = 0; i < n_out; i++) {
            out_bufs.mBuffers[i].mNumberChannels = 1;
            out_bufs.mBuffers[i].mDataByteSize = num_frames * sizeof(Float32);
            out_bufs.mBuffers[i].mData = outputs[i];
        }

        AudioTimeStamp ts = {};
        ts.mFlags = kAudioTimeStampSampleTimeValid;
        ts.mSampleTime = 0;

        // For effects, set up input callback or provide input buffers
        // For instruments, just render
        AudioUnitRenderActionFlags flags = 0;
        AudioUnitRender(unit, &flags, &ts, 0, num_frames, &out_bufs);
    }

    void set_param(uint32_t index, float value) override {
        if (unit)
            AudioUnitSetParameter(unit, static_cast<AudioUnitParameterID>(index),
                                   kAudioUnitScope_Global, 0, value, 0);
    }

    bool get_param_info(uint32_t index, IpcParamInfoResponse &resp) override {
        resp.index = index;
        resp.current_value = 0;

        AudioUnitParameterInfo au_info = {};
        UInt32 info_size = sizeof(au_info);
        OSStatus err = AudioUnitGetProperty(
            unit, kAudioUnitProperty_ParameterInfo,
            kAudioUnitScope_Global, static_cast<AudioUnitParameterID>(index),
            &au_info, &info_size);

        if (err == noErr) {
            if (au_info.flags & kAudioUnitParameterFlag_HasCFNameString &&
                au_info.cfNameString) {
                CFStringGetCString(au_info.cfNameString, resp.name,
                                    sizeof(resp.name), kCFStringEncodingUTF8);
                if (!(au_info.flags & kAudioUnitParameterFlag_CFNameRelease))
                    {} // don't release
                else
                    CFRelease(au_info.cfNameString);
            } else {
                strncpy(resp.name, au_info.name, sizeof(resp.name) - 1);
            }
            resp.current_value = au_info.defaultValue;
        }
        return err == noErr;
    }

    void send_midi(int32_t /*delta*/, const uint8_t data[4]) override {
        if (unit)
            MusicDeviceMIDIEvent(unit, data[0], data[1], data[2], 0);
    }

    std::vector<uint8_t> get_chunk() override {
        // AU uses ClassInfo/AUPreset format — simplified here
        CFPropertyListRef preset = nullptr;
        UInt32 size = sizeof(preset);
        OSStatus err = AudioUnitGetProperty(
            unit, kAudioUnitProperty_ClassInfo,
            kAudioUnitScope_Global, 0, &preset, &size);
        if (err != noErr || !preset) return {};

        CFDataRef data = CFPropertyListCreateData(
            kCFAllocatorDefault, preset, kCFPropertyListBinaryFormat_v1_0,
            0, nullptr);
        CFRelease(preset);
        if (!data) return {};

        const uint8_t *bytes = CFDataGetBytePtr(data);
        size_t len = static_cast<size_t>(CFDataGetLength(data));
        std::vector<uint8_t> result(bytes, bytes + len);
        CFRelease(data);
        return result;
    }

    void set_chunk(const uint8_t *data, size_t size) override {
        CFDataRef cf_data = CFDataCreate(kCFAllocatorDefault, data,
                                          static_cast<CFIndex>(size));
        if (!cf_data) return;

        CFPropertyListRef preset = CFPropertyListCreateWithData(
            kCFAllocatorDefault, cf_data, kCFPropertyListImmutable,
            nullptr, nullptr);
        CFRelease(cf_data);
        if (!preset) return;

        AudioUnitSetProperty(unit, kAudioUnitProperty_ClassInfo,
                              kAudioUnitScope_Global, 0,
                              &preset, sizeof(preset));
        CFRelease(preset);
    }

    bool has_editor() override { return false; } // AU GUI deferred
    bool open_editor(void *) override { return false; }
    void close_editor() override {}
    void editor_idle() override {}
    bool get_editor_rect(int &, int &) override { return false; }

    void close() override {
        if (unit) {
            if (is_active) AudioUnitUninitialize(unit);
            AudioComponentInstanceDispose(unit);
            unit = nullptr;
        }
    }
};

BridgeLoader *create_au_loader() { return new AULoader(); }

#else
// Non-macOS: AU not available
BridgeLoader *create_au_loader() { return nullptr; }
#endif
