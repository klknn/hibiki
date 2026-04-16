#include "engine/effects/builtin_hott.hpp"

#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace hibiki {

// OTT default band parameters
static constexpr float kDefaultLowDownThresh = -29.9f;
static constexpr float kDefaultMidDownThresh = -16.4f;
static constexpr float kDefaultHighDownThresh = -29.9f;
static constexpr float kDefaultDownRatio = 1000.0f;  // effectively inf:1
static constexpr float kDefaultLowUpThresh = 8.0f;    // dB above which upward stops
static constexpr float kDefaultMidUpThresh = 8.0f;
static constexpr float kDefaultHighUpThresh = 8.0f;
static constexpr float kDefaultUpRatio = 1000.0f;    // strong upward expansion

static constexpr float kDefaultLowAttack = 47.8f;
static constexpr float kDefaultLowRelease = 282.0f;
static constexpr float kDefaultMidAttack = 22.4f;
static constexpr float kDefaultMidRelease = 282.0f;
static constexpr float kDefaultHighAttack = 13.5f;
static constexpr float kDefaultHighRelease = 132.0f;

BuiltinHott::BuiltinHott() { reset(); }

bool BuiltinHott::load(const std::string& /*path*/, int /*plugin_index*/,
                       double sample_rate) {
  sample_rate_ = sample_rate;
  reset();
  return true;
}

