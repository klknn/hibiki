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

void Vst3Plugin::runMainLoop() {
    @autoreleasepool {
        std::cerr << "BACKEND: Starting macOS main loop..." << std::endl;
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        [NSApp run];
        std::cerr << "BACKEND: macOS main loop exited." << std::endl;
    }
}

void Vst3Plugin::showEditor() {
    if (!impl->controller) {
        std::cerr << "No controller available for showing editor" << std::endl;
        return;
    }

    if (impl->editorRunning) return;
    impl->editorRunning = true;

    dispatch_async(dispatch_get_main_queue(), ^{
        @autoreleasepool {
            std::cerr << "BACKEND: Creating VST3 view..." << std::endl;
            Steinberg::IPtr<Steinberg::IPlugView> view = Steinberg::owned(impl->controller->createView(Steinberg::Vst::ViewType::kEditor));
            if (!view) {
                std::cerr << "BACKEND ERROR: Plugin does not provide an editor view" << std::endl;
                impl->editorRunning = false;
                return;
            }

            Steinberg::ViewRect rect;
            if (view->getSize(&rect) != Steinberg::kResultTrue) {
                std::cerr << "BACKEND ERROR: Cannot get view size" << std::endl;
                impl->editorRunning = false;
                return;
            }

            int width = rect.right - rect.left;
            int height = rect.bottom - rect.top;
            if (width <= 0 || height <= 0) {
                std::cerr << "BACKEND WARNING: Window size invalid (" << width << "x" << height << "), defaulting to 800x600" << std::endl;
                width = 800;
                height = 600;
            }
            std::cerr << "BACKEND: Editor size: " << width << "x" << height << std::endl;

            std::cerr << "BACKEND: Initializing NSWindow..." << std::endl;
            NSRect frame = NSMakeRect(0, 0, width, height);
            NSWindow* window = [[NSWindow alloc] initWithContentRect:frame
                                                         styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable)
                                                           backing:NSBackingStoreBuffered
                                                             defer:NO];
            if (!window) {
                std::cerr << "BACKEND ERROR: Failed to allocate NSWindow" << std::endl;
                impl->editorRunning = false;
                return;
            }
            [window setTitle:[NSString stringWithUTF8String:impl->name.c_str()]];
            [window setReleasedWhenClosed:NO];
            std::cerr << "BACKEND: Showing window..." << std::endl;
            [window makeKeyAndOrderFront:nil];

            std::cerr << "BACKEND: Setting up delegate..." << std::endl;
            VstWindowDelegate* delegate = [[VstWindowDelegate alloc] init];
            delegate.editorRunning = &impl->editorRunning;
            [window setDelegate:delegate];
            impl->windowDelegate = (__bridge_retained void*)delegate;

            impl->editorWindow = (uint64_t)window;
            impl->view = view;

            std::cerr << "BACKEND: Attaching VST3 view to window..." << std::endl;
            if (view->attached((__bridge void*)[window contentView], Steinberg::kPlatformTypeNSView) != Steinberg::kResultTrue) {
                std::cerr << "BACKEND ERROR: Failed to attach view to NSView" << std::endl;
                [window close];
                impl->editorRunning = false;
                return;
            }
            std::cerr << "BACKEND: VST3 view attached successfully." << std::endl;
        }
    });
}

void Vst3Plugin::stopEditor() {
    if (!impl->editorRunning) return;
    
    dispatch_async(dispatch_get_main_queue(), ^{
        if (impl->editorWindow) {
            uint64_t winId = impl->editorWindow.load();
            NSWindow* window = (__bridge NSWindow*)(void*)winId;
            [window close];
            if (impl->view) {
                impl->view->removed();
                impl->view = nullptr;
            }
            impl->editorWindow = 0;
            if (impl->windowDelegate) {
                VstWindowDelegate* delegate = (__bridge_transfer VstWindowDelegate*)impl->windowDelegate;
                delegate.editorRunning = nullptr;
                impl->windowDelegate = nullptr;
            }
        }
        impl->editorRunning = false;
    });
}
