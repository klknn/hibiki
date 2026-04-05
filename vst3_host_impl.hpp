#pragma once

#include <atomic>
#include <memory>
#include <thread>

#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "public.sdk/source/vst/hosting/module.h"


namespace hibiki {

struct Vst3PluginImpl {
  VST3::Hosting::Module::Ptr module;
  Steinberg::IPtr<Steinberg::Vst::IComponent> component;
  Steinberg::IPtr<Steinberg::Vst::IAudioProcessor> processor;
  Steinberg::IPtr<Steinberg::Vst::IEditController> controller;
  Steinberg::IPtr<Steinberg::Vst::IHostApplication> hostContext;
  Steinberg::IPtr<Steinberg::IPlugView> view;

  std::string name;
  std::string path;
  int pluginIndex = 0;
  bool isInstrument = false;
  std::thread editorThread;
  std::atomic<bool> editorRunning{false};
  std::atomic<uint64_t> editorWindow{0};
  void* windowDelegate = nullptr;
};

}  // namespace hibiki