void BuiltinHott::process(float** inputs, float** outputs, int num_samples,
                          const HostProcessContext& context,
                          const std::vector<MidiNoteEvent>& /*events*/) {
  if (sample_rate_ != context.sampleRate) {
    sample_rate_ = context.sampleRate;
    updateCrossoverCoeffs();
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
    for (auto& b : bands_) b.gain_reduction_db = 0.0f;
    return;
  }

  float amount = (float)(params_[PARAM_AMOUNT] * 2.0);  // 0..2
  float time_mult = (float)(params_[PARAM_TIME] * 2.0);  // 0..2
  float global_out_db = normToDb24(params_[PARAM_OUTPUT]);
  float global_out_lin = std::pow(10.0f, global_out_db / 20.0f);

  // Per-band output gains
  float band_out_lin[kNumBands];
  band_out_lin[0] = std::pow(10.0f, normToDb24(params_[PARAM_LOW_OUT]) / 20.0f);
  band_out_lin[1] = std::pow(10.0f, normToDb24(params_[PARAM_MID_OUT]) / 20.0f);
  band_out_lin[2] = std::pow(10.0f, normToDb24(params_[PARAM_HIGH_OUT]) / 20.0f);

  // Per-band attack/release coefficients
  float attack_coeff[kNumBands], release_coeff[kNumBands];
  for (int b = 0; b < kNumBands; ++b) {
    float att_ms = bands_[b].attack_ms * time_mult;
    float rel_ms = bands_[b].release_ms * time_mult;
    if (att_ms < 0.01f) att_ms = 0.01f;
    if (rel_ms < 0.1f) rel_ms = 0.1f;
    attack_coeff[b] =
        (float)std::exp(-1.0 / (att_ms * 0.001 * sample_rate_));
    release_coeff[b] =
        (float)std::exp(-1.0 / (rel_ms * 0.001 * sample_rate_));
  }

  updateCrossoverCoeffs();

  float peak_gr[kNumBands] = {0.0f, 0.0f, 0.0f};
  float peak_input_db = -200.0f;
  float peak_output_db = -200.0f;

  for (int i = 0; i < num_samples; ++i) {
    float inL = outL[i];
    float inR = outR[i];

    float in_abs = std::max(std::abs(inL), std::abs(inR));
    float in_db =
        (in_abs > 1e-10f) ? 20.0f * std::log10(in_abs) : -200.0f;
    if (in_db > peak_input_db) peak_input_db = in_db;

    // Split into 3 bands using LR4 crossover
    // Low split: LP1 cascaded, HP1 cascaded
    float lp1_L = processBiquad(lp1_L_[0], lp1_coeffs_, inL);
    lp1_L = processBiquad(lp1_L_[1], lp1_coeffs_, lp1_L);
    float hp1_L = processBiquad(hp1_L_[0], hp1_coeffs_, inL);
    hp1_L = processBiquad(hp1_L_[1], hp1_coeffs_, hp1_L);

    float lp1_R = processBiquad(lp1_R_[0], lp1_coeffs_, inR);
    lp1_R = processBiquad(lp1_R_[1], lp1_coeffs_, lp1_R);
    float hp1_R = processBiquad(hp1_R_[0], hp1_coeffs_, inR);
    hp1_R = processBiquad(hp1_R_[1], hp1_coeffs_, hp1_R);

    // High split on hp1 output: LP2, HP2
    float lp2_L = processBiquad(lp2_L_[0], lp2_coeffs_, hp1_L);
    lp2_L = processBiquad(lp2_L_[1], lp2_coeffs_, lp2_L);
    float hp2_L = processBiquad(hp2_L_[0], hp2_coeffs_, hp1_L);
    hp2_L = processBiquad(hp2_L_[1], hp2_coeffs_, hp2_L);

    float lp2_R = processBiquad(lp2_R_[0], lp2_coeffs_, hp1_R);
    lp2_R = processBiquad(lp2_R_[1], lp2_coeffs_, lp2_R);
    float hp2_R = processBiquad(hp2_R_[0], hp2_coeffs_, hp1_R);
    hp2_R = processBiquad(hp2_R_[1], hp2_coeffs_, hp2_R);

    // Band channels: Low=lp1, Mid=lp2, High=hp2
    float bandL[kNumBands] = {lp1_L, lp2_L, hp2_L};
    float bandR[kNumBands] = {lp1_R, lp2_R, hp2_R};

    float sumL = 0, sumR = 0;
    for (int b = 0; b < kNumBands; ++b) {
      float bL = bandL[b];
      float bR = bandR[b];
      float b_abs = std::max(std::abs(bL), std::abs(bR));
      float b_db =
          (b_abs > 1e-10f) ? 20.0f * std::log10(b_abs) : -200.0f;

      // Compute gain (combined up+down compression)
      float gain_db = computeBandGain(bands_[b], b_db, amount);

      // Smoothed envelope
      if (gain_db < bands_[b].envelope_db) {
        bands_[b].envelope_db =
            attack_coeff[b] * bands_[b].envelope_db +
            (1 - attack_coeff[b]) * gain_db;
      } else {
        bands_[b].envelope_db =
            release_coeff[b] * bands_[b].envelope_db +
            (1 - release_coeff[b]) * gain_db;
      }

      float gain_lin =
          std::pow(10.0f, bands_[b].envelope_db / 20.0f) * band_out_lin[b];

      sumL += bL * gain_lin;
      sumR += bR * gain_lin;

      if (bands_[b].envelope_db < peak_gr[b])
        peak_gr[b] = bands_[b].envelope_db;
    }

    outL[i] = sumL * global_out_lin;
    outR[i] = sumR * global_out_lin;

    float out_abs = std::max(std::abs(outL[i]), std::abs(outR[i]));
    float out_db =
        (out_abs > 1e-10f) ? 20.0f * std::log10(out_abs) : -200.0f;
    if (out_db > peak_output_db) peak_output_db = out_db;
  }

  for (int b = 0; b < kNumBands; ++b) {
    bands_[b].gain_reduction_db = peak_gr[b];
  }
  input_db_.store(peak_input_db, std::memory_order_relaxed);
  output_db_.store(peak_output_db, std::memory_order_relaxed);
}

int BuiltinHott::getParameterCount() const { return kTotalParams; }

