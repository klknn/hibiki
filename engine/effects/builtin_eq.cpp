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
    if (bands_[b].type == OFF) continue;
    const auto& c = coeffs_[b];
    auto& sL = state_[b][0];
    auto& sR = state_[b][1];

    for (int i = 0; i < num_samples; ++i) {
      // Left channel
      float xL = outL[i];
      float yL =
          c.b0 * xL + c.b1 * sL.x1 + c.b2 * sL.x2 - c.a1 * sL.y1 - c.a2 * sL.y2;
      sL.x2 = sL.x1;
      sL.x1 = xL;
      sL.y2 = sL.y1;
      sL.y1 = yL;
      outL[i] = yL;

      // Right channel
      float xR = outR[i];
      float yR =
          c.b0 * xR + c.b1 * sR.x1 + c.b2 * sR.x2 - c.a1 * sR.y1 - c.a2 * sR.y2;
      sR.x2 = sR.x1;
      sR.x1 = xR;
      sR.y2 = sR.y1;
      sR.y1 = yR;
      outR[i] = yR;
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
    if (bands_[b].type == OFF) continue;
    const auto& c = coeffs_[b];
    double num_re = c.b0 + c.b1 * cos_w + c.b2 * cos_2w;
    double num_im = -(c.b1 * sin_w + c.b2 * sin_2w);
    double den_re = 1.0 + c.a1 * cos_w + c.a2 * cos_2w;
    double den_im = -(c.a1 * sin_w + c.a2 * sin_2w);
    double mag_sq = (num_re * num_re + num_im * num_im) /
                    (den_re * den_re + den_im * den_im);
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
    coeffs_[b] = {};
    state_[b][0] = {};
    state_[b][1] = {};
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
  double typeNorm = params_[b];
  if (typeNorm < 0.1)
    bands_[b].type = OFF;
  else if (typeNorm < 0.3)
    bands_[b].type = LPF;
  else if (typeNorm < 0.5)
    bands_[b].type = HPF;
  else if (typeNorm < 0.7)
    bands_[b].type = LOW_SHELF;
  else if (typeNorm < 0.9)
    bands_[b].type = HIGH_SHELF;
  else
    bands_[b].type = BELL;

  bands_[b].freq = normToFreq(params_[b + kNumBands]);
  bands_[b].gain_db =
      (float)(params_[b + kNumBands * 2] - 0.5) * 48.0f;  // ±24 dB
  bands_[b].q = normToQ(params_[b + kNumBands * 3]);
  calcCoeffs(b);
}

void BuiltinEq::recalcAllCoeffs() {
  for (int b = 0; b < kNumBands; ++b) updateBand(b);
}

void BuiltinEq::calcCoeffs(int b) {
  const auto& bp = bands_[b];
  if (bp.type == OFF) {
    coeffs_[b] = {1, 0, 0, 0, 0};
    return;
  }

  double w0 = 2.0 * std::numbers::pi_v<double> * bp.freq / sample_rate_;
  double cos_w0 = std::cos(w0);
  double sin_w0 = std::sin(w0);
  double alpha = sin_w0 / (2.0 * bp.q);
  double A = std::pow(10.0, bp.gain_db / 40.0);

  double b0, b1, b2, a0, a1, a2;

  switch (bp.type) {
    case LPF:
      b0 = (1 - cos_w0) / 2;
      b1 = 1 - cos_w0;
      b2 = (1 - cos_w0) / 2;
      a0 = 1 + alpha;
      a1 = -2 * cos_w0;
      a2 = 1 - alpha;
      break;
    case HPF:
      b0 = (1 + cos_w0) / 2;
      b1 = -(1 + cos_w0);
      b2 = (1 + cos_w0) / 2;
      a0 = 1 + alpha;
      a1 = -2 * cos_w0;
      a2 = 1 - alpha;
      break;
    case LOW_SHELF: {
      double sqA = std::sqrt(A);
      double two_sqA_alpha = 2 * sqA * alpha;
      b0 = A * ((A + 1) - (A - 1) * cos_w0 + two_sqA_alpha);
      b1 = 2 * A * ((A - 1) - (A + 1) * cos_w0);
      b2 = A * ((A + 1) - (A - 1) * cos_w0 - two_sqA_alpha);
      a0 = (A + 1) + (A - 1) * cos_w0 + two_sqA_alpha;
      a1 = -2 * ((A - 1) + (A + 1) * cos_w0);
      a2 = (A + 1) + (A - 1) * cos_w0 - two_sqA_alpha;
      break;
    }
    case HIGH_SHELF: {
      double sqA = std::sqrt(A);
      double two_sqA_alpha = 2 * sqA * alpha;
      b0 = A * ((A + 1) + (A - 1) * cos_w0 + two_sqA_alpha);
      b1 = -2 * A * ((A - 1) + (A + 1) * cos_w0);
      b2 = A * ((A + 1) + (A - 1) * cos_w0 - two_sqA_alpha);
      a0 = (A + 1) - (A - 1) * cos_w0 + two_sqA_alpha;
      a1 = 2 * ((A - 1) - (A + 1) * cos_w0);
      a2 = (A + 1) - (A - 1) * cos_w0 - two_sqA_alpha;
      break;
    }
    case BELL:
    default:
      b0 = 1 + alpha * A;
      b1 = -2 * cos_w0;
      b2 = 1 - alpha * A;
      a0 = 1 + alpha / A;
      a1 = -2 * cos_w0;
      a2 = 1 - alpha / A;
      break;
  }

  coeffs_[b].b0 = (float)(b0 / a0);
  coeffs_[b].b1 = (float)(b1 / a0);
  coeffs_[b].b2 = (float)(b2 / a0);
  coeffs_[b].a1 = (float)(a1 / a0);
  coeffs_[b].a2 = (float)(a2 / a0);
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
