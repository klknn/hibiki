#include "engine/effects/builtin_hott.hpp"

#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace hibiki {

// Xfer OTT (Over The Top) default per-band compressor parameters.
// Reference: https://www.makou.com/available-xfer-ott-parameters/
//
// Normalization formulas (matching BuiltinCompressor):
//   Threshold norm = (threshold_db + 60) / 60
//   Ratio norm:  ratio = 1/(1-norm), so norm = 1 - 1/ratio
//   Attack norm: attack_ms = 0.1 * 1000^norm, so norm = log10(attack_ms/0.1) /
//   3 Release norm: release_ms = 10 * 100^norm, so norm = log10(release_ms/10)
//   / 2
//
// Band defaults (from Ableton Live OTT.adv / Xfer OTT):
//   [T]ime — per-band attack/release:
//     High: 13.5ms / 132ms
//     Mid:  22.4ms / 282ms
//     Low:  47.8ms / 282ms
//   [A]bove — downward compression (threshold, ratio):
//     High: -35.5dB, inf:1
//     Mid:  -30.2dB, 66.7:1
//     Low:  -33.8dB, 66.7:1
//   [B]elow — upward compression (threshold, ratio):
//     High: -40.8dB, 4.17:1
//     Mid:  -41.8dB, 4.17:1
//     Low:  -40.8dB, 4.17:1
//   Input gains:  all 5.2 dB
//   Output gains: High 10.3dB, Mid 5.7dB, Low 10.3dB
//   Split freq:   Low 88.3Hz, High 2.50kHz

struct BandDefaults {
  double threshold_norm;
  double ratio_norm;
  double attack_norm;
  double release_norm;
};

static BandDefaults computeBandDefaults(float threshold_db, float ratio,
                                        float attack_ms, float release_ms) {
  BandDefaults d;
  d.threshold_norm = (threshold_db + 60.0) / 60.0;
  d.ratio_norm = (ratio >= 999.0f) ? 0.999 : (1.0 - 1.0 / ratio);
  d.attack_norm = std::log10(attack_ms / 0.1) / 3.0;
  d.release_norm = std::log10(release_ms / 10.0) / 2.0;
  // Clamp to [0, 1]
  d.threshold_norm = std::max(0.0, std::min(1.0, d.threshold_norm));
  d.ratio_norm = std::max(0.0, std::min(1.0, d.ratio_norm));
  d.attack_norm = std::max(0.0, std::min(1.0, d.attack_norm));
  d.release_norm = std::max(0.0, std::min(1.0, d.release_norm));
  return d;
}

// [A]bove — downward compression defaults
static constexpr float kDefaultLowDownThresh = -33.8f;
static constexpr float kDefaultMidDownThresh = -30.2f;
static constexpr float kDefaultHighDownThresh = -35.5f;
static constexpr float kDefaultLowDownRatio = 66.7f;
static constexpr float kDefaultMidDownRatio = 66.7f;
static constexpr float kDefaultHighDownRatio = 1000.0f;  // inf:1

// [B]elow — upward compression defaults
static constexpr float kDefaultLowUpThresh = -40.8f;
static constexpr float kDefaultMidUpThresh = -41.8f;
static constexpr float kDefaultHighUpThresh = -40.8f;
static constexpr float kDefaultLowUpRatio = 4.17f;
static constexpr float kDefaultMidUpRatio = 4.17f;
static constexpr float kDefaultHighUpRatio = 4.17f;

