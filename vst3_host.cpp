#include "vst3_host.hpp"
#include "pluginterfaces/base/ustring.h"
#include <iostream>
#include <vector>
#include <thread>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include "pluginterfaces/gui/iplugview.h"
#include "public.sdk/source/common/memorystream.h"




Vst3HostContext::Vst3HostContext() {}
Vst3HostContext::~Vst3HostContext() {}

Steinberg::uint32 PLUGIN_API Vst3HostContext::addRef() { return 1; }
Steinberg::uint32 PLUGIN_API Vst3HostContext::release() { return 1; }

Steinberg::tresult PLUGIN_API Vst3HostContext::queryInterface(const Steinberg::TUID _iid, void** obj) {
    QUERY_INTERFACE(_iid, obj, Steinberg::Vst::IHostApplication::iid, Steinberg::Vst::IHostApplication)
    QUERY_INTERFACE(_iid, obj, Steinberg::Vst::IComponentHandler::iid, Steinberg::Vst::IComponentHandler)
    if (Steinberg::FUnknownPrivate::iidEqual(_iid, Steinberg::FUnknown::iid)) {
        *obj = static_cast<Steinberg::Vst::IHostApplication*>(this);
        addRef();
        return Steinberg::kResultTrue;
    }
    return Steinberg::kNoInterface;
}



Steinberg::tresult PLUGIN_API Vst3HostContext::getName(Steinberg::Vst::String128 name) {

    Steinberg::UString str(name, 128);
    str.fromAscii("Hibiki DAW");
    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API Vst3HostContext::createInstance(Steinberg::TUID cid, Steinberg::TUID iid, void** obj) {
    *obj = nullptr;
    if (Steinberg::FUnknownPrivate::iidEqual(cid, Steinberg::Vst::IMessage::iid) && Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IMessage::iid)) {
        *obj = new Steinberg::Vst::HostMessage;
        return Steinberg::kResultTrue;
    }
    if (Steinberg::FUnknownPrivate::iidEqual(cid, Steinberg::Vst::IAttributeList::iid) && Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IAttributeList::iid)) {
        if (auto attr = Steinberg::Vst::HostAttributeList::make()) {
            attr->addRef();
            *obj = attr;
            return Steinberg::kResultTrue;
        }
    }
    return Steinberg::kResultFalse;
}

Steinberg::tresult PLUGIN_API Vst3HostContext::beginEdit(Steinberg::Vst::ParamID tag) { return Steinberg::kResultTrue; }
Steinberg::tresult PLUGIN_API Vst3HostContext::performEdit(Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue valueNormalized) { return Steinberg::kResultTrue; }
Steinberg::tresult PLUGIN_API Vst3HostContext::endEdit(Steinberg::Vst::ParamID tag) { return Steinberg::kResultTrue; }
Steinberg::tresult PLUGIN_API Vst3HostContext::restartComponent(Steinberg::int32 flags) { return Steinberg::kResultTrue; }


bool Vst3Plugin::load(const std::string& path, int plugin_index) {
    std::string error;
    module = VST3::Hosting::Module::create(path, error);
    if (!module) {
        std::cerr << "Failed to load VST3 module: " << error << std::endl;
        return false;
    }

    auto factory = module->getFactory();
    auto classes = factory.classInfos();
    std::vector<VST3::Hosting::ClassInfo> audioEffects;
    for (auto& info : classes) {
        if (info.category() == kVstAudioEffectClass) {
            audioEffects.push_back(info);
        }
    }

    if (plugin_index < 0 || plugin_index >= (int)audioEffects.size()) {
        std::cerr << "Plugin index " << plugin_index << " out of range (found " << audioEffects.size() << " audio effects)\n";
        return false;
    }

    auto& info = audioEffects[plugin_index];
    component = factory.createInstance<Steinberg::Vst::IComponent>(info.ID());
    if (!component) {
        std::cerr << "Failed to create IComponent for " << info.name() << std::endl;
        return false;
    }

    component->queryInterface(Steinberg::Vst::IAudioProcessor::iid, (void**)&processor);
    if (!processor) {
        processor = factory.createInstance<Steinberg::Vst::IAudioProcessor>(info.ID());
    }

    if (!processor) {
        std::cerr << "Failed to create IAudioProcessor for " << info.name() << std::endl;
        return false;
    }

    hostContext = Steinberg::owned(new Vst3HostContext());
    if (component->initialize(hostContext) != Steinberg::kResultTrue) {
        std::cerr << "Failed to initialize component" << std::endl;
        return false;
    }

    // Get EditController (needed for GUI)
    if (component->queryInterface(Steinberg::Vst::IEditController::iid, (void**)&controller) != Steinberg::kResultTrue) {
        Steinberg::TUID controllerCID;
        if (component->getControllerClassId(controllerCID) == Steinberg::kResultTrue) {
            controller = factory.createInstance<Steinberg::Vst::IEditController>(controllerCID);
        }
        
        if (!controller) {
            std::cerr << "Failed to get IEditController from component, trying factory as fallback..." << std::endl;
            controller = factory.createInstance<Steinberg::Vst::IEditController>(info.ID());
        }
    }

    
    if (controller) {
        controller->initialize(hostContext);
        
        Steinberg::Vst::IComponentHandler* handler = nullptr;
        if (hostContext->queryInterface(Steinberg::Vst::IComponentHandler::iid, (void**)&handler) == Steinberg::kResultTrue) {
            controller->setComponentHandler(handler);
            handler->release();
        }
        
        // Connect component and controller

        Steinberg::IPtr<Steinberg::Vst::IConnectionPoint> cp1, cp2;
        component->queryInterface(Steinberg::Vst::IConnectionPoint::iid, (void**)&cp1);
        controller->queryInterface(Steinberg::Vst::IConnectionPoint::iid, (void**)&cp2);
        if (cp1 && cp2) {
            cp1->connect(cp2);
            cp2->connect(cp1);
        }
        
        // Sync state
        Steinberg::MemoryStream stream;
        if (component->getState(&stream) == Steinberg::kResultTrue) {
            stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
            controller->setComponentState(&stream);
        }
    }


    Steinberg::Vst::ProcessSetup setup;

    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.maxSamplesPerBlock = 512;
    setup.sampleRate = 44100.0;
    
    processor->setupProcessing(setup);
    component->setActive(true);

    int numInBuses = component->getBusCount(Steinberg::Vst::kAudio, Steinberg::Vst::kInput);
    int numOutBuses = component->getBusCount(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput);
    std::cout << "Plugin: " << info.name() << " - Audio Buses - In: " << numInBuses << ", Out: " << numOutBuses << "\n";

    component->activateBus(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput, 0, true);
    component->activateBus(Steinberg::Vst::kEvent, Steinberg::Vst::kInput, 0, true);

    processor->setProcessing(true);

    return true;
}

