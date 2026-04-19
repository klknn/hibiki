#include "engine/effects/builtin_eq.hpp"

#include <cmath>
#include <numbers>

namespace hibiki {

BuiltinEq::BuiltinEq() { reset(); }

bool BuiltinEq::load(const std::string& /*path*/, int /*plugin_index*/,
                     double sample_rate) {
  sample_rate_ = sample_rate;
  reset();
  return true;
}

void BuiltinEq::process(float** inputs, float** outputs, int num_samples,
                        const HostProcessContext& context,
                        const std::vector<MidiNoteEvent>& /*events*/,
                        float** /*sidechain*/) {
  if (sample_rate_ != context.sampleRate) {
    sample_rate_ = context.sampleRate;
    recalcAllCoeffs();
    initSpectrumBins();
  }

  float* outL = outputs[0];
  float* outR = outputs[1];
  if (inputs && inputs != outputs) {
    for (int i = 0; i < num_samples; ++i) {
      outL[i] = inputs[0][i];
      outR[i] = inputs[1][i];
    }
  }

  // Accumulate input spectrum (before EQ processing)
  spectrum_analyzer_.accumulateToRing(outL, outR, num_samples, input_ring_);

  if (!enabled_) {
    spectrum_analyzer_.accumulateToRing(outL, outR, num_samples, output_ring_);
    spectrum_analyzer_.advanceRingPos(num_samples);
    spectrum_sample_count_ += num_samples;
    maybeUpdateSpectrum();
    return;
  }

  // Process each band
  for (int b = 0; b < kNumBands; ++b) {
    if (bands_[b].type == BiquadFilter::Type::OFF) continue;

    for (int i = 0; i < num_samples; ++i) {
      outL[i] = filters_[b][0].process(outL[i]);
      outR[i] = filters_[b][1].process(outR[i]);
    }
  }

  // Accumulate output spectrum (after EQ processing)
  spectrum_analyzer_.accumulateToRing(outL, outR, num_samples, output_ring_);
  spectrum_analyzer_.advanceRingPos(num_samples);
  spectrum_sample_count_ += num_samples;
  maybeUpdateSpectrum();
}

int BuiltinEq::getParameterCount() const { return kTotalParams; }

bool BuiltinEq::getParameterInfo(int index, VstParamInfo& info) const {
  if (index < 0 || index >= kTotalParams) return false;
  info.id = index;
  if (index < kNumBands) {
    info.name = "Band " + std::to_string(index + 1) + " Type";
    info.defaultValue = 0.0;  // OFF
  } else if (index < kNumBands * 2) {
    int b = index - kNumBands;
    info.name = "Band " + std::to_string(b + 1) + " Freq";
    info.defaultValue = freqToNorm(defaultFreqs_[b]);
  } else if (index < kNumBands * 3) {
    int b = index - kNumBands * 2;
    info.name = "Band " + std::to_string(b + 1) + " Gain";
    info.defaultValue = 0.5;  // 0 dB
  } else if (index < kNumBands * 4) {
    int b = index - kNumBands * 3;
    info.name = "Band " + std::to_string(b + 1) + " Q";
    info.defaultValue = qToNorm(0.707);
  } else {
    info.name = "Enable";
    info.defaultValue = 1.0;
  }
  return true;
}

void BuiltinEq::setParameterValue(uint32_t id, double value) {
  int idx = (int)id;
  if (idx < 0 || idx >= kTotalParams) return;
  params_[idx] = value;

  if (idx == kNumBands * kParamsPerBand) {
    enabled_ = value >= 0.5;
    return;
  }

  int band = idx % kNumBands;
  updateBand(band);
}

double BuiltinEq::getParameterValue(uint32_t id) const {
  int idx = (int)id;
  if (idx < 0 || idx >= kTotalParams) return 0.0;
  return params_[idx];
}

const std::string& BuiltinEq::getName() const {
  static const std::string name = kName;
  return name;
}

const std::string& BuiltinEq::getPath() const {
  static const std::string path = kPath;
  return path;
}

int BuiltinEq::getPluginIndex() const { return 0; }
bool BuiltinEq::isInstrument() const { return false; }

float BuiltinEq::getMagnitudeDb(float freq) const {
  if (!enabled_) return 0.0f;
  float total_db = 0.0f;
  double w = 2.0 * std::numbers::pi_v<double> * freq / sample_rate_;
  double cos_w = std::cos(w);
  double cos_2w = std::cos(2.0 * w);
  double sin_w = std::sin(w);
  double sin_2w = std::sin(2.0 * w);

  for (int b = 0; b < kNumBands; ++b) {
    if (bands_[b].type == BiquadFilter::Type::OFF) continue;
    double mag_sq = filters_[b][0].getMagnitudeSq(cos_w, cos_2w, sin_w, sin_2w);
    if (mag_sq > 0) total_db += 10.0f * std::log10(mag_sq);
  }
  return total_db;
}

BuiltinEq::SpectrumData BuiltinEq::getSpectrumData() const {
  SpectrumData d;
  for (int i = 0; i < kSpectrumBins; ++i) {
    d.input_db[i] = spectrum_input_db_[i].load(std::memory_order_relaxed);
    d.output_db[i] = spectrum_output_db_[i].load(std::memory_order_relaxed);
  }
  return d;
}

// --- Private methods ---

void BuiltinEq::reset() {
  for (int b = 0; b < kNumBands; ++b) {
    bands_[b] = {};
    bands_[b].freq = defaultFreqs_[b];
    filters_[b][0].reset();
    filters_[b][1].reset();
  }
  for (int b = 0; b < kNumBands; ++b) {
    params_[b] = 0.0;  // type = OFF
    params_[b + kNumBands] = freqToNorm(defaultFreqs_[b]);
    params_[b + kNumBands * 2] = 0.5;  // 0 dB
    params_[b + kNumBands * 3] = qToNorm(0.707);
  }
  params_[kNumBands * kParamsPerBand] = 1.0;  // enabled
  enabled_ = true;
  initSpectrumBins();
}

void BuiltinEq::updateBand(int b) {
  bands_[b].type = BiquadFilter::normToTypeV2(params_[b]);
  bands_[b].freq = normToFreq(params_[b + kNumBands]);
  bands_[b].gain_db =
      (float)(params_[b + kNumBands * 2] - 0.5) * 48.0f;  // ±24 dB

  filters_[b][0].setParams(bands_[b].type, bands_[b].freq, bands_[b].q,
                           bands_[b].gain_db, (float)sample_rate_);
  filters_[b][1].setParams(bands_[b].type, bands_[b].freq, bands_[b].q,
                           bands_[b].gain_db, (float)sample_rate_);
}

void BuiltinEq::recalcAllCoeffs() {
  for (int b = 0; b < kNumBands; ++b) updateBand(b);
}

float BuiltinEq::normToFreq(double norm) {
  return (float)(20.0 * std::pow(1000.0, norm));
}

double BuiltinEq::freqToNorm(float freq) {
  return std::log(freq / 20.0) / std::log(1000.0);
}

float BuiltinEq::normToQ(double norm) {
  return (float)(0.1 * std::pow(180.0, norm));
}

double BuiltinEq::qToNorm(float q) {
  return std::log(q / 0.1) / std::log(180.0);
}

void BuiltinEq::initSpectrumBins() {
  spectrum_analyzer_.init(kSpectrumBins, (float)sample_rate_);
  spectrum_sample_count_ = 0;
  for (int i = 0; i < kSpectrumBins; ++i) {
    spectrum_input_db_[i].store(-100.0f, std::memory_order_relaxed);
    spectrum_output_db_[i].store(-100.0f, std::memory_order_relaxed);
  }
}

void BuiltinEq::maybeUpdateSpectrum() {
  if (spectrum_sample_count_ < kFftSize) return;
  spectrum_analyzer_.computeSpectrum(input_ring_, spectrum_input_db_);
  spectrum_analyzer_.computeSpectrum(output_ring_, spectrum_output_db_);
  spectrum_sample_count_ = 0;
}

}  // namespace hibiki
