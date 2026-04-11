//
// test-plugin: minimal VST2 plugin for CI testing.
// Identity effect: passes audio through unchanged.
// Reports known metadata for verification.
//
// Build as a shared library (.so/.dll/.dylib) exporting VSTPluginMain.
//

#include <vestige/vestige.h>
#include <cstring>
#include <cstdio>

static const int32_t TEST_UNIQUE_ID   = 0x4B505354; // "KPST"
static const int     TEST_NUM_INPUTS  = 2;
static const int     TEST_NUM_OUTPUTS = 2;
static const int     TEST_NUM_PARAMS  = 1;
static const char   *TEST_NAME        = "Keepsake Test Plugin";
static const char   *TEST_VENDOR      = "Infinite Loop Audio";
static const char   *TEST_PRODUCT     = "KeepsakeTest";
static const int32_t TEST_VERSION     = 0x010000; // 1.0.0

static float g_param_value = 0.5f; // "Mix" parameter

static intptr_t __cdecl plugin_dispatcher(
    AEffect *effect, int opcode, int index, intptr_t value, void *ptr, float opt)
{
    switch (opcode) {
    case effOpen:
        return 0;
    case effClose:
        return 0;
    case effGetEffectName:
        if (ptr) strncpy(static_cast<char *>(ptr), TEST_NAME, 63);
        return 1;
    case effGetVendorString:
        if (ptr) strncpy(static_cast<char *>(ptr), TEST_VENDOR, 63);
        return 1;
    case effGetProductString:
        if (ptr) strncpy(static_cast<char *>(ptr), TEST_PRODUCT, 63);
        return 1;
    case effGetVendorVersion:
        return TEST_VERSION;
    case effGetPlugCategory:
        return kPlugCategEffect;
    case effGetParamName:
        if (ptr && index == 0) strncpy(static_cast<char *>(ptr), "Mix", 7);
        return 0;
    case effGetParamLabel:
        if (ptr && index == 0) strncpy(static_cast<char *>(ptr), "%", 7);
        return 0;
    case effGetParamDisplay:
        if (ptr && index == 0)
            snprintf(static_cast<char *>(ptr), 8, "%.0f", g_param_value * 100);
        return 0;
    case effSetSampleRate:
        return 0;
    case effSetBlockSize:
        return 0;
    case effMainsChanged:
        return 0;
    case effGetVstVersion:
        return 2400;
    case effGetChunk:
        // Store param value as chunk
        if (ptr) {
            static float chunk_data;
            chunk_data = g_param_value;
            *static_cast<void **>(ptr) = &chunk_data;
            return static_cast<intptr_t>(sizeof(float));
        }
        return 0;
    case effSetChunk:
        if (ptr && value >= static_cast<intptr_t>(sizeof(float))) {
            g_param_value = *static_cast<float *>(ptr);
        }
        return 0;
    default:
        return 0;
    }
}

static void __cdecl plugin_process_replacing(
    AEffect * /*effect*/, float **inputs, float **outputs, int frames)
{
    // Identity: copy input to output (stereo pass-through)
    for (int ch = 0; ch < TEST_NUM_OUTPUTS; ch++) {
        if (inputs && outputs && inputs[ch] && outputs[ch]) {
            memcpy(outputs[ch], inputs[ch], static_cast<size_t>(frames) * sizeof(float));
        } else if (outputs && outputs[ch]) {
            memset(outputs[ch], 0, static_cast<size_t>(frames) * sizeof(float));
        }
    }
}

static void __cdecl plugin_set_parameter(AEffect *, int index, float value) {
    if (index == 0) g_param_value = value;
}

static float __cdecl plugin_get_parameter(AEffect *, int index) {
    if (index == 0) return g_param_value;
    return 0.0f;
}

// Static AEffect instance
static AEffect g_effect;

#ifdef _WIN32
#define EXPORT extern "C" __declspec(dllexport)
#else
#define EXPORT extern "C" __attribute__((visibility("default")))
#endif

EXPORT AEffect *VSTPluginMain(audioMasterCallback hostCallback) {
    if (!hostCallback) return nullptr;

    memset(&g_effect, 0, sizeof(g_effect));
    g_effect.magic = kEffectMagic;
    g_effect.dispatcher = plugin_dispatcher;
    g_effect.setParameter = plugin_set_parameter;
    g_effect.getParameter = plugin_get_parameter;
    g_effect.numPrograms = 1;
    g_effect.numParams = TEST_NUM_PARAMS;
    g_effect.numInputs = TEST_NUM_INPUTS;
    g_effect.numOutputs = TEST_NUM_OUTPUTS;
    g_effect.flags = effFlagsCanReplacing;
    g_effect.uniqueID = TEST_UNIQUE_ID;
    g_effect.version = TEST_VERSION;
    g_effect.processReplacing = plugin_process_replacing;

    return &g_effect;
}
