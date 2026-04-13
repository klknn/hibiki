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
                                const std::vector<MidiNoteEvent>& /*events*/) {
  if (sample_rate_ != context.sampleRate) {
    sample_rate_ = context.sampleRate;
  }

  float* outL = outputs[0];
  float* outR = outputs[1];
  if (inputs && inputs != outputs) {
    for (int i = 0; i < num_samples; ++i) {
      outL[i] = inputs[0][i];
      outR[i] = inputs[1][i];
    }
  }

  if (!enabled_) {
    gain_reduction_db_ = 0.0f;
    return;
  }

  float threshold = normToThreshold(params_[PARAM_THRESHOLD]);
  float ratio = normToRatio(params_[PARAM_RATIO]);
  float attack_ms = normToAttack(params_[PARAM_ATTACK]);
  float release_ms = normToRelease(params_[PARAM_RELEASE]);
  float knee_db = (float)(params_[PARAM_KNEE] * 30.0);
  float makeup_lin = (float)std::pow(10.0, params_[PARAM_MAKEUP] * 30.0 / 20.0);

  float attack_coeff =
      (float)std::exp(-1.0 / (attack_ms * 0.001 * sample_rate_));
  float release_coeff =
      (float)std::exp(-1.0 / (release_ms * 0.001 * sample_rate_));

  float peak_gr = 0.0f;
  float peak_input_db = -200.0f;
  float peak_output_db = -200.0f;

  for (int i = 0; i < num_samples; ++i) {
    float input_abs = std::max(std::abs(outL[i]), std::abs(outR[i]));
    float input_db =
        (input_abs > 1e-10f) ? 20.0f * std::log10(input_abs) : -200.0f;
    if (input_db > peak_input_db) peak_input_db = input_db;

    float gr_db = computeGainReduction(input_db, threshold, ratio, knee_db);

    if (gr_db < envelope_db_) {
      envelope_db_ = attack_coeff * envelope_db_ + (1 - attack_coeff) * gr_db;
    } else {
      envelope_db_ = release_coeff * envelope_db_ + (1 - release_coeff) * gr_db;
    }

    float gain = (float)std::pow(10.0, envelope_db_ / 20.0) * makeup_lin;
    outL[i] *= gain;
    outR[i] *= gain;

    float output_abs = std::max(std::abs(outL[i]), std::abs(outR[i]));
    float output_db =
        (output_abs > 1e-10f) ? 20.0f * std::log10(output_abs) : -200.0f;
    if (output_db > peak_output_db) peak_output_db = output_db;

    if (envelope_db_ < peak_gr) peak_gr = envelope_db_;
  }

  gain_reduction_db_ = peak_gr;
  input_db_.store(peak_input_db, std::memory_order_relaxed);
  output_db_.store(peak_output_db, std::memory_order_relaxed);
}

int BuiltinCompressor::getParameterCount() const { return kTotalParams; }

bool BuiltinCompressor::getParameterInfo(int index, VstParamInfo& info) const {
  if (index < 0 || index >= kTotalParams) return false;
  info.id = index;
  static const char* names[] = {"Threshold", "Ratio",  "Attack", "Release",
                                "Knee",      "Makeup", "Enable"};
  static const double defaults[] = {1.0, 0.0, 0.3, 0.3, 0.0, 0.0, 1.0};
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
  return gain_reduction_db_;
}

float BuiltinCompressor::getInputDb() const {
  return input_db_.load(std::memory_order_relaxed);
}

float BuiltinCompressor::getOutputDb() const {
  return output_db_.load(std::memory_order_relaxed);
}

float BuiltinCompressor::computeOutputDb(float input_db) const {
  float threshold = normToThreshold(params_[PARAM_THRESHOLD]);
  float ratio = normToRatio(params_[PARAM_RATIO]);
  float knee_db = (float)(params_[PARAM_KNEE] * 30.0);
  float gr = computeGainReduction(input_db, threshold, ratio, knee_db);
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
  enabled_ = true;
  envelope_db_ = 0.0f;
  gain_reduction_db_ = 0.0f;
}

float BuiltinCompressor::computeGainReduction(float input_db, float threshold,
                                              float ratio, float knee_db) {
  float half_knee = knee_db / 2.0f;
  float gr_db = 0.0f;

  if (knee_db <= 0.01f) {
    if (input_db > threshold) {
      gr_db = (threshold - input_db) * (1.0f - 1.0f / ratio);
    }
  } else {
    float lower = threshold - half_knee;
    float upper = threshold + half_knee;
    if (input_db <= lower) {
      gr_db = 0.0f;
    } else if (input_db >= upper) {
      gr_db = (threshold - input_db) * (1.0f - 1.0f / ratio);
    } else {
      float x = input_db - lower;
      gr_db = -(1.0f - 1.0f / ratio) * x * x / (2.0f * knee_db);
    }
  }
  return gr_db;
}

float BuiltinCompressor::normToThreshold(double norm) {
  return (float)(norm * 60.0 - 60.0);
}

float BuiltinCompressor::normToRatio(double norm) {
  if (norm >= 0.999) return 1000.0f;
  return 1.0f / (1.0f - (float)norm);
}

float BuiltinCompressor::normToAttack(double norm) {
  return (float)(0.1 * std::pow(1000.0, norm));
}

float BuiltinCompressor::normToRelease(double norm) {
  return (float)(10.0 * std::pow(100.0, norm));
}

}  // namespace hibiki