bool BuiltinHott::getParameterInfo(int index, VstParamInfo& info) const {
  if (index < 0 || index >= kTotalParams) return false;
  info.id = index;
  static const char* names[] = {"LowXover", "HighXover", "Amount", "Time",
                                "Output",   "LowOut",    "MidOut", "HighOut",
                                "Enable"};
  static const double defaults[] = {0.25, 0.65, 1.0, 1.0, 0.5,
                                    0.62, 0.71, 0.71, 1.0};
  info.name = names[index];
  info.defaultValue = defaults[index];
  return true;
}

void BuiltinHott::setParameterValue(uint32_t id, double value) {
  if (id >= (uint32_t)kTotalParams) return;
  params_[id] = value;
  if (id == PARAM_ENABLE) enabled_ = value >= 0.5;
}

double BuiltinHott::getParameterValue(uint32_t id) const {
  if (id >= (uint32_t)kTotalParams) return 0.0;
  return params_[id];
}

const std::string& BuiltinHott::getName() const {
  static const std::string name = kName;
  return name;
}

const std::string& BuiltinHott::getPath() const {
  static const std::string path = kPath;
  return path;
}

int BuiltinHott::getPluginIndex() const { return 0; }
bool BuiltinHott::isInstrument() const { return false; }

float BuiltinHott::getBandGainReduction(int band) const {
  if (band < 0 || band >= kNumBands) return 0.0f;
  return bands_[band].gain_reduction_db;
}

float BuiltinHott::getInputDb() const {
  return input_db_.load(std::memory_order_relaxed);
}

float BuiltinHott::getOutputDb() const {
  return output_db_.load(std::memory_order_relaxed);
}

// --- Private ---

void BuiltinHott::reset() {
  // Default parameter values
  params_[PARAM_LOW_CROSSOVER] = 0.25;  // ~88 Hz
  params_[PARAM_HIGH_CROSSOVER] = 0.65; // ~2.5 kHz
  params_[PARAM_AMOUNT] = 1.0;          // 100%
  params_[PARAM_TIME] = 1.0;            // 100%
  params_[PARAM_OUTPUT] = 0.5;          // 0 dB
  params_[PARAM_LOW_OUT] = 0.62;        // ~5.7 dB
  params_[PARAM_MID_OUT] = 0.71;        // ~10.3 dB
  params_[PARAM_HIGH_OUT] = 0.71;       // ~10.3 dB
  params_[PARAM_ENABLE] = 1.0;
  enabled_ = true;

  // Band 0: Low
  bands_[0].down_threshold_db = kDefaultLowDownThresh;
  bands_[0].down_ratio = kDefaultDownRatio;
  bands_[0].up_threshold_db = kDefaultLowUpThresh;
  bands_[0].up_ratio = kDefaultUpRatio;
  bands_[0].attack_ms = kDefaultLowAttack;
  bands_[0].release_ms = kDefaultLowRelease;
  bands_[0].envelope_db = 0.0f;
  bands_[0].gain_reduction_db = 0.0f;

  // Band 1: Mid
  bands_[1].down_threshold_db = kDefaultMidDownThresh;
  bands_[1].down_ratio = kDefaultDownRatio;
  bands_[1].up_threshold_db = kDefaultMidUpThresh;
  bands_[1].up_ratio = kDefaultUpRatio;
  bands_[1].attack_ms = kDefaultMidAttack;
  bands_[1].release_ms = kDefaultMidRelease;
  bands_[1].envelope_db = 0.0f;
  bands_[1].gain_reduction_db = 0.0f;

  // Band 2: High
  bands_[2].down_threshold_db = kDefaultHighDownThresh;
  bands_[2].down_ratio = kDefaultDownRatio;
  bands_[2].up_threshold_db = kDefaultHighUpThresh;
  bands_[2].up_ratio = kDefaultUpRatio;
  bands_[2].attack_ms = kDefaultHighAttack;
  bands_[2].release_ms = kDefaultHighRelease;
  bands_[2].envelope_db = 0.0f;
  bands_[2].gain_reduction_db = 0.0f;

  // Clear filter states
  for (int s = 0; s < 2; ++s) {
    lp1_L_[s] = lp1_R_[s] = {};
    hp1_L_[s] = hp1_R_[s] = {};
    lp2_L_[s] = lp2_R_[s] = {};
    hp2_L_[s] = hp2_R_[s] = {};
  }

  updateCrossoverCoeffs();
}

