#pragma once

#include <cstdint>
#include <string>
#include <vector>

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
  bool is_instrument = false;
};

// Abstract interface for audio plugins.
// Implemented by Vst3Plugin (in-process) and PluginProxy (out-of-process).
// Uses raw pointers for ABI compatibility across process boundaries.
class IPlugin {
 public:
  virtual ~IPlugin() = default;

  virtual bool load(const std::string& path, int plugin_index = 0,
                    double sample_rate = 44100.0) = 0;
  virtual void showEditor() = 0;
  virtual void stopEditor() = 0;
  virtual void process(float** inputs, float** outputs, int num_samples,
                       const HostProcessContext& context,
                       const std::vector<MidiNoteEvent>& events,
                       float** sidechain = nullptr) = 0;

  virtual int getParameterCount() const = 0;
  virtual bool getParameterInfo(int index, VstParamInfo& info) const = 0;
  virtual void setParameterValue(uint32_t id, double valueNormalized) = 0;
  virtual double getParameterValue(uint32_t id) const = 0;
  virtual const std::string& getName() const = 0;
  virtual const std::string& getPath() const = 0;
  virtual int getPluginIndex() const = 0;
  virtual bool isInstrument() const = 0;

  // Editor framebuffer capture (for remote GUI forwarding).
  // Returns raw BGRA pixel data in `rgba`, sets width/height.
  virtual bool captureEditorFrame(std::vector<uint8_t>& /*rgba*/, int& /*w*/,
                                  int& /*h*/) {
    return false;
  }
  // Forward mouse/keyboard input to the editor window.
  virtual void sendEditorInput(int /*type*/, int /*x*/, int /*y*/,
                               int /*button*/, int /*key_code*/,
                               int /*delta*/) {}
};

}  // namespace hibiki
