//
// Bridge GUI — macOS window chrome views.
//

#import "bridge_gui_mac_internal.h"

@interface KeepsakeFlippedView : NSView
@end

@implementation KeepsakeFlippedView
- (BOOL)isFlipped { return YES; }
@end

@interface KeepsakeHeaderView : KeepsakeFlippedView
@property (nonatomic, strong) NSString *pluginName;
@property (nonatomic, strong) NSString *formatBadge;
@property (nonatomic, strong) NSString *archBadge;
@property (nonatomic, strong) NSString *isolationBadge;
@property (nonatomic, strong) NSString *presentationBadge;
@end

@implementation KeepsakeHeaderView

- (void)drawRect:(NSRect)dirtyRect {
    [[NSColor colorWithWhite:0.15 alpha:1.0] setFill];
    NSRectFill(self.bounds);

    [[NSColor colorWithWhite:0.3 alpha:1.0] setFill];
    NSRectFill(NSMakeRect(0, 0, self.bounds.size.width, 1));

    NSDictionary *nameAttrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:11 weight:NSFontWeightMedium],
        NSForegroundColorAttributeName: [NSColor colorWithWhite:0.9 alpha:1.0]
    };
    NSDictionary *badgeAttrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:9 weight:NSFontWeightSemibold],
        NSForegroundColorAttributeName: [NSColor colorWithWhite:0.7 alpha:1.0]
    };

    CGFloat x = 10;
    CGFloat textY = 6;
    if (self.pluginName) {
        [self.pluginName drawAtPoint:NSMakePoint(x, textY) withAttributes:nameAttrs];
        x += [self.pluginName sizeWithAttributes:nameAttrs].width + 12;
    }

    __block CGFloat rx = self.bounds.size.width - 10;
    auto drawBadge = ^(NSString *text, NSColor *bg) {
        if (!text || text.length == 0) return;
        NSSize sz = [text sizeWithAttributes:badgeAttrs];
        CGFloat bw = sz.width + 8;
        CGFloat bh = 16;
        CGFloat bx = rx - bw;
        CGFloat by = (HEADER_HEIGHT - bh) / 2;

        NSBezierPath *path = [NSBezierPath bezierPathWithRoundedRect:
            NSMakeRect(bx, by, bw, bh) xRadius:3 yRadius:3];
        [bg setFill];
        [path fill];
        [text drawAtPoint:NSMakePoint(bx + 4, by + 2) withAttributes:badgeAttrs];
        rx = bx - 6;
    };

    drawBadge(self.isolationBadge,
              [NSColor colorWithRed:0.3 green:0.3 blue:0.5 alpha:1.0]);
    drawBadge(self.archBadge,
              [NSColor colorWithRed:0.3 green:0.45 blue:0.3 alpha:1.0]);
    drawBadge(self.formatBadge,
              [NSColor colorWithRed:0.45 green:0.3 blue:0.2 alpha:1.0]);
    drawBadge(self.presentationBadge,
              [NSColor colorWithRed:0.2 green:0.38 blue:0.55 alpha:1.0]);
}

@end

NSView *gui_mac_make_content_view(int w, int h) {
    return [[KeepsakeFlippedView alloc]
        initWithFrame:NSMakeRect(0, 0, w, h + HEADER_HEIGHT)];
}

NSView *gui_mac_make_editor_container(int w, int h) {
    NSView *view = [[KeepsakeFlippedView alloc]
        initWithFrame:NSMakeRect(0, HEADER_HEIGHT, w, h)];
    [view setWantsLayer:YES];
    view.layer.masksToBounds = YES;
    return view;
}

NSView *gui_mac_make_header_view(int w, const EditorHeaderInfo &header) {
    KeepsakeHeaderView *headerView = [[KeepsakeHeaderView alloc]
        initWithFrame:NSMakeRect(0, 0, w, HEADER_HEIGHT)];
    headerView.pluginName = [NSString stringWithUTF8String:header.plugin_name.c_str()];
    headerView.formatBadge = [NSString stringWithUTF8String:header.format.c_str()];
    headerView.archBadge = [NSString stringWithUTF8String:header.architecture.c_str()];
    headerView.isolationBadge = [NSString stringWithUTF8String:header.isolation.c_str()];
    headerView.presentationBadge = [NSString stringWithUTF8String:header.presentation.c_str()];
    [headerView setWantsLayer:YES];
    return headerView;
}