// [T]ime — attack/release defaults
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
    // Re-load compressors with new sample rate
    for (int b = 0; b < kNumBands; ++b) {
      band_comp_[b].load("", 0, sample_rate_);
    }
    updateBandCompParams();
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
    return;
  }

  float amount = (float)params_[PARAM_AMOUNT];  // 0..1 dry/wet
  float global_out_db = normToDb24(params_[PARAM_OUTPUT]);
  float global_out_lin = std::pow(10.0f, global_out_db / 20.0f);

  // Per-band input gains
  float band_in_lin[kNumBands];
  band_in_lin[0] = std::pow(10.0f, normToDb24(params_[PARAM_LOW_IN]) / 20.0f);
  band_in_lin[1] = std::pow(10.0f, normToDb24(params_[PARAM_MID_IN]) / 20.0f);
  band_in_lin[2] = std::pow(10.0f, normToDb24(params_[PARAM_HIGH_IN]) / 20.0f);

  // Per-band output gains
  float band_out_lin[kNumBands];
  band_out_lin[0] = std::pow(10.0f, normToDb24(params_[PARAM_LOW_OUT]) / 20.0f);
  band_out_lin[1] = std::pow(10.0f, normToDb24(params_[PARAM_MID_OUT]) / 20.0f);
  band_out_lin[2] =
      std::pow(10.0f, normToDb24(params_[PARAM_HIGH_OUT]) / 20.0f);

  updateCrossoverCoeffs();

  float peak_input_db = -200.0f;
  float peak_output_db = -200.0f;

  // Temporary per-band buffers
  std::vector<float> band_buf(kNumBands * 2 * num_samples, 0.0f);
  float* bandL[kNumBands];
  float* bandR[kNumBands];
  for (int b = 0; b < kNumBands; ++b) {
    bandL[b] = &band_buf[(b * 2) * num_samples];
    bandR[b] = &band_buf[(b * 2 + 1) * num_samples];
  }

  // Split into 3 bands using LR4 crossover (per-sample)
  for (int i = 0; i < num_samples; ++i) {
    float inL = outL[i];
    float inR = outR[i];

    float in_abs = std::max(std::abs(inL), std::abs(inR));
    float in_db = (in_abs > 1e-10f) ? 20.0f * std::log10(in_abs) : -200.0f;
    if (in_db > peak_input_db) peak_input_db = in_db;

    // Low split: LP1 cascaded, HP1 cascaded
    float lp1_l = processBiquad(lp1_L_[0], lp1_coeffs_, inL);
    lp1_l = processBiquad(lp1_L_[1], lp1_coeffs_, lp1_l);
    float hp1_l = processBiquad(hp1_L_[0], hp1_coeffs_, inL);
    hp1_l = processBiquad(hp1_L_[1], hp1_coeffs_, hp1_l);

    float lp1_r = processBiquad(lp1_R_[0], lp1_coeffs_, inR);
    lp1_r = processBiquad(lp1_R_[1], lp1_coeffs_, lp1_r);
    float hp1_r = processBiquad(hp1_R_[0], hp1_coeffs_, inR);
    hp1_r = processBiquad(hp1_R_[1], hp1_coeffs_, hp1_r);

    // High split on hp1 output: LP2, HP2
    float lp2_l = processBiquad(lp2_L_[0], lp2_coeffs_, hp1_l);
    lp2_l = processBiquad(lp2_L_[1], lp2_coeffs_, lp2_l);
    float hp2_l = processBiquad(hp2_L_[0], hp2_coeffs_, hp1_l);
    hp2_l = processBiquad(hp2_L_[1], hp2_coeffs_, hp2_l);

    float lp2_r = processBiquad(lp2_R_[0], lp2_coeffs_, hp1_r);
    lp2_r = processBiquad(lp2_R_[1], lp2_coeffs_, lp2_r);
    float hp2_r = processBiquad(hp2_R_[0], hp2_coeffs_, hp1_r);
    hp2_r = processBiquad(hp2_R_[1], hp2_coeffs_, hp2_r);

    // Band channels: Low=lp1, Mid=lp2, High=hp2
    bandL[0][i] = lp1_l;
    bandR[0][i] = lp1_r;
    bandL[1][i] = lp2_l;
    bandR[1][i] = lp2_r;
    bandL[2][i] = hp2_l;
    bandR[2][i] = hp2_r;
  }

  // Apply per-band input gains, then process each band through its compressor
  for (int b = 0; b < kNumBands; ++b) {
    for (int i = 0; i < num_samples; ++i) {
      bandL[b][i] *= band_in_lin[b];
      bandR[b][i] *= band_in_lin[b];
    }
    float* band_bufs[] = {bandL[b], bandR[b]};
    band_comp_[b].process(band_bufs, band_bufs, num_samples, context, {});
  }

  // Sum bands back together with dry/wet (amount) and per-band output gains
  for (int i = 0; i < num_samples; ++i) {
    float dryL = outL[i];
    float dryR = outR[i];

    float sumL = 0, sumR = 0;
    // Reconstruct dry signal from bands (uncompressed) for blending
    // Since we already overwrote outL/outR, we use the original input.
    // But we need dry band signals too — for simplicity, when amount < 1
    // we blend at the final output level.
    for (int b = 0; b < kNumBands; ++b) {
      sumL += bandL[b][i] * band_out_lin[b];
      sumR += bandR[b][i] * band_out_lin[b];
    }

    // Blend wet/dry based on amount (0=dry, 1=fully wet)
    outL[i] = (dryL * (1.0f - amount) + sumL * amount) * global_out_lin;
    outR[i] = (dryR * (1.0f - amount) + sumR * amount) * global_out_lin;

    float out_abs = std::max(std::abs(outL[i]), std::abs(outR[i]));
    float out_db = (out_abs > 1e-10f) ? 20.0f * std::log10(out_abs) : -200.0f;
    if (out_db > peak_output_db) peak_output_db = out_db;
  }

  input_db_.store(peak_input_db, std::memory_order_relaxed);
  output_db_.store(peak_output_db, std::memory_order_relaxed);
}

