#include "vst3_host.hpp"
#include "pluginterfaces/base/ustring.h"
#include <iostream>

Vst3HostContext::Vst3HostContext() {}
Vst3HostContext::~Vst3HostContext() {}

IMPLEMENT_FUNKNOWN_METHODS(Vst3HostContext, Steinberg::Vst::IHostApplication, Steinberg::Vst::IHostApplication::iid)

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

bool Vst3Plugin::load(const std::string& path) {
    std::string error;
    module = VST3::Hosting::Module::create(path, error);
    if (!module) {
        std::cerr << "Failed to load VST3 module: " << error << std::endl;
        return false;
    }

    auto factory = module->getFactory();
    for (Steinberg::uint32 i = 0; i < factory.classCount(); ++i) {
        auto classes = factory.classInfos();
        if (i < classes.size()) {
            auto& info = classes[i];
            if (info.category() == kVstAudioEffectClass) {
                component = factory.createInstance<Steinberg::Vst::IComponent>(info.ID());
                if (component) {
                    component->queryInterface(Steinberg::Vst::IAudioProcessor::iid, (void**)&processor);
                    // If not a single component effect, create processor separately
                    if (!processor) {
                        processor = factory.createInstance<Steinberg::Vst::IAudioProcessor>(info.ID());
                    }
                    break;
                }
            }
        }
    }

    if (!component || !processor) {
        std::cerr << "Failed to create IComponent or IAudioProcessor" << std::endl;
        return false;
    }

    hostContext = Steinberg::owned(new Vst3HostContext());
    if (component->initialize(hostContext) != Steinberg::kResultTrue) {
        std::cerr << "Failed to initialize component" << std::endl;
        return false;
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
    std::cout << "Plugin Audio Buses - In: " << numInBuses << ", Out: " << numOutBuses << "\n";

    int numEventInBuses = component->getBusCount(Steinberg::Vst::kEvent, Steinberg::Vst::kInput);
    std::cout << "Plugin Event Buses - In: " << numEventInBuses << "\n";

    component->activateBus(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput, 0, true);
    component->activateBus(Steinberg::Vst::kEvent, Steinberg::Vst::kInput, 0, true);

    processor->setProcessing(true);

    return true;
}

Vst3Plugin::~Vst3Plugin() {
    if (processor) {
        processor->setProcessing(false);
    }
    if (component) {
        component->setActive(false);
        component->terminate();
    }
}
