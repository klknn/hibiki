#include "engine/effects/builtin_bitcrusher.hpp"

#include <algorithm>
#include <cmath>

namespace hibiki {

static const std::string kBitcrusherName = "Bitcrusher";
static const std::string kBitcrusherPath = "builtin://bitcrusher";

struct BuiltinBitcrusher::Impl {
  double params[kTotalParams] = {};
  double sample_rate = 44100.0;

  // DSP State
  double phase_accumulator = 0.0;
  float held_l = 0.0f;
  float held_r = 0.0f;

  void reset() {
    phase_accumulator = 0.0;
    held_l = 0.0f;
    held_r = 0.0f;
  }
};

BuiltinBitcrusher::BuiltinBitcrusher() : impl_(std::make_unique<Impl>()) {
  // Defaults: 16-bit, full sample rate, 0dB drive, 100% wet, enabled
  // 16 bits maps to norm: (16 - 1) / 23 = 15/23 ~= 0.652174
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
  return (float)(1.0 + norm * 23.0);
}

float BuiltinBitcrusher::normToSampleRate(double norm,
                                          double host_sample_rate) {
  if (norm >= 1.0) return (float)host_sample_rate;
  // Exponential mapping between 20 Hz and host_sample_rate
  return (float)(20.0 * std::pow(host_sample_rate / 20.0, norm));
}

float BuiltinBitcrusher::normToDriveDb(double norm) {
  return (float)(norm * 24.0);
}

void BuiltinBitcrusher::process(float** inputs, float** outputs,
                                int num_samples,
                                const HostProcessContext& /*context*/,
                                const std::vector<MidiNoteEvent>& /*events*/,
                                float** /*sidechain*/) {
  bool enabled = impl_->params[PARAM_ENABLE] >= 0.5;
  if (!enabled) {
    for (int i = 0; i < num_samples; ++i) {
      outputs[0][i] = inputs[0][i];
      outputs[1][i] = inputs[1][i];
    }
    return;
  }

  float bit_depth = normToBitDepth(impl_->params[PARAM_BIT_DEPTH]);
  float target_sr =
      normToSampleRate(impl_->params[PARAM_SAMPLE_RATE], impl_->sample_rate);
  float drive_db = normToDriveDb(impl_->params[PARAM_DRIVE]);
  float drive_lin = std::pow(10.0f, drive_db / 20.0f);
  float wet = (float)impl_->params[PARAM_WET_DRY];
  float dry = 1.0f - wet;

  float step = std::pow(2.0f, bit_depth - 1.0f);

  for (int i = 0; i < num_samples; ++i) {
    float in_l = inputs[0][i];
    float in_r = inputs[1][i];

    // 1. Pre-amp Drive (Input Amplification)
    float driven_l = in_l * drive_lin;
    float driven_r = in_r * drive_lin;

    // Hard clip driven signal to prevent numerical overflow before quantization
    driven_l = std::clamp(driven_l, -1.0f, 1.0f);
    driven_r = std::clamp(driven_r, -1.0f, 1.0f);

    // 2. Downsampling (Sample and Hold)
    impl_->phase_accumulator += (double)target_sr / impl_->sample_rate;
    if (impl_->phase_accumulator >= 1.0) {
      impl_->phase_accumulator = std::fmod(impl_->phase_accumulator, 1.0);
      impl_->held_l = driven_l;
      impl_->held_r = driven_r;
    }

    // 3. Bit Crushing (Quantization)
    float crushed_l = std::round(impl_->held_l * step) / step;
    float crushed_r = std::round(impl_->held_r * step) / step;

    // 4. Dry/Wet Mix
    outputs[0][i] = crushed_l * wet + in_l * dry;
    outputs[1][i] = crushed_r * wet + in_r * dry;
  }
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
