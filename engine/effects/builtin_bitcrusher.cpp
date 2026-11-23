#include "engine/effects/builtin_bitcrusher.hpp"

#include <algorithm>

namespace hibiki {

static const std::string kBitcrusherName = "Bitcrusher";
static const std::string kBitcrusherPath = "builtin://bitcrusher";

struct BitcrusherDspState {
  double phase_accumulator = 0.0;
  float held_l = 0.0f;
  float held_r = 0.0f;
  void reset();
};

extern float d_bitcrusher_norm_to_bit_depth(double norm);
extern float d_bitcrusher_norm_to_sample_rate(double norm,
                                              double host_sample_rate);
extern float d_bitcrusher_norm_to_drive_db(double norm);
extern void d_bitcrusher_process(BitcrusherDspState& state, const float* in_l,
                                 const float* in_r, float* out_l, float* out_r,
                                 int num_samples, double sample_rate,
                                 double bit_depth_norm, double sample_rate_norm,
                                 double drive_norm, double wet_dry_norm,
                                 bool enabled);

struct BuiltinBitcrusher::Impl {
  double params[kTotalParams] = {};
  double sample_rate = 44100.0;
  BitcrusherDspState dsp_state;

  void reset() { dsp_state.reset(); }
};

BuiltinBitcrusher::BuiltinBitcrusher() : impl_(std::make_unique<Impl>()) {
  impl_->params[PARAM_BIT_DEPTH] = 0.652174;
  impl_->params[PARAM_SAMPLE_RATE] = 1.0;
  impl_->params[PARAM_DRIVE] = 0.0;
  impl_->params[PARAM_WET_DRY] = 1.0;
  impl_->params[PARAM_ENABLE] = 1.0;
  impl_->reset();
}

BuiltinBitcrusher::~BuiltinBitcrusher() = default;

bool BuiltinBitcrusher::load(const std::string& /*path*/, int /*plugin_index*/,
                             double sample_rate) {
  impl_->sample_rate = sample_rate;
  impl_->reset();
  return true;
}

float BuiltinBitcrusher::normToBitDepth(double norm) {
  return d_bitcrusher_norm_to_bit_depth(norm);
}

float BuiltinBitcrusher::normToSampleRate(double norm,
                                          double host_sample_rate) {
  return d_bitcrusher_norm_to_sample_rate(norm, host_sample_rate);
}

float BuiltinBitcrusher::normToDriveDb(double norm) {
  return d_bitcrusher_norm_to_drive_db(norm);
}

void BuiltinBitcrusher::process(float** inputs, float** outputs,
                                int num_samples,
                                const HostProcessContext& /*context*/,
                                const std::vector<MidiNoteEvent>& /*events*/,
                                float** /*sidechain*/) {
  bool enabled = impl_->params[PARAM_ENABLE] >= 0.5;
  d_bitcrusher_process(
      impl_->dsp_state, inputs[0], inputs[1], outputs[0], outputs[1],
      num_samples, impl_->sample_rate, impl_->params[PARAM_BIT_DEPTH],
      impl_->params[PARAM_SAMPLE_RATE], impl_->params[PARAM_DRIVE],
      impl_->params[PARAM_WET_DRY], enabled);
}

int BuiltinBitcrusher::getParameterCount() const { return kTotalParams; }

bool BuiltinBitcrusher::getParameterInfo(int index, VstParamInfo& info) const {
  if (index < 0 || index >= kTotalParams) return false;
  static const char* names[] = {"Bit Depth", "Sample Rate", "Drive", "Mix",
                                "Enable"};
  static const double defaults[] = {0.652174, 1.0, 0.0, 1.0, 1.0};
  info.id = index;
  info.name = names[index];
  info.defaultValue = defaults[index];
  return true;
}

void BuiltinBitcrusher::setParameterValue(uint32_t id, double value) {
  if (id < kTotalParams) {
    impl_->params[id] = std::clamp(value, 0.0, 1.0);
  }
}

double BuiltinBitcrusher::getParameterValue(uint32_t id) const {
  if (id < kTotalParams) {
    return impl_->params[id];
  }
  return 0.0;
}

const std::string& BuiltinBitcrusher::getName() const {
  return kBitcrusherName;
}

const std::string& BuiltinBitcrusher::getPath() const {
  return kBitcrusherPath;
}

int BuiltinBitcrusher::getPluginIndex() const { return 0; }

bool BuiltinBitcrusher::isInstrument() const { return false; }

}  // namespace hibiki
