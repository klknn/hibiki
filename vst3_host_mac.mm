#import <Cocoa/Cocoa.h>
#include "vst3_host.hpp"
#include "vst3_host_impl.hpp"
#include "pluginterfaces/gui/iplugview.h"
#include <iostream>

@interface VstWindowDelegate : NSObject <NSWindowDelegate>
@property (assign) std::atomic<bool>* editorRunning;
@end

@implementation VstWindowDelegate
- (void)windowWillClose:(NSNotification *)notification {
    if (_editorRunning) {
        *_editorRunning = false;
    }
}
@end

void Vst3Plugin::showEditor() {
    if (!impl->controller) {
        std::cerr << "No controller available for showing editor" << std::endl;
        return;
    }

    if (impl->editorRunning) return;
    stopEditor();

    impl->editorRunning = true;
    impl->editorThread = std::thread([this]() {
        @autoreleasepool {
            Steinberg::IPtr<Steinberg::IPlugView> view = Steinberg::owned(impl->controller->createView(Steinberg::Vst::ViewType::kEditor));
            if (!view) {
                std::cerr << "Plugin does not provide an editor view" << std::endl;
                impl->editorRunning = false;
                return;
            }

            Steinberg::ViewRect rect;
            if (view->getSize(&rect) != Steinberg::kResultTrue) {
                std::cerr << "Cannot get view size" << std::endl;
                impl->editorRunning = false;
                return;
            }

            int width = rect.right - rect.left;
            int height = rect.bottom - rect.top;

            // On macOS, UI elements should really be on the main thread.
            // But for a CLI tool, we can try running a run loop in this thread.
            // Note: This might not work for all plugins.
            
            NSRect frame = NSMakeRect(0, 0, width, height);
            NSWindow* window = [[NSWindow alloc] initWithContentRect:frame
                                                         styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable)
                                                           backing:NSBackingStoreBuffered
                                                             defer:NO];
            [window setTitle:[NSString stringWithUTF8String:impl->name.c_str()]];
            [window setReleasedWhenClosed:NO];
            [window makeKeyAndOrderFront:nil];

            VstWindowDelegate* delegate = [[VstWindowDelegate alloc] init];
            delegate.editorRunning = &impl->editorRunning;
            [window setDelegate:delegate];

            impl->editorWindow = (uint64_t)window;

            if (view->attached((__bridge void*)[window contentView], Steinberg::kPlatformTypeNSView) != Steinberg::kResultTrue) {
                std::cerr << "Failed to attach view to NSView" << std::endl;
                [window close];
                impl->editorRunning = false;
                return;
            }

            // Run local loop
            while (impl->editorRunning) {
                @autoreleasepool {
                    NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                                        untilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]
                                                           inMode:NSDefaultRunLoopMode
                                                          dequeue:YES];
                    if (event) {
                        [NSApp sendEvent:event];
                    }
                }
            }

            view->removed();
            [window close];
            impl->editorWindow = 0;
            impl->editorRunning = false;
        }
    });
}

void Vst3Plugin::stopEditor() {
    if (impl->editorThread.joinable()) {
        impl->editorRunning = false;
        impl->editorThread.join();
    } else {
        impl->editorRunning = false;
    }
}
