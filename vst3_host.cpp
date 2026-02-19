#include "vst3_host.hpp"

#include <atomic>

#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"




// Very basic event list moved from main.cpp
class VstEventList : public Steinberg::Vst::IEventList {
    std::vector<Steinberg::Vst::Event> events;
public:
    VstEventList() {}
    virtual ~VstEventList() {}
    
    DECLARE_FUNKNOWN_METHODS
    Steinberg::int32 PLUGIN_API getEventCount() override { return events.size(); }
    Steinberg::tresult PLUGIN_API getEvent(Steinberg::int32 index, Steinberg::Vst::Event& e) override {
        if (index < 0 || index >= (int)events.size()) return Steinberg::kResultFalse;
        e = events[index];
        return Steinberg::kResultTrue;
    }
    Steinberg::tresult PLUGIN_API addEvent(Steinberg::Vst::Event& e) override {
        events.push_back(e);
        return Steinberg::kResultTrue;
    }
    void clear() { events.clear(); }
};
IMPLEMENT_FUNKNOWN_METHODS(VstEventList, Steinberg::Vst::IEventList, Steinberg::Vst::IEventList::iid)



class Vst3HostContext : public Steinberg::Vst::IHostApplication, public Steinberg::Vst::IComponentHandler {
public:
    Vst3HostContext();
    virtual ~Vst3HostContext();

    DECLARE_FUNKNOWN_METHODS

    // IHostApplication
    Steinberg::tresult PLUGIN_API getName(Steinberg::Vst::String128 name) override;
    Steinberg::tresult PLUGIN_API createInstance(Steinberg::TUID cid, Steinberg::TUID iid, void** obj) override;

    // IComponentHandler
    Steinberg::tresult PLUGIN_API beginEdit(Steinberg::Vst::ParamID tag) override;
    Steinberg::tresult PLUGIN_API performEdit(Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue valueNormalized) override;
    Steinberg::tresult PLUGIN_API endEdit(Steinberg::Vst::ParamID tag) override;
    Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32 flags) override;

private:
    std::atomic<uint32_t> refCount;
};


Vst3HostContext::Vst3HostContext() : refCount(1) {}

Vst3HostContext::~Vst3HostContext() {}

Steinberg::uint32 PLUGIN_API Vst3HostContext::addRef() { return ++refCount; }
Steinberg::uint32 PLUGIN_API Vst3HostContext::release() {
    Steinberg::uint32 r = --refCount;
    if (r == 0) delete this;
    return r;
}

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


struct Vst3PluginImpl {
    VST3::Hosting::Module::Ptr module;
    Steinberg::IPtr<Steinberg::Vst::IComponent> component;
    Steinberg::IPtr<Steinberg::Vst::IAudioProcessor> processor;
    Steinberg::IPtr<Steinberg::Vst::IEditController> controller;
    Steinberg::IPtr<Steinberg::Vst::IHostApplication> hostContext;
    
    std::thread editorThread;
    std::atomic<bool> editorRunning{false};
    std::atomic<uint64_t> editorWindow{0};
};

Vst3Plugin::Vst3Plugin() : impl(std::make_unique<Vst3PluginImpl>()) {}

Vst3Plugin::~Vst3Plugin() {
    stopEditor();
    
    if (impl->processor) {
        impl->processor->setProcessing(false);
    }

    if (impl->controller) {
        impl->controller->setComponentHandler(nullptr);
        impl->controller->terminate();
    }

    if (impl->component) {
        impl->component->setActive(false);
        impl->component->terminate();
    }
}



