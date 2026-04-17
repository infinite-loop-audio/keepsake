#include <vestige/vestige.h>

#import <Cocoa/Cocoa.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

static const int32_t DEMO_UNIQUE_ID = 0x4B504755; // "KPGU"
static const int DEMO_NUM_INPUTS = 2;
static const int DEMO_NUM_OUTPUTS = 2;
static const int DEMO_NUM_PARAMS = 1;
static const char *DEMO_NAME = "Keepsake Demo GUI";
static const char *DEMO_VENDOR = "Infinite Loop Audio";
static const char *DEMO_PRODUCT = "KeepsakeDemoGui";
static const int32_t DEMO_VERSION = 0x010000;
static const int DEMO_EDITOR_WIDTH = 320;
static const int DEMO_EDITOR_HEIGHT = 160;

struct ERect {
    int16_t top;
    int16_t left;
    int16_t bottom;
    int16_t right;
};

static float g_gain = 0.6f;
static double g_phase = 0.0;
static double g_sample_rate = 44100.0;
static audioMasterCallback g_host_callback = nullptr;
static ERect g_editor_rect = {0, 0, DEMO_EDITOR_HEIGHT, DEMO_EDITOR_WIDTH};
static float g_chunk_gain = 0.6f;

static void __cdecl plugin_set_parameter(AEffect *, int index, float value);

@interface KeepsakeDemoEditorView : NSView
@end

@implementation KeepsakeDemoEditorView

- (BOOL)isFlipped {
    return YES;
}

- (NSRect)sliderRect {
    return NSMakeRect(24.0, 88.0, 272.0, 22.0);
}

- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    [[NSColor colorWithCalibratedRed:0.93 green:0.95 blue:0.98 alpha:1.0] setFill];
    NSRectFill(self.bounds);

    [[NSColor colorWithCalibratedRed:0.10 green:0.14 blue:0.22 alpha:1.0] set];
    NSDictionary *title_attrs = @{
        NSFontAttributeName: [NSFont boldSystemFontOfSize:20.0],
        NSForegroundColorAttributeName: NSColor.blackColor,
    };
    [@"Keepsake Demo GUI" drawAtPoint:NSMakePoint(24.0, 24.0) withAttributes:title_attrs];

    NSDictionary *body_attrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:13.0],
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedWhite:0.2 alpha:1.0],
    };
    NSString *body = [NSString stringWithFormat:@"Gain %.0f%%  Click or drag the bar.", g_gain * 100.0f];
    [body drawAtPoint:NSMakePoint(24.0, 54.0) withAttributes:body_attrs];

    NSRect slider = [self sliderRect];
    [[NSColor colorWithCalibratedWhite:0.83 alpha:1.0] setFill];
    [[NSBezierPath bezierPathWithRoundedRect:slider xRadius:11.0 yRadius:11.0] fill];

    NSRect fill = slider;
    fill.size.width = std::max<CGFloat>(18.0, slider.size.width * g_gain);
    [[NSColor colorWithCalibratedRed:0.16 green:0.48 blue:0.89 alpha:1.0] setFill];
    [[NSBezierPath bezierPathWithRoundedRect:fill xRadius:11.0 yRadius:11.0] fill];

    CGFloat knob_x = slider.origin.x + slider.size.width * g_gain;
    NSRect knob = NSMakeRect(knob_x - 8.0, slider.origin.y - 5.0, 16.0, slider.size.height + 10.0);
    [[NSColor colorWithCalibratedRed:0.04 green:0.12 blue:0.24 alpha:1.0] setFill];
    [[NSBezierPath bezierPathWithRoundedRect:knob xRadius:8.0 yRadius:8.0] fill];
}

- (void)updateGainForPoint:(NSPoint)point {
    const NSRect slider = [self sliderRect];
    CGFloat value = (point.x - slider.origin.x) / slider.size.width;
    if (value < 0.0) value = 0.0;
    if (value > 1.0) value = 1.0;
    g_gain = static_cast<float>(value);
    [self setNeedsDisplay:YES];

    if (g_host_callback) {
        g_host_callback(nullptr, audioMasterBeginEdit, 0, 0, nullptr, 0.0f);
        g_host_callback(nullptr, audioMasterAutomate, 0, 0, nullptr, g_gain);
        g_host_callback(nullptr, audioMasterUpdateDisplay, 0, 0, nullptr, 0.0f);
        g_host_callback(nullptr, audioMasterEndEdit, 0, 0, nullptr, 0.0f);
    }
}

- (void)mouseDown:(NSEvent *)event {
    [self updateGainForPoint:[self convertPoint:event.locationInWindow fromView:nil]];
}

- (void)mouseDragged:(NSEvent *)event {
    [self updateGainForPoint:[self convertPoint:event.locationInWindow fromView:nil]];
}

@end

static NSView *g_editor_view = nil;

