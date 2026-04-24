#include "engine/effects/builtin_envelope_shaper.hpp"

#include <algorithm>
#include <cmath>

namespace hibiki {

static const std::string kEnvShaperName = "EnvShaper";
static const std::string kEnvShaperPath = "builtin://envelope_shaper";

// Rate table: maps normalized value to beat duration
static const struct {
  float beats;
  const char* label;
} kRateTable[] = {
    {0.25f, "1/16"},   {0.333f, "1/8 T"}, {0.375f, "1/8 d"}, {0.5f, "1/8"},
    {0.667f, "1/4 T"}, {0.75f, "1/4 d"},  {1.0f, "1/4"},     {1.333f, "1/2 T"},
    {1.5f, "1/2 d"},   {2.0f, "1/2"},     {3.0f, "3/4"},     {4.0f, "1 bar"},
    {8.0f, "2 bars"},  {16.0f, "4 bars"},
};
static constexpr int kNumRates = sizeof(kRateTable) / sizeof(kRateTable[0]);

BuiltinEnvelopeShaper::BuiltinEnvelopeShaper() {
  params_[PARAM_MIX] = 1.0;
  params_[PARAM_RATE] = 0.43;  // ~1/4 note
  params_[PARAM_SMOOTH] = 0.3;
  params_[PARAM_MODE] = 0.0;  // Gain mode
  params_[PARAM_ENABLE] = 1.0;
  params_[PARAM_NUM_POINTS] = 0.25;  // 6 points

  // Default curve: inverted kick (fast dip → decay back to full)
  // Creating a sidechain-compression-like envelope
  params_[PARAM_POINT_Y_0] = 1.0;      // Start at full
  params_[PARAM_POINT_Y_0 + 1] = 0.0;  // Quick dip
  params_[PARAM_POINT_Y_0 + 2] = 0.05;
  params_[PARAM_POINT_Y_0 + 3] = 0.2;
  params_[PARAM_POINT_Y_0 + 4] = 0.6;
  params_[PARAM_POINT_Y_0 + 5] = 1.0;  // Back to full
  for (int i = 6; i < kMaxPoints; ++i) {
    params_[PARAM_POINT_Y_0 + i] = 1.0;
  }
}

bool BuiltinEnvelopeShaper::load(const std::string& /*path*/,
                                 int /*plugin_index*/, double sample_rate) {
  sample_rate_ = sample_rate;
  reset();
  return true;
}

void BuiltinEnvelopeShaper::reset() {
  phase_ = 0.0;
  lp_state_l_ = lp_state_r_ = 0.0f;
  hp_state_l_ = hp_state_r_ = 0.0f;
}

// --- Parameter mapping ---

float BuiltinEnvelopeShaper::normToRateBeats(double norm) {
  int idx = std::clamp((int)(norm * (kNumRates - 1) + 0.5), 0, kNumRates - 1);
  return kRateTable[idx].beats;
}

const char* BuiltinEnvelopeShaper::normToRateLabel(double norm) {
  int idx = std::clamp((int)(norm * (kNumRates - 1) + 0.5), 0, kNumRates - 1);
  return kRateTable[idx].label;
}

int BuiltinEnvelopeShaper::getNumPoints() const {
  return std::clamp((int)(params_[PARAM_NUM_POINTS] * 14 + 2), 2, kMaxPoints);
}

float BuiltinEnvelopeShaper::catmullRom(float p0, float p1, float p2, float p3,
                                        float t) {
  float t2 = t * t;
  float t3 = t2 * t;
  return 0.5f * ((2.0f * p1) + (-p0 + p2) * t +
                 (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                 (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

float BuiltinEnvelopeShaper::evaluateCurve(float t) const {
  int n = getNumPoints();
  if (n < 2) return 1.0f;

  // Map t ∈ [0,1] to segment index
  float scaled = t * (n - 1);
  int seg = std::clamp((int)scaled, 0, n - 2);
  float frac = scaled - seg;

  float smooth = std::clamp((float)params_[PARAM_SMOOTH], 0.0f, 1.0f);

  if (smooth < 0.01f) {
    // Linear interpolation
    float y0 = (float)params_[PARAM_POINT_Y_0 + seg];
    float y1 = (float)params_[PARAM_POINT_Y_0 + seg + 1];
    return y0 + (y1 - y0) * frac;
  }

  // Catmull-Rom spline with smooth blending
  int i0 = std::max(0, seg - 1);
  int i1 = seg;
  int i2 = std::min(n - 1, seg + 1);
  int i3 = std::min(n - 1, seg + 2);

  float p0 = (float)params_[PARAM_POINT_Y_0 + i0];
  float p1 = (float)params_[PARAM_POINT_Y_0 + i1];
  float p2 = (float)params_[PARAM_POINT_Y_0 + i2];
  float p3 = (float)params_[PARAM_POINT_Y_0 + i3];

  float spline = catmullRom(p0, p1, p2, p3, frac);
  float linear = p1 + (p2 - p1) * frac;

  // Blend between linear and spline based on smooth param
  float val = linear * (1.0f - smooth) + spline * smooth;
  return std::clamp(val, 0.0f, 1.0f);
}

void BuiltinEnvelopeShaper::process(
    float** inputs, float** outputs, int num_samples,
    const HostProcessContext& context,
    const std::vector<MidiNoteEvent>& /*events*/, float** /*sidechain*/) {
  if (!enabled_) {
    for (int i = 0; i < num_samples; ++i) {
      outputs[0][i] = inputs[0][i];
      outputs[1][i] = inputs[1][i];
    }
    return;
  }

  float mix = std::clamp((float)params_[PARAM_MIX], 0.0f, 1.0f);
  // Mode: 0=gain, 0.33=HPF, 0.5=BPF, 1.0=LPF
  float mode_val = std::clamp((float)params_[PARAM_MODE], 0.0f, 1.0f);
  int filter_mode = 0;  // 0=gain
  if (mode_val > 0.08f && mode_val < 0.25f)
    filter_mode = 1;  // HPF
  else if (mode_val >= 0.25f && mode_val < 0.58f)
    filter_mode = 2;  // BPF
  else if (mode_val >= 0.58f)
    filter_mode = 3;  // LPF

  // Compute cycle length in samples from tempo
  float rate_beats = normToRateBeats(params_[PARAM_RATE]);
  double cycle_samples = 1.0;
  if (context.tempo > 0 && sample_rate_ > 0) {
    double cycle_sec = rate_beats * 60.0 / context.tempo;
    cycle_samples = cycle_sec * sample_rate_;
  }
  if (cycle_samples < 1.0) cycle_samples = 1.0;

  // Phase increment per sample
  double phase_inc = 1.0 / cycle_samples;

  // Sync phase to transport position for tight alignment
  if (context.tempo > 0 && context.projectTimeMusic >= 0) {
    double total_beats = context.projectTimeMusic;
    if (rate_beats > 0) {
      phase_ = std::fmod(total_beats / rate_beats, 1.0);
    }
  }

  float in_sum_sq = 0, out_sum_sq = 0;

  for (int i = 0; i < num_samples; ++i) {
    float in_l = inputs[0][i];
    float in_r = inputs[1][i];

    float env = evaluateCurve((float)phase_);
    float out_l, out_r;

    if (filter_mode == 0) {
      // Gain mode: env directly modulates amplitude
      float gain = 1.0f * (1.0f - mix) + env * mix;
      out_l = in_l * gain;
      out_r = in_r * gain;
    } else {
      // Filter modes: env controls cutoff frequency
      float cutoff_hz = 80.0f * std::pow(250.0f, env);
      float lp_coeff =
          1.0f - std::exp(-2.0f * 3.14159f * cutoff_hz / (float)sample_rate_);

      switch (filter_mode) {
        case 1: {  // HPF
          lp_state_l_ += lp_coeff * (in_l - lp_state_l_);
          lp_state_r_ += lp_coeff * (in_r - lp_state_r_);
          float wet_l = in_l - lp_state_l_;
          float wet_r = in_r - lp_state_r_;
          out_l = in_l * (1.0f - mix) + wet_l * mix;
          out_r = in_r * (1.0f - mix) + wet_r * mix;
          break;
        }
        case 2: {  // BPF (LP then HP)
          lp_state_l_ += lp_coeff * (in_l - lp_state_l_);
          lp_state_r_ += lp_coeff * (in_r - lp_state_r_);
          hp_state_l_ += lp_coeff * (lp_state_l_ - hp_state_l_);
          hp_state_r_ += lp_coeff * (lp_state_r_ - hp_state_r_);
          float wet_l = lp_state_l_ - hp_state_l_;
          float wet_r = lp_state_r_ - hp_state_r_;
          out_l = in_l * (1.0f - mix) + wet_l * mix;
          out_r = in_r * (1.0f - mix) + wet_r * mix;
          break;
        }
        case 3:
        default: {  // LPF
          lp_state_l_ += lp_coeff * (in_l - lp_state_l_);
          lp_state_r_ += lp_coeff * (in_r - lp_state_r_);
          float wet_l = lp_state_l_;
          float wet_r = lp_state_r_;
          out_l = in_l * (1.0f - mix) + wet_l * mix;
          out_r = in_r * (1.0f - mix) + wet_r * mix;
          break;
        }
      }
    }

    outputs[0][i] = out_l;
    outputs[1][i] = out_r;

    in_sum_sq += in_l * in_l + in_r * in_r;
    out_sum_sq += out_l * out_l + out_r * out_r;

    phase_ += phase_inc;
    if (phase_ >= 1.0) phase_ -= 1.0;
  }

  // Smoothed RMS metering
  float in_rms = std::sqrt(in_sum_sq / (2.0f * num_samples));
  float out_rms = std::sqrt(out_sum_sq / (2.0f * num_samples));
  constexpr float kSmooth = 0.15f;
  input_rms_ = input_rms_.load() * (1.0f - kSmooth) + in_rms * kSmooth;
  output_rms_ = output_rms_.load() * (1.0f - kSmooth) + out_rms * kSmooth;
  current_phase_ = (float)phase_;
}

// --- IPlugin interface ---

int BuiltinEnvelopeShaper::getParameterCount() const { return kTotalParams; }

bool BuiltinEnvelopeShaper::getParameterInfo(int index,
                                             VstParamInfo& info) const {
  if (index < 0 || index >= kTotalParams) return false;

  info.id = index;
  switch (index) {
    case PARAM_MIX:
      info.name = "Mix";
      info.defaultValue = 1.0;
      break;
    case PARAM_RATE:
      info.name = "Rate";
      info.defaultValue = 0.43;
      break;
    case PARAM_SMOOTH:
      info.name = "Smooth";
      info.defaultValue = 0.3;
      break;
    case PARAM_MODE:
      info.name = "Mode";
      info.defaultValue = 0.0;
      break;
    case PARAM_ENABLE:
      info.name = "Enable";
      info.defaultValue = 1.0;
      break;
    case PARAM_NUM_POINTS:
      info.name = "Points";
      info.defaultValue = 0.25;
      break;
    default:
      // Point Y values
      if (index >= PARAM_POINT_Y_0 && index < PARAM_POINT_Y_0 + kMaxPoints) {
        int pt = index - PARAM_POINT_Y_0;
        info.name = "Pt " + std::to_string(pt);
        info.defaultValue = (pt < 6) ? params_[index] : 1.0;
      }
      break;
  }
  return true;
}

void BuiltinEnvelopeShaper::setParameterValue(uint32_t id, double value) {
  if (id < (uint32_t)kTotalParams) {
    params_[id] = value;
    if (id == PARAM_ENABLE) enabled_ = value >= 0.5;
  }
}

double BuiltinEnvelopeShaper::getParameterValue(uint32_t id) const {
  return id < (uint32_t)kTotalParams ? params_[id] : 0.0;
}

const std::string& BuiltinEnvelopeShaper::getName() const {
  return kEnvShaperName;
}
const std::string& BuiltinEnvelopeShaper::getPath() const {
  return kEnvShaperPath;
}
int BuiltinEnvelopeShaper::getPluginIndex() const { return 0; }
bool BuiltinEnvelopeShaper::isInstrument() const { return false; }

float BuiltinEnvelopeShaper::getInputDb() const {
  float rms = input_rms_.load();
  return rms > 0 ? 20.0f * std::log10(rms) : -100.0f;
}

float BuiltinEnvelopeShaper::getOutputDb() const {
  float rms = output_rms_.load();
  return rms > 0 ? 20.0f * std::log10(rms) : -100.0f;
}

float BuiltinEnvelopeShaper::getEnvelopePhase() const {
  return current_phase_.load();
}

}  // namespace hibiki
