#pragma once

#include <memory>
#include <string>
#include <vector>

#include "engine/plugin/iplugin.hpp"

namespace hibiki {

struct PhaserDspState;

PhaserDspState* d_phaser_create();
void d_phaser_destroy(PhaserDspState* state);
void d_phaser_reset(PhaserDspState* state);
void d_phaser_set_parameter(PhaserDspState* state, uint32_t id, double value);
double d_phaser_get_parameter(const PhaserDspState* state, uint32_t id);
float d_phaser_get_input_db(const PhaserDspState* state);
float d_phaser_get_output_db(const PhaserDspState* state);
void d_phaser_get_scope_data(const PhaserDspState* state, float* left,
                             float* right, int size);
void d_phaser_process(PhaserDspState* state, const float* in_l,
                      const float* in_r, float* out_l, float* out_r,
                      int num_samples, double sample_rate, double tempo);
float d_phaser_norm_to_rate(double norm);
int d_phaser_norm_to_stages(double norm);
int d_phaser_norm_to_mode(double norm);

// Multi-mode allpass-based phase effect (Phaser / Chorus / Flanger /
// Ring-Mod / Disperser) with Lissajous scope output.
// Path: builtin://phaser
class BuiltinPhaser : public IPlugin {
 public:
  static constexpr int kTotalParams = 12;
  static constexpr const char* kPath = "builtin://phaser";
  static constexpr const char* kName = "Phaser";
  static constexpr int kMaxStages = 12;
  static constexpr int kScopeSize = 256;
  static constexpr int kMaxCombDelay = 4096;

  enum ParamId {
    PARAM_RATE = 0,
    PARAM_DEPTH = 1,
    PARAM_FEEDBACK = 2,
    PARAM_STAGES = 3,
    PARAM_MIX = 4,
    PARAM_STEREO = 5,
    PARAM_MODE = 6,
    PARAM_LFO_SHAPE = 7,
    PARAM_CENTER = 8,
    PARAM_SPREAD = 9,
    PARAM_SYNC = 10,
    PARAM_ENABLE = 11,
  };

  enum Mode {
    MODE_PHASER = 0,
    MODE_CHORUS = 1,
    MODE_FLANGER = 2,
    MODE_RINGMOD = 3,
    MODE_DISPERSER = 4,
  };

  BuiltinPhaser();
  ~BuiltinPhaser() override;

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

  // Scope data for Lissajous display (call from notification thread)
  void getScopeData(float* left, float* right, int size) const;

  // Parameter mapping
  static float normToRate(double norm);
  static int normToStages(double norm);
  static int normToMode(double norm);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;

  void reset();
};

}  // namespace hibiki