int BuiltinHott::getParameterCount() const { return kTotalParams; }

bool BuiltinHott::getParameterInfo(int index, VstParamInfo& info) const {
  if (index < 0 || index >= kTotalParams) return false;
  info.id = index;
  static const char* names[] = {
      "LowXover",     "HighXover",     "Amount",        "Time",
      "Output",       "LowOut",        "MidOut",        "HighOut",
      "Enable",       "LowDownThresh", "MidDownThresh", "HighDownThresh",
      "LowUpThresh",  "MidUpThresh",   "HighUpThresh",  "SoftKnee",
      "RmsMode",      "LowAttack",     "MidAttack",     "HighAttack",
      "LowRelease",   "MidRelease",    "HighRelease",   "LowDownRatio",
      "MidDownRatio", "HighDownRatio", "LowUpRatio",    "MidUpRatio",
      "HighUpRatio",  "LowIn",         "MidIn",         "HighIn"};
  // Defaults computed in reset() — provide matching values here for hosts.
  static const double defaults[] = {
      0.461,
      0.436,
      1.0,
      1.0,
      0.5,
      0.715,
      0.619,
      0.715,                                   // 0-7
      1.0,                                     // 8 (Enable)
      (kDefaultLowDownThresh + 60.0) / 60.0,   // 9
      (kDefaultMidDownThresh + 60.0) / 60.0,   // 10
      (kDefaultHighDownThresh + 60.0) / 60.0,  // 11
      (kDefaultLowUpThresh + 60.0) / 72.0,     // 12
      (kDefaultMidUpThresh + 60.0) / 72.0,     // 13
      (kDefaultHighUpThresh + 60.0) / 72.0,    // 14
      1.0,
      1.0,  // 15-16 (SoftKnee,RMS on)
      0.0,
      0.0,
      0.0,
      0.0,
      0.0,
      0.0,  // 17-22 (att/rel, set in reset)
      0.0,
      0.0,
      0.0,
      0.0,
      0.0,
      0.0,  // 23-28 (ratios, set in reset)
      0.608,
      0.608,
      0.608};  // 29-31 (in gains ~5.2dB)
  info.name = names[index];
  info.defaultValue = defaults[index];
  return true;
}

void BuiltinHott::setParameterValue(uint32_t id, double value) {
  if (id >= (uint32_t)kTotalParams) return;
  params_[id] = value;
  if (id == PARAM_ENABLE) enabled_ = value >= 0.5;
  // Update band compressor params when relevant params change
  if (id == PARAM_TIME ||
      (id >= PARAM_LOW_DOWN_THRESH && id <= PARAM_HIGH_UP_THRESH) ||
      (id >= PARAM_SOFT_KNEE && id <= PARAM_HIGH_UP_RATIO)) {
    updateBandCompParams();
  }
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
  return band_comp_[band].getGainReductionDb();
}

float BuiltinHott::getInputDb() const {
  return input_db_.load(std::memory_order_relaxed);
}

float BuiltinHott::getOutputDb() const {
  return output_db_.load(std::memory_order_relaxed);
}

// --- Private ---