void Vst3Plugin::showEditor() {
    if (!controller) {
        std::cerr << "No controller available for showing editor" << std::endl;
        return;
    }

    if (editorRunning) {
        std::cout << "Editor already running" << std::endl;
        return;
    }

    editorRunning = true;
    editorThread = std::thread([this]() {
        Steinberg::IPtr<Steinberg::IPlugView> view = Steinberg::owned(controller->createView(Steinberg::Vst::ViewType::kEditor));
        if (!view) {
            std::cerr << "Plugin does not provide an editor view" << std::endl;
            editorRunning = false;
            return;
        }

        Display* display = XOpenDisplay(NULL);
        if (!display) {
            std::cerr << "Cannot open X display" << std::endl;
            editorRunning = false;
            return;
        }
        std::cout << "X11 Display opened successfully" << std::endl;

        Steinberg::ViewRect rect;
        if (view->getSize(&rect) != Steinberg::kResultTrue) {
            std::cerr << "Cannot get view size" << std::endl;
            XCloseDisplay(display);
            editorRunning = false;
            return;
        }

        int width = rect.right - rect.left;
        int height = rect.bottom - rect.top;
        std::cout << "Plugin View Size: " << width << "x" << height << std::endl;

        int screen = DefaultScreen(display);
        Window window = XCreateSimpleWindow(display, RootWindow(display, screen), 0, 0, width, height, 1,
                                          BlackPixel(display, screen), WhitePixel(display, screen));

        editorWindow = (uint64_t)window;

        XStoreName(display, window, "Vst3 Plugin Editor");
        XSelectInput(display, window, ExposureMask | KeyPressMask | StructureNotifyMask | SubstructureNotifyMask);
        XMapWindow(display, window);
        XFlush(display);
        std::cout << "X11 Window created and mapped" << std::endl;

        if (view->attached((void*)window, Steinberg::kPlatformTypeX11EmbedWindowID) != Steinberg::kResultTrue) {
            std::cerr << "Failed to attach view to X11 window" << std::endl;
            XDestroyWindow(display, window);
            XCloseDisplay(display);
            editorRunning = false;
            return;
        }
        std::cout << "Plugin View attached successfully" << std::endl;

        XEvent event;
        while (editorRunning) {
            while (XPending(display)) {
                XNextEvent(display, &event);
                if (event.type == DestroyNotify && event.xdestroywindow.window == window) {
                    editorRunning = false;
                    break;
                }
            }
            if (!editorRunning) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        view->removed();
        XDestroyWindow(display, window);
        XCloseDisplay(display);
        editorRunning = false;
        editorWindow = 0;
        std::cout << "Editor thread finished" << std::endl;
    });
}

void Vst3Plugin::stopEditor() {
    if (editorRunning) {
        editorRunning = false;
        if (editorThread.joinable()) {
            editorThread.join();
        }
    }
}


void Vst3Plugin::listPlugins(const std::string& path) {
    std::string error;
    auto mod = VST3::Hosting::Module::create(path, error);
    if (!mod) {
        std::cerr << "Error: " << error << std::endl;
        return;
    }

    auto factory = mod->getFactory();
    auto classes = factory.classInfos();
    int idx = 0;
    for (auto& info : classes) {
        if (info.category() == kVstAudioEffectClass) {
            std::cout << idx << ":" << info.name() << "\n";
            idx++;
        }
    }
}



Vst3Plugin::~Vst3Plugin() {
    stopEditor();
    if (processor) {
        processor->setProcessing(false);
    }

    if (component) {
        component->setActive(false);
        component->terminate();
    }
}

