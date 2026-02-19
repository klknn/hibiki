#pragma once

#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmessage.h"

#include <string>

class Vst3HostContext : public Steinberg::Vst::IHostApplication {
public:
    Vst3HostContext();
    virtual ~Vst3HostContext();

    DECLARE_FUNKNOWN_METHODS

    Steinberg::tresult PLUGIN_API getName(Steinberg::Vst::String128 name) override;
    Steinberg::tresult PLUGIN_API createInstance(Steinberg::TUID cid, Steinberg::TUID iid, void** obj) override;
};

struct Vst3Plugin {
    VST3::Hosting::Module::Ptr module;
    Steinberg::IPtr<Steinberg::Vst::IComponent> component;
    Steinberg::IPtr<Steinberg::Vst::IAudioProcessor> processor;
    Steinberg::IPtr<Steinberg::Vst::IEditController> controller;
    Steinberg::IPtr<Steinberg::Vst::IHostApplication> hostContext;
    
    bool load(const std::string& path, int plugin_index = 0);
    void showEditor();
    static void listPlugins(const std::string& path);

    ~Vst3Plugin();
};