bool Vst3Plugin::load(const std::string& path, int plugin_index) {
    std::string error;
    impl->module = VST3::Hosting::Module::create(path, error);
    if (!impl->module) {
        std::cerr << "Failed to load VST3 module: " << error << std::endl;
        return false;
    }

    auto factory = impl->module->getFactory();
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
    impl->component = factory.createInstance<Steinberg::Vst::IComponent>(info.ID());
    if (!impl->component) {
        std::cerr << "Failed to create IComponent for " << info.name() << std::endl;
        return false;
    }

    impl->component->queryInterface(Steinberg::Vst::IAudioProcessor::iid, (void**)&impl->processor);
    if (!impl->processor) {
        impl->processor = factory.createInstance<Steinberg::Vst::IAudioProcessor>(info.ID());
    }

    if (!impl->processor) {
        std::cerr << "Failed to create IAudioProcessor for " << info.name() << std::endl;
        return false;
    }

    impl->hostContext = Steinberg::owned(new Vst3HostContext());
    if (impl->component->initialize(impl->hostContext) != Steinberg::kResultTrue) {
        std::cerr << "Failed to initialize component" << std::endl;
        return false;
    }

    // Get EditController (needed for GUI)
    if (impl->component->queryInterface(Steinberg::Vst::IEditController::iid, (void**)&impl->controller) != Steinberg::kResultTrue) {
        Steinberg::TUID controllerCID;
        if (impl->component->getControllerClassId(controllerCID) == Steinberg::kResultTrue) {
            impl->controller = factory.createInstance<Steinberg::Vst::IEditController>(controllerCID);
        }
        
        if (!impl->controller) {
            std::cerr << "Failed to get IEditController from component, trying factory as fallback..." << std::endl;
            impl->controller = factory.createInstance<Steinberg::Vst::IEditController>(info.ID());
        }
    }

    
    if (impl->controller) {
        impl->controller->initialize(impl->hostContext);
        
        Steinberg::Vst::IComponentHandler* handler = nullptr;
        if (impl->hostContext->queryInterface(Steinberg::Vst::IComponentHandler::iid, (void**)&handler) == Steinberg::kResultTrue) {
            impl->controller->setComponentHandler(handler);
            handler->release();
        }
        
        // Connect component and controller

        Steinberg::IPtr<Steinberg::Vst::IConnectionPoint> cp1, cp2;
        impl->component->queryInterface(Steinberg::Vst::IConnectionPoint::iid, (void**)&cp1);
        impl->controller->queryInterface(Steinberg::Vst::IConnectionPoint::iid, (void**)&cp2);
        if (cp1 && cp2) {
            cp1->connect(cp2);
            cp2->connect(cp1);
        }
        
        // Sync state
        Steinberg::MemoryStream stream;
        if (impl->component->getState(&stream) == Steinberg::kResultTrue) {
            std::cout << "Vst3Plugin::load: Syncing state to controller..." << std::endl;
            stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
            impl->controller->setComponentState(&stream);
        }
    } else {
        std::cout << "Vst3Plugin::load: No controller available." << std::endl;
    }



    // Activate audio buses
    int numInBuses = impl->component->getBusCount(Steinberg::Vst::kAudio, Steinberg::Vst::kInput);
    for (int i = 0; i < numInBuses; i++) {
        impl->component->activateBus(Steinberg::Vst::kAudio, Steinberg::Vst::kInput, i, true);
    }
    int numOutBuses = impl->component->getBusCount(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput);
    for (int i = 0; i < numOutBuses; i++) {
        impl->component->activateBus(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput, i, true);
    }

    // Activate event buses (MIDI)
    int numInEventBuses = impl->component->getBusCount(Steinberg::Vst::kEvent, Steinberg::Vst::kInput);
    for (int i = 0; i < numInEventBuses; i++) {
        impl->component->activateBus(Steinberg::Vst::kEvent, Steinberg::Vst::kInput, i, true);
    }

    if (impl->component->setActive(true) != Steinberg::kResultTrue) {
        std::cerr << "Failed to activate component" << std::endl;
        return false;
    }

    Steinberg::Vst::ProcessSetup setup;
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.maxSamplesPerBlock = 512;
    setup.sampleRate = 44100.0;
    
    if (impl->processor->setupProcessing(setup) != Steinberg::kResultTrue) {
        std::cerr << "Failed to setup processing" << std::endl;
        impl->component->setActive(false);
        return false;
    }

    std::cout << "Plugin: " << info.name() << " - Audio Buses - In: " << numInBuses << ", Out: " << numOutBuses << "\n";

    impl->processor->setProcessing(true);

    return true;
}



