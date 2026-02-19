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

    ~Vst3Plugin();
};
