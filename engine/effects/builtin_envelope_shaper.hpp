#pragma once

#include <memory>
#include <string>
#include <vector>

#include "engine/plugin/iplugin.hpp"

namespace hibiki {

struct EnvShaperDspState;

EnvShaperDspState* d_env_shaper_create();
void d_env_shaper_destroy(EnvShaperDspState* state);
void d_env_shaper_reset(EnvShaperDspState* state);
void d_env_shaper_set_parameter(EnvShaperDspState* state, uint32_t id,
                                double value);
double d_env_shaper_get_parameter(const EnvShaperDspState* state, uint32_t id);
void d_env_shaper_process(EnvShaperDspState* state, const float* in_l,
                          const float* in_r, float* out_l, float* out_r,
                          int num_samples, double sample_rate, double tempo,
                          double project_time_music);
float d_env_shaper_norm_to_rate_beats(double norm);
const char* d_env_shaper_norm_to_rate_label(double norm);
float d_env_shaper_get_input_db(const EnvShaperDspState* state);
float d_env_shaper_get_output_db(const EnvShaperDspState* state);
float d_env_shaper_get_current_phase(const EnvShaperDspState* state);

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
  ~BuiltinEnvelopeShaper() override;

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
  struct Impl;
  std::unique_ptr<Impl> impl_;

  void reset();
};

}  // namespace hibiki