void Vst3Plugin::showEditor() {
    if (!impl->controller) {
        std::cerr << "No controller available for showing editor" << std::endl;
        return;
    }

    if (impl->editorRunning) return;

    // Clean up any old finished thread
    stopEditor();

    impl->editorRunning = true;
    impl->editorThread = std::thread([this]() {
        Steinberg::IPtr<Steinberg::IPlugView> view = Steinberg::owned(impl->controller->createView(Steinberg::Vst::ViewType::kEditor));
        if (!view) {
            std::cerr << "Plugin does not provide an editor view" << std::endl;
            impl->editorRunning = false;
            return;
        }


        Display* display = XOpenDisplay(NULL);
        if (!display) {
            std::cerr << "Cannot open X display" << std::endl;
            impl->editorRunning = false;
            return;
        }

        Steinberg::ViewRect rect;
        if (view->getSize(&rect) != Steinberg::kResultTrue) {
            std::cerr << "Cannot get view size" << std::endl;
            XCloseDisplay(display);
            impl->editorRunning = false;
            return;
        }

        int width = rect.right - rect.left;
        int height = rect.bottom - rect.top;
        std::cout << "Plugin View Size: " << width << "x" << height << std::endl;

        int screen = DefaultScreen(display);
        Window window = XCreateSimpleWindow(display, RootWindow(display, screen), 0, 0, width, height, 1,
                                          BlackPixel(display, screen), WhitePixel(display, screen));

        impl->editorWindow = (uint64_t)window;

        XStoreName(display, window, "Vst3 Plugin Editor");
        XSelectInput(display, window, ExposureMask | KeyPressMask | StructureNotifyMask | SubstructureNotifyMask);

        // Intercept window close request
        Atom wmDeleteMessage = XInternAtom(display, "WM_DELETE_WINDOW", False);
        XSetWMProtocols(display, window, &wmDeleteMessage, 1);

        XMapWindow(display, window);
        XFlush(display);
        std::cout << "X11 Window created and mapped" << std::endl;


        if (view->attached((void*)window, Steinberg::kPlatformTypeX11EmbedWindowID) != Steinberg::kResultTrue) {
            std::cerr << "Failed to attach view to X11 window" << std::endl;
            XDestroyWindow(display, window);
            XCloseDisplay(display);
            impl->editorRunning = false;
            return;
        }
        std::cout << "Plugin View attached successfully" << std::endl;

        XEvent event;
        bool windowWasDestroyed = false;
        while (impl->editorRunning) {
            while (impl->editorRunning && XPending(display)) {
                XNextEvent(display, &event);
                if (event.type == DestroyNotify && (event.xdestroywindow.window == window)) {
                    std::cout << "X11 Window destroyed by WM" << std::endl;
                    windowWasDestroyed = true;
                    impl->editorRunning = false;
                    break;
                }
                if (event.type == ClientMessage) {
                    if ((Atom)event.xclient.data.l[0] == wmDeleteMessage) {
                        std::cout << "X11 Close button clicked" << std::endl;
                        impl->editorRunning = false;
                        break;
                    }
                }
            }
            if (!impl->editorRunning) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        std::cout << "Cleaning up VST3 view..." << std::endl;
        view->removed();
        
        if (!windowWasDestroyed) {
            XDestroyWindow(display, window);
            XSync(display, False); // Ensure destruction finishes before closing display
        }
        XCloseDisplay(display);
        impl->editorWindow = 0;
        impl->editorRunning = false;
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



void Vst3Plugin::process(float** inputs, float** outputs, int numSamples, 
                        const HostProcessContext& context, 
                        const std::vector<MidiNoteEvent>& events) {
    if (!impl->processor) return;

    Steinberg::Vst::AudioBusBuffers inBuses = {}, outBuses = {};
    inBuses.numChannels = 2; // Assuming stereo for now
    inBuses.silenceFlags = 0;
    inBuses.channelBuffers32 = inputs;

    outBuses.numChannels = 2;
    outBuses.silenceFlags = 0;
    outBuses.channelBuffers32 = outputs;

    VstEventList eventList;
    for (const auto& me : events) {
        Steinberg::Vst::Event e = {};
        e.sampleOffset = me.sampleOffset;
        if (me.isNoteOn) {
            e.type = Steinberg::Vst::Event::kNoteOnEvent;
            e.noteOn.channel = me.channel;
            e.noteOn.pitch = me.pitch;
            e.noteOn.velocity = me.velocity;
        } else {
            e.type = Steinberg::Vst::Event::kNoteOffEvent;
            e.noteOff.channel = me.channel;
            e.noteOff.pitch = me.pitch;
            e.noteOff.velocity = me.velocity;
        }
        eventList.addEvent(e);
    }

    Steinberg::Vst::ProcessContext vstContext = {};
    vstContext.state = Steinberg::Vst::ProcessContext::kPlaying;
    vstContext.sampleRate = context.sampleRate;
    vstContext.tempo = context.tempo;
    vstContext.timeSigNumerator = context.timeSigNumerator;
    vstContext.timeSigDenominator = context.timeSigDenominator;
    vstContext.continousTimeSamples = context.continuousTimeSamples;
    vstContext.projectTimeMusic = context.projectTimeMusic;

    Steinberg::Vst::ProcessData data;
    data.processMode = Steinberg::Vst::kRealtime;
    data.symbolicSampleSize = Steinberg::Vst::kSample32;
    data.numSamples = numSamples;
    data.numInputs = 1;
    data.inputs = &inBuses;
    data.numOutputs = 1;
    data.outputs = &outBuses;
    data.inputParameterChanges = nullptr;
    data.outputParameterChanges = nullptr;
    data.inputEvents = &eventList;
    data.outputEvents = nullptr;
    data.processContext = &vstContext;

    impl->processor->process(data);

}