static intptr_t __cdecl plugin_dispatcher(
    AEffect *effect, int opcode, int index, intptr_t value, void *ptr, float opt)
{
    (void)effect;
    (void)index;
    (void)value;
    (void)opt;

    switch (opcode) {
    case effOpen:
        return 0;
    case effClose:
        return 0;
    case effGetEffectName:
        if (ptr) std::strncpy(static_cast<char *>(ptr), DEMO_NAME, 63);
        return 1;
    case effGetVendorString:
        if (ptr) std::strncpy(static_cast<char *>(ptr), DEMO_VENDOR, 63);
        return 1;
    case effGetProductString:
        if (ptr) std::strncpy(static_cast<char *>(ptr), DEMO_PRODUCT, 63);
        return 1;
    case effGetVendorVersion:
        return DEMO_VERSION;
    case effGetPlugCategory:
        return kPlugCategEffect;
    case effGetParamName:
        if (ptr && index == 0) std::strncpy(static_cast<char *>(ptr), "Gain", 7);
        return 0;
    case effGetParamLabel:
        if (ptr && index == 0) std::strncpy(static_cast<char *>(ptr), "%", 7);
        return 0;
    case effGetParamDisplay:
        if (ptr && index == 0) std::snprintf(static_cast<char *>(ptr), 8, "%.0f", g_gain * 100.0f);
        return 0;
    case effSetSampleRate:
        g_sample_rate = opt > 1.0f ? opt : 44100.0f;
        return 0;
    case effSetBlockSize:
        return 0;
    case effMainsChanged:
        return 0;
    case effGetVstVersion:
        return 2400;
    case effEditGetRect:
        if (ptr) {
            *static_cast<ERect **>(ptr) = &g_editor_rect;
            return 1;
        }
        return 0;
    case effEditOpen:
        if (!ptr) return 0;
        @autoreleasepool {
            NSView *parent = (__bridge NSView *)ptr;
            if (!parent) return 0;
            [g_editor_view removeFromSuperview];
            KeepsakeDemoEditorView *view =
                [[KeepsakeDemoEditorView alloc] initWithFrame:NSMakeRect(0, 0, DEMO_EDITOR_WIDTH, DEMO_EDITOR_HEIGHT)];
            view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
            [parent addSubview:view];
            g_editor_view = view;
        }
        return 1;
    case effEditClose:
        @autoreleasepool {
            [g_editor_view removeFromSuperview];
            g_editor_view = nil;
        }
        return 1;
    case effEditIdle:
        @autoreleasepool {
            if (g_editor_view) [g_editor_view setNeedsDisplay:YES];
        }
        return 0;
    case effGetChunk:
        if (ptr) {
            g_chunk_gain = g_gain;
            *static_cast<void **>(ptr) = &g_chunk_gain;
            return static_cast<intptr_t>(sizeof(g_chunk_gain));
        }
        return 0;
    case effSetChunk:
        if (ptr && value >= static_cast<intptr_t>(sizeof(float))) {
            plugin_set_parameter(nullptr, 0, *static_cast<float *>(ptr));
            return 1;
        }
        return 0;
    default:
        return 0;
    }
}

static void __cdecl plugin_process_replacing(
    AEffect * /*effect*/, float **inputs, float **outputs, int frames)
{
    const double phase_step = (2.0 * M_PI * 220.0) / (g_sample_rate > 1.0 ? g_sample_rate : 44100.0);
    for (int frame = 0; frame < frames; ++frame) {
        float sample = std::sin(g_phase) * 0.12f * g_gain;
        g_phase += phase_step;
        if (g_phase >= 2.0 * M_PI) g_phase -= 2.0 * M_PI;

        for (int ch = 0; ch < DEMO_NUM_OUTPUTS; ++ch) {
            if (!outputs || !outputs[ch]) continue;
            const float input = (inputs && inputs[ch]) ? inputs[ch][frame] : 0.0f;
            outputs[ch][frame] = input * g_gain + sample;
        }
    }
}

static void __cdecl plugin_set_parameter(AEffect *, int index, float value) {
    if (index == 0) {
        if (value < 0.0f) value = 0.0f;
        if (value > 1.0f) value = 1.0f;
        g_gain = value;
        g_chunk_gain = value;
        @autoreleasepool {
            if (g_editor_view) [g_editor_view setNeedsDisplay:YES];
        }
    }
}

static float __cdecl plugin_get_parameter(AEffect *, int index) {
    if (index == 0) return g_gain;
    return 0.0f;
}

static AEffect g_effect;

#ifdef _WIN32
#define EXPORT extern "C" __declspec(dllexport)
#else
#define EXPORT extern "C" __attribute__((visibility("default")))
#endif

EXPORT AEffect *VSTPluginMain(audioMasterCallback hostCallback) {
    if (!hostCallback) return nullptr;

    g_host_callback = hostCallback;
    g_gain = 0.6f;
    g_chunk_gain = g_gain;
    g_phase = 0.0;
    g_sample_rate = 44100.0;
    std::memset(&g_effect, 0, sizeof(g_effect));
    g_effect.magic = kEffectMagic;
    g_effect.dispatcher = plugin_dispatcher;
    g_effect.setParameter = plugin_set_parameter;
    g_effect.getParameter = plugin_get_parameter;
    g_effect.numPrograms = 1;
    g_effect.numParams = DEMO_NUM_PARAMS;
    g_effect.numInputs = DEMO_NUM_INPUTS;
    g_effect.numOutputs = DEMO_NUM_OUTPUTS;
    g_effect.flags = effFlagsCanReplacing | effFlagsHasEditor;
    g_effect.uniqueID = DEMO_UNIQUE_ID;
    g_effect.version = DEMO_VERSION;
    g_effect.processReplacing = plugin_process_replacing;

    return &g_effect;
}
