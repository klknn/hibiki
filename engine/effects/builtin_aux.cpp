#include "engine/effects/builtin_aux.hpp"

#include <algorithm>
#include <cmath>

namespace hibiki {

static const std::string kAuxName = "Aux";
static const std::string kAuxPath = "builtin://aux";

BuiltinAux::BuiltinAux() {
  params_[PARAM_GAIN] = 0.7;  // ~0 dB
  params_[PARAM_PAN] = 0.5;   // Center
}

bool BuiltinAux::load(const std::string& /*path*/, int /*plugin_index*/,
                      double sample_rate) {
  sample_rate_ = sample_rate;
  return true;
}

void BuiltinAux::process(float** inputs, float** outputs, int num_samples,
                         const HostProcessContext& /*context*/,
                         const std::vector<MidiNoteEvent>& /*events*/,
                         float** /*sidechain*/) {
  // Map gain: 0.0 → -inf, 0.7 → 0 dB, 1.0 → +6 dB
  // Use cubic curve for natural feel
  float gain_norm = std::clamp((float)params_[PARAM_GAIN], 0.0f, 1.0f);
  float gain_db;
  if (gain_norm <= 0.001f) {
    gain_db = -100.0f;  // effectively -inf
  } else {
    // Map [0,1] to [-60, +6] dB with 0.7 → 0 dB
    gain_db = 20.0f * std::log10(gain_norm / 0.7f) * 2.0f;
    gain_db = std::clamp(gain_db, -60.0f, 6.0f);
  }
  float gain_linear = std::pow(10.0f, gain_db / 20.0f);

  // Constant-power pan
  float pan = std::clamp((float)params_[PARAM_PAN], 0.0f, 1.0f);
  float pan_angle = pan * 1.5707963f;  // 0..pi/2
  float pan_l = std::cos(pan_angle);
  float pan_r = std::sin(pan_angle);

  for (int i = 0; i < num_samples; ++i) {
    float in_l = inputs[0][i];
    float in_r = inputs[1][i];
    outputs[0][i] = in_l * gain_linear * pan_l;
    outputs[1][i] = in_r * gain_linear * pan_r;
  }
}

// --- IPlugin interface ---

int BuiltinAux::getParameterCount() const { return kTotalParams; }

bool BuiltinAux::getParameterInfo(int index, VstParamInfo& info) const {
  if (index < 0 || index >= kTotalParams) return false;
  static const char* names[] = {"Gain", "Pan"};
  static const double defaults[] = {0.7, 0.5};
  info.id = index;
  info.name = names[index];
  info.defaultValue = defaults[index];
  return true;
}

void BuiltinAux::setParameterValue(uint32_t id, double value) {
  if (id < kTotalParams) params_[id] = value;
}

double BuiltinAux::getParameterValue(uint32_t id) const {
  return id < kTotalParams ? params_[id] : 0.0;
}

const std::string& BuiltinAux::getName() const { return kAuxName; }
const std::string& BuiltinAux::getPath() const { return kAuxPath; }
int BuiltinAux::getPluginIndex() const { return 0; }
bool BuiltinAux::isInstrument() const { return false; }

}  // namespace hibiki