void BuiltinHott::updateCrossoverCoeffs() {
  float low_freq = normToFreq(params_[PARAM_LOW_CROSSOVER], 20.0f, 500.0f);
  float high_freq = normToFreq(params_[PARAM_HIGH_CROSSOVER], 500.0f, 20000.0f);
  lp1_coeffs_ = computeLowpass(low_freq, sample_rate_);
  hp1_coeffs_ = computeHighpass(low_freq, sample_rate_);
  lp2_coeffs_ = computeLowpass(high_freq, sample_rate_);
  hp2_coeffs_ = computeHighpass(high_freq, sample_rate_);
}

BiquadCoeffs BuiltinHott::computeLowpass(float freq, double sr) {
  float w0 = 2.0f * (float)M_PI * freq / (float)sr;
  float cos_w0 = std::cos(w0);
  float sin_w0 = std::sin(w0);
  // Butterworth Q for LR4 cascade = 0.7071 (sqrt(2)/2)
  float alpha = sin_w0 / (2.0f * 0.7071f);

  float a0 = 1.0f + alpha;
  BiquadCoeffs c;
  c.b0 = ((1.0f - cos_w0) / 2.0f) / a0;
  c.b1 = (1.0f - cos_w0) / a0;
  c.b2 = c.b0;
  c.a1 = (-2.0f * cos_w0) / a0;
  c.a2 = (1.0f - alpha) / a0;
  return c;
}

BiquadCoeffs BuiltinHott::computeHighpass(float freq, double sr) {
  float w0 = 2.0f * (float)M_PI * freq / (float)sr;
  float cos_w0 = std::cos(w0);
  float sin_w0 = std::sin(w0);
  float alpha = sin_w0 / (2.0f * 0.7071f);

  float a0 = 1.0f + alpha;
  BiquadCoeffs c;
  c.b0 = ((1.0f + cos_w0) / 2.0f) / a0;
  c.b1 = -(1.0f + cos_w0) / a0;
  c.b2 = c.b0;
  c.a1 = (-2.0f * cos_w0) / a0;
  c.a2 = (1.0f - alpha) / a0;
  return c;
}

float BuiltinHott::processBiquad(BiquadState& state, const BiquadCoeffs& c,
                                 float x) {
  float y = c.b0 * x + c.b1 * state.x1 + c.b2 * state.x2 - c.a1 * state.y1 -
            c.a2 * state.y2;
  state.x2 = state.x1;
  state.x1 = x;
  state.y2 = state.y1;
  state.y1 = y;
  return y;
}

float BuiltinHott::computeBandGain(const BandCompressor& band, float input_db,
                                   float amount) {
  float gain_db = 0.0f;

  // Downward compression: reduce signals above threshold
  if (input_db > band.down_threshold_db) {
    float over = input_db - band.down_threshold_db;
    float target = band.down_threshold_db + over / band.down_ratio;
    gain_db += (target - input_db);  // negative = reduction
  }

  // Upward compression: boost signals below threshold
  if (input_db < band.up_threshold_db && input_db > -100.0f) {
    float under = band.up_threshold_db - input_db;
    float target = band.up_threshold_db - under / band.up_ratio;
    gain_db += (target - input_db);  // positive = boost
  }

  return gain_db * amount;
}

float BuiltinHott::normToFreq(double norm, float min_hz, float max_hz) {
  return min_hz * std::pow(max_hz / min_hz, (float)norm);
}

float BuiltinHott::normToDb24(double norm) {
  return (float)(norm * 48.0 - 24.0);
}

}  // namespace hibiki
