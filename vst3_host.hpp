#pragma once

#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmessage.h"

#include <string>
#include <thread>
#include <atomic>
#include <vector>

struct HostProcessContext {
    double sampleRate;
    double tempo;
    int32_t timeSigNumerator;
    int32_t timeSigDenominator;
    int64_t continuousTimeSamples;
    double projectTimeMusic;
};

struct MidiNoteEvent {
    int32_t sampleOffset;
    uint8_t channel;
    uint8_t pitch;
    float velocity; // 0.0 - 1.0
    bool isNoteOn;
};


// Internal host context class is now defined in vst3_host.cpp

struct Vst3Plugin {

    VST3::Hosting::Module::Ptr module;
    Steinberg::IPtr<Steinberg::Vst::IComponent> component;
    Steinberg::IPtr<Steinberg::Vst::IAudioProcessor> processor;
    Steinberg::IPtr<Steinberg::Vst::IEditController> controller;
    Steinberg::IPtr<Steinberg::Vst::IHostApplication> hostContext;
    
    std::thread editorThread;
    std::atomic<bool> editorRunning{false};
    std::atomic<uint64_t> editorWindow{0}; // Store X11 Window ID

    bool load(const std::string& path, int plugin_index = 0);
    void showEditor();
    void stopEditor();
    static void listPlugins(const std::string& path);

    void process(float** inputs, float** outputs, int numSamples, 
                 const HostProcessContext& context, 
                 const std::vector<MidiNoteEvent>& events);

    ~Vst3Plugin();

};
