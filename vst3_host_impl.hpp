#pragma once

#include <atomic>
#include <thread>
#include <memory>
#include "public.sdk/source/vst/hosting/module.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivsthostapplication.h"

struct Vst3PluginImpl {
    VST3::Hosting::Module::Ptr module;
    Steinberg::IPtr<Steinberg::Vst::IComponent> component;
    Steinberg::IPtr<Steinberg::Vst::IAudioProcessor> processor;
    Steinberg::IPtr<Steinberg::Vst::IEditController> controller;
    Steinberg::IPtr<Steinberg::Vst::IHostApplication> hostContext;
    
    std::string name;
    bool isInstrument = false;
    std::thread editorThread;
    std::atomic<bool> editorRunning{false};
    std::atomic<uint64_t> editorWindow{0};
};
