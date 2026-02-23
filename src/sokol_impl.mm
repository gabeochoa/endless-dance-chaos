// Objective-C++ file that compiles the Sokol implementations for Metal on macOS.
// Compiled as a single translation unit with SOKOL_IMPL defined.

#define SOKOL_IMPL
#define SOKOL_METAL
#define SOKOL_NO_ENTRY

#include <sokol/sokol_app.h>
#include <sokol/sokol_gfx.h>
#include <sokol/sokol_glue.h>
#include <sokol/sokol_time.h>
#include <sokol/sokol_log.h>

#define SOKOL_GL_IMPL
#include <sokol/sokol_gl.h>

#define FONTSTASH_IMPLEMENTATION
#include <fontstash/stb_truetype.h>
#include <fontstash/fontstash.h>

#define SOKOL_FONTSTASH_IMPL
#include <sokol/sokol_fontstash.h>

#import <AppKit/AppKit.h>

extern "C" void metal_set_window_size(int width, int height) {
    @autoreleasepool {
        NSWindow* window = (__bridge NSWindow*)sapp_macos_get_window();
        if (window) {
            NSRect frame = [window frame];
            NSRect content = [window contentRectForFrameRect:frame];
            content.size.width = width;
            content.size.height = height;
            NSRect newFrame = [window frameRectForContentRect:content];
            newFrame.origin.y += frame.size.height - newFrame.size.height;
            [window setFrame:newFrame display:YES animate:NO];
        }
    }
}

extern "C" void metal_take_screenshot(const char* filename) {
    @autoreleasepool {
        NSWindow* window = [NSApp mainWindow];
        if (!window) window = [NSApp keyWindow];
        if (!window) {
            for (NSWindow* w in [NSApp windows]) {
                if ([w isVisible]) { window = w; break; }
            }
        }
        if (!window) {
            NSLog(@"take_screenshot: no window available");
            return;
        }

        CGWindowID windowID = (CGWindowID)[window windowNumber];
        NSString* path = [NSString stringWithUTF8String:filename];
        NSString* cmd = [NSString stringWithFormat:
            @"/usr/sbin/screencapture -x -o -l %u %@", windowID, path];
        int ret = system([cmd UTF8String]);
        if (ret != 0) {
            NSLog(@"take_screenshot: screencapture failed with code %d", ret);
        }
    }
}
