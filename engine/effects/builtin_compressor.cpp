#include "engine/effects/builtin_compressor.hpp"

#include <cmath>

namespace hibiki {

BuiltinCompressor::BuiltinCompressor() { reset(); }

bool BuiltinCompressor::load(const std::string& /*path*/, int /*plugin_index*/,
                             double sample_rate) {
  sample_rate_ = sample_rate;
  reset();
  return true;
}

void BuiltinCompressor::process(float** inputs, float** outputs,
                                int num_samples,
                                const HostProcessContext& context,
                                const std::vector<MidiNoteEvent>& /*events*/,
                                float** sidechain) {
  if (sample_rate_ != context.sampleRate) {
    sample_rate_ = context.sampleRate;
  }

  const float* inL = inputs ? inputs[0] : nullptr;
  const float* inR = inputs ? inputs[1] : nullptr;
  float* outL = outputs[0];
  float* outR = outputs[1];
  const float* scL = (sidechain && sidechain[0]) ? sidechain[0] : nullptr;
  const float* scR = (sidechain && sidechain[1]) ? sidechain[1] : nullptr;

  d_compressor_process(
      dsp_state_, inL, inR, outL, outR, num_samples, sample_rate_,
      params_[PARAM_THRESHOLD], params_[PARAM_RATIO], params_[PARAM_ATTACK],
      params_[PARAM_RELEASE], params_[PARAM_KNEE], params_[PARAM_MAKEUP],
      params_[PARAM_UP_THRESHOLD], params_[PARAM_UP_RATIO],
      params_[PARAM_RMS_MODE] >= 0.5, enabled_, scL, scR);
}

int BuiltinCompressor::getParameterCount() const { return kTotalParams; }

bool BuiltinCompressor::getParameterInfo(int index, VstParamInfo& info) const {
  if (index < 0 || index >= kTotalParams) return false;
  info.id = index;
  static const char* names[] = {"Threshold", "Ratio",  "Attack", "Release",
                                "Knee",      "Makeup", "Enable", "UpThresh",
                                "UpRatio",   "RmsMode"};
  static const double defaults[] = {1.0, 0.0, 0.3, 0.3, 0.0,
                                    0.0, 1.0, 0.0, 0.0, 0.0};
  info.name = names[index];
  info.defaultValue = defaults[index];
  return true;
}

void BuiltinCompressor::setParameterValue(uint32_t id, double value) {
  if (id >= kTotalParams) return;
  params_[id] = value;
  if (id == PARAM_ENABLE) enabled_ = value >= 0.5;
}

double BuiltinCompressor::getParameterValue(uint32_t id) const {
  if (id >= kTotalParams) return 0.0;
  return params_[id];
}

const std::string& BuiltinCompressor::getName() const {
  static const std::string name = kName;
  return name;
}

const std::string& BuiltinCompressor::getPath() const {
  static const std::string path = kPath;
  return path;
}

int BuiltinCompressor::getPluginIndex() const { return 0; }
bool BuiltinCompressor::isInstrument() const { return false; }

float BuiltinCompressor::getGainReductionDb() const {
  return dsp_state_.gain_reduction_db;
}

float BuiltinCompressor::getInputDb() const { return dsp_state_.input_db; }

float BuiltinCompressor::getOutputDb() const { return dsp_state_.output_db; }

float BuiltinCompressor::getSidechainDb() const {
  return dsp_state_.sidechain_db;
}

float BuiltinCompressor::computeOutputDb(float input_db) const {
  float threshold = normToThreshold(params_[PARAM_THRESHOLD]);
  float ratio = normToRatio(params_[PARAM_RATIO]);
  float knee_db = (float)(params_[PARAM_KNEE] * 30.0);
  float up_threshold = normToUpThreshold(params_[PARAM_UP_THRESHOLD]);
  float up_ratio = normToRatio(params_[PARAM_UP_RATIO]);
  float gr = d_compressor_compute_gain_reduction(
      input_db, threshold, ratio, knee_db, up_threshold, up_ratio);
  return input_db + gr;
}

// --- Private ---

void BuiltinCompressor::reset() {
  params_[PARAM_THRESHOLD] = 1.0;
  params_[PARAM_RATIO] = 0.0;
  params_[PARAM_ATTACK] = 0.3;
  params_[PARAM_RELEASE] = 0.3;
  params_[PARAM_KNEE] = 0.0;
  params_[PARAM_MAKEUP] = 0.0;
  params_[PARAM_ENABLE] = 1.0;
  params_[PARAM_UP_THRESHOLD] = 0.0;  // -60 dB (effectively off)
  params_[PARAM_UP_RATIO] = 0.0;      // 1:1 (no upward comp)
  params_[PARAM_RMS_MODE] = 0.0;      // Peak detection by default
  enabled_ = true;
  dsp_state_.reset();
}

float BuiltinCompressor::normToThreshold(double norm) {
  return d_compressor_norm_to_threshold(norm);
}

float BuiltinCompressor::normToRatio(double norm) {
  return d_compressor_norm_to_ratio(norm);
}

float BuiltinCompressor::normToAttack(double norm) {
  return d_compressor_norm_to_attack(norm);
}

float BuiltinCompressor::normToRelease(double norm) {
  return d_compressor_norm_to_release(norm);
}

float BuiltinCompressor::normToUpThreshold(double norm) {
  return d_compressor_norm_to_up_threshold(norm);
}

}  // namespace hibiki
