#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "iplugin.hpp"


namespace hibiki {

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
  float velocity;  // 0.0 - 1.0
  bool isNoteOn;
};

struct VstParamInfo {
  uint32_t id;
  std::string name;
  double defaultValue;
};

struct PluginDescription {
  int index;
  std::string name;
  std::string vendor;
};

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
               const std::vector<MidiNoteEvent>& events) override;

  int getParameterCount() const override;
  bool getParameterInfo(int index, VstParamInfo& info) const override;
  void setParameterValue(uint32_t id, double valueNormalized) override;
  double getParameterValue(uint32_t id) const override;
  const std::string& getName() const override;
  const std::string& getPath() const override;
  int getPluginIndex() const override;
  bool isInstrument() const override;

  static std::vector<PluginDescription> listPlugins(const std::string& path);
  static std::vector<PluginDescription> listPluginsIsolated(
      const std::string& path);
  static void
  runMainLoop();  // For platforms that need a UI loop on the main thread

 private:
  std::unique_ptr<Vst3PluginImpl> impl;
};

}  // namespace hibiki
