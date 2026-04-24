#pragma once

#include <atomic>
#include <string>
#include <vector>

#include "engine/plugin/iplugin.hpp"

namespace hibiki {

// Tempo-synced envelope shaper (Kickstart / LFOTool style).
// Users define a gain or filter envelope curve with up to 16 breakpoints
// that loops every N beats. Supports linear and catmull-rom interpolation.
// Path: builtin://envelope_shaper
class BuiltinEnvelopeShaper : public IPlugin {
 public:
  static constexpr int kMaxPoints = 16;
  // 5 control params + 16 point Y-values + 1 num-points = 22
  static constexpr int kTotalParams = 22;
  static constexpr const char* kPath = "builtin://envelope_shaper";
  static constexpr const char* kName = "EnvShaper";

  enum ParamId {
    PARAM_MIX = 0,
    PARAM_RATE = 1,    // Beat division (0–1, maps to div table)
    PARAM_SMOOTH = 2,  // 0=linear, 1=max catmull-rom smoothing
    PARAM_MODE = 3,    // 0=gain, 0.33=HPF, 0.5=BPF, 1.0=LPF
    PARAM_ENABLE = 4,
    PARAM_POINT_Y_0 = 5,  // Point Y values (5..20)
    // PARAM_POINT_Y_1 = 6, ... PARAM_POINT_Y_15 = 20
    PARAM_NUM_POINTS = 21,
  };

  BuiltinEnvelopeShaper();

  // IPlugin interface
  bool load(const std::string& path, int plugin_index = 0,
            double sample_rate = 44100.0) override;
  void showEditor() override {}
  void stopEditor() override {}
  void process(float** inputs, float** outputs, int num_samples,
               const HostProcessContext& context,
               const std::vector<MidiNoteEvent>& events,
               float** sidechain = nullptr) override;
  int getParameterCount() const override;
  bool getParameterInfo(int index, VstParamInfo& info) const override;
  void setParameterValue(uint32_t id, double value) override;
  double getParameterValue(uint32_t id) const override;
  const std::string& getName() const override;
  const std::string& getPath() const override;
  int getPluginIndex() const override;
  bool isInstrument() const override;

  // Metering
  float getInputDb() const;
  float getOutputDb() const;
  // Current envelope phase (0–1) for cursor display
  float getEnvelopePhase() const;

  // Rate mapping (public for UI)
  static float normToRateBeats(double norm);
  static const char* normToRateLabel(double norm);

 private:
  double params_[kTotalParams] = {};
  double sample_rate_ = 44100.0;
  bool enabled_ = true;

  // Envelope state
  double phase_ = 0.0;  // 0–1 within current cycle

  // LP filter state for cutoff mode
  float lp_state_l_ = 0.0f;
  float lp_state_r_ = 0.0f;
  // HP filter state
  float hp_state_l_ = 0.0f;
  float hp_state_r_ = 0.0f;

  // Metering
  std::atomic<float> input_rms_{0.0f};
  std::atomic<float> output_rms_{0.0f};
  std::atomic<float> current_phase_{0.0f};

  // Get the number of active points (2–16)
  int getNumPoints() const;
  // Evaluate the envelope at phase t (0–1)
  float evaluateCurve(float t) const;
  // Catmull-rom interpolation
  static float catmullRom(float p0, float p1, float p2, float p3, float t);

  void reset();
};

}  // namespace hibiki
