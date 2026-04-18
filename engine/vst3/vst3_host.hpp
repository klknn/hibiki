#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "engine/plugin/iplugin.hpp"

namespace hibiki {

struct Vst3PluginImpl;

class Vst3Plugin : public IPlugin {
 public:
  Vst3Plugin();
  ~Vst3Plugin() override;

  bool load(const std::string& path, int plugin_index = 0,
            double sample_rate = 44100.0) override;
  void showEditor() override;
  void stopEditor() override;
  void process(float** inputs, float** outputs, int num_samples,
               const HostProcessContext& context,
               const std::vector<MidiNoteEvent>& events,
               float** sidechain = nullptr) override;

  int getParameterCount() const override;
  bool getParameterInfo(int index, VstParamInfo& info) const override;
  void setParameterValue(uint32_t id, double valueNormalized) override;
  double getParameterValue(uint32_t id) const override;
  const std::string& getName() const override;
  const std::string& getPath() const override;
  int getPluginIndex() const override;
  bool isInstrument() const override;
  bool captureEditorFrame(std::vector<uint8_t>& rgba, int& w, int& h) override;
  void sendEditorInput(int type, int x, int y, int button, int key_code,
                       int delta) override;

  static std::vector<PluginDescription> listPlugins(const std::string& path);
  static std::vector<PluginDescription> listPluginsIsolated(
      const std::string& path);
  static std::vector<std::string> getDefaultVst3Dirs();
  static void
  runMainLoop();  // For platforms that need a UI loop on the main thread

 private:
  std::unique_ptr<Vst3PluginImpl> impl;
};

}  // namespace hibiki