void BuiltinHott::reset() {
  // Default parameter values — Xfer OTT exact defaults
  params_[PARAM_LOW_CROSSOVER] = 0.461;   // 88.3 Hz
  params_[PARAM_HIGH_CROSSOVER] = 0.436;  // 2.50 kHz
  params_[PARAM_AMOUNT] = 1.0;            // 100%
  params_[PARAM_TIME] = 1.0;              // 100%
  params_[PARAM_OUTPUT] = 0.5;            // 0 dB
  params_[PARAM_LOW_OUT] = 0.715;         // 10.3 dB
  params_[PARAM_MID_OUT] = 0.619;         // 5.7 dB
  params_[PARAM_HIGH_OUT] = 0.715;        // 10.3 dB
  params_[PARAM_ENABLE] = 1.0;
  enabled_ = true;

  // [A]bove — downward threshold defaults (0..1 -> -60..0 dB)
  params_[PARAM_LOW_DOWN_THRESH] = (kDefaultLowDownThresh + 60.0) / 60.0;
  params_[PARAM_MID_DOWN_THRESH] = (kDefaultMidDownThresh + 60.0) / 60.0;
  params_[PARAM_HIGH_DOWN_THRESH] = (kDefaultHighDownThresh + 60.0) / 60.0;
  // [B]elow — upward threshold defaults (0..1 -> -60..+12 dB)
  params_[PARAM_LOW_UP_THRESH] = (kDefaultLowUpThresh + 60.0) / 72.0;
  params_[PARAM_MID_UP_THRESH] = (kDefaultMidUpThresh + 60.0) / 72.0;
  params_[PARAM_HIGH_UP_THRESH] = (kDefaultHighUpThresh + 60.0) / 72.0;

  // Soft knee and RMS enabled by default (matches Xfer OTT)
  params_[PARAM_SOFT_KNEE] = 1.0;
  params_[PARAM_RMS_MODE] = 1.0;

  // [T]ime — per-band attack/release defaults (normalized)
  auto bd_low =
      computeBandDefaults(0, 1, kDefaultLowAttack, kDefaultLowRelease);
  auto bd_mid =
      computeBandDefaults(0, 1, kDefaultMidAttack, kDefaultMidRelease);
  auto bd_hi =
      computeBandDefaults(0, 1, kDefaultHighAttack, kDefaultHighRelease);
  params_[PARAM_LOW_ATTACK] = bd_low.attack_norm;
  params_[PARAM_MID_ATTACK] = bd_mid.attack_norm;
  params_[PARAM_HIGH_ATTACK] = bd_hi.attack_norm;
  params_[PARAM_LOW_RELEASE] = bd_low.release_norm;
  params_[PARAM_MID_RELEASE] = bd_mid.release_norm;
  params_[PARAM_HIGH_RELEASE] = bd_hi.release_norm;

  // Per-band downward ratios (normalized: ratio = 1/(1-norm))
  auto r_low_down = computeBandDefaults(0, kDefaultLowDownRatio, 1, 10);
  auto r_mid_down = computeBandDefaults(0, kDefaultMidDownRatio, 1, 10);
  auto r_hi_down = computeBandDefaults(0, kDefaultHighDownRatio, 1, 10);
  params_[PARAM_LOW_DOWN_RATIO] = r_low_down.ratio_norm;
  params_[PARAM_MID_DOWN_RATIO] = r_mid_down.ratio_norm;
  params_[PARAM_HIGH_DOWN_RATIO] = r_hi_down.ratio_norm;

  // Per-band upward ratios
  auto r_low_up = computeBandDefaults(0, kDefaultLowUpRatio, 1, 10);
  auto r_mid_up = computeBandDefaults(0, kDefaultMidUpRatio, 1, 10);
  auto r_hi_up = computeBandDefaults(0, kDefaultHighUpRatio, 1, 10);
  params_[PARAM_LOW_UP_RATIO] = r_low_up.ratio_norm;
  params_[PARAM_MID_UP_RATIO] = r_mid_up.ratio_norm;
  params_[PARAM_HIGH_UP_RATIO] = r_hi_up.ratio_norm;

  // Per-band input gains (5.2 dB -> norm = (5.2+24)/48 = 0.608)
  params_[PARAM_LOW_IN] = 0.608;
  params_[PARAM_MID_IN] = 0.608;
  params_[PARAM_HIGH_IN] = 0.608;

  // Initialize band compressors
  for (int b = 0; b < kNumBands; ++b) {
    band_comp_[b].load("", 0, sample_rate_);
  }
  updateBandCompParams();

  // Clear filter states
  for (int s = 0; s < 2; ++s) {
    lp1_L_[s] = lp1_R_[s] = {};
    hp1_L_[s] = hp1_R_[s] = {};
    lp2_L_[s] = lp2_R_[s] = {};
    hp2_L_[s] = hp2_R_[s] = {};
  }

  updateCrossoverCoeffs();
}

void BuiltinHott::updateBandCompParams() {
  float time_mult = (float)(params_[PARAM_TIME] * 2.0);
  if (time_mult < 0.01f) time_mult = 0.01f;

  // Per-band thresholds (already in BuiltinCompressor norm space)
  double down_thresh_norm[kNumBands] = {params_[PARAM_LOW_DOWN_THRESH],
                                        params_[PARAM_MID_DOWN_THRESH],
                                        params_[PARAM_HIGH_DOWN_THRESH]};
  double up_thresh_norm[kNumBands] = {params_[PARAM_LOW_UP_THRESH],
                                      params_[PARAM_MID_UP_THRESH],
                                      params_[PARAM_HIGH_UP_THRESH]};

  // Per-band ratios from params (already normalized)
  double down_ratio_norm[kNumBands] = {params_[PARAM_LOW_DOWN_RATIO],
                                       params_[PARAM_MID_DOWN_RATIO],
                                       params_[PARAM_HIGH_DOWN_RATIO]};
  double up_ratio_norm[kNumBands] = {params_[PARAM_LOW_UP_RATIO],
                                     params_[PARAM_MID_UP_RATIO],
                                     params_[PARAM_HIGH_UP_RATIO]};

  // Per-band attack/release from params (normalized), scaled by Time
  double att_norm[kNumBands] = {params_[PARAM_LOW_ATTACK],
                                params_[PARAM_MID_ATTACK],
                                params_[PARAM_HIGH_ATTACK]};
  double rel_norm[kNumBands] = {params_[PARAM_LOW_RELEASE],
                                params_[PARAM_MID_RELEASE],
                                params_[PARAM_HIGH_RELEASE]};

  // Soft knee: 0 -> hard (0 dB), 1 -> soft (~10 dB width -> norm 0.33)
  double knee_norm = (params_[PARAM_SOFT_KNEE] >= 0.5) ? 0.33 : 0.0;
  double rms_mode = (params_[PARAM_RMS_MODE] >= 0.5) ? 1.0 : 0.0;

  for (int b = 0; b < kNumBands; ++b) {
    // Convert normalized att/rel to ms, apply time multiplier, re-normalize
    float att_ms = 0.1f * std::pow(1000.0f, (float)att_norm[b]) * time_mult;
    float rel_ms = 10.0f * std::pow(100.0f, (float)rel_norm[b]) * time_mult;
    auto bd = computeBandDefaults(0.0f, 1.0f, att_ms, rel_ms);

    band_comp_[b].setParameterValue(BuiltinCompressor::PARAM_THRESHOLD,
                                    down_thresh_norm[b]);
    band_comp_[b].setParameterValue(BuiltinCompressor::PARAM_RATIO,
                                    down_ratio_norm[b]);
    band_comp_[b].setParameterValue(BuiltinCompressor::PARAM_ATTACK,
                                    bd.attack_norm);
    band_comp_[b].setParameterValue(BuiltinCompressor::PARAM_RELEASE,
                                    bd.release_norm);
    band_comp_[b].setParameterValue(BuiltinCompressor::PARAM_KNEE, knee_norm);
    band_comp_[b].setParameterValue(BuiltinCompressor::PARAM_MAKEUP, 0.0);
    band_comp_[b].setParameterValue(BuiltinCompressor::PARAM_ENABLE, 1.0);
    band_comp_[b].setParameterValue(BuiltinCompressor::PARAM_UP_THRESHOLD,
                                    up_thresh_norm[b]);
    band_comp_[b].setParameterValue(BuiltinCompressor::PARAM_UP_RATIO,
                                    up_ratio_norm[b]);
    band_comp_[b].setParameterValue(BuiltinCompressor::PARAM_RMS_MODE,
                                    rms_mode);
  }
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

float BuiltinHott::normToFreq(double norm, float min_hz, float max_hz) {
  return min_hz * std::pow(max_hz / min_hz, (float)norm);
}

float BuiltinHott::normToDb24(double norm) {
  return (float)(norm * 48.0 - 24.0);
}

}  // namespace hibiki
