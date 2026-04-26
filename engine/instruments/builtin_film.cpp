#include "engine/instruments/builtin_film.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "absl/base/optimization.h"

namespace hibiki {

namespace {
constexpr float kTwoPi = 6.28318530718f;
constexpr float kMaxModDepth = 1.0f * 3.14159265f;  // 4π max FM depth

// Map normalized 0..1 to frequency ratio (0.5–16x, exponential).
float normToRatio(float norm) {
  // 0 -> 0.5x, 0.5 -> 1x, 1.0 -> 16x
  return 0.5f * std::pow(32.0f, norm);
}

// Map normalized 0..1 to fine detune in cents (-100..+100).
float normToFineCents(float norm) { return (norm - 0.5f) * 200.0f; }

// Map normalized 0..1 to LFO rate in Hz (0.01..20).
float normToLfoRate(float norm) { return 0.01f * std::pow(2000.0f, norm); }

// Simple seeded PRNG for noise.
float whiteNoise() {
  static uint32_t seed = 0x12345678;
  seed ^= seed << 13;
  seed ^= seed >> 17;
  seed ^= seed << 5;
  return (float)(int32_t)seed / 2147483648.0f;
}
}  // namespace

BuiltinFilm::BuiltinFilm() { reset(); }

bool BuiltinFilm::load(const std::string& /*path*/, int /*plugin_index*/,
                       double sample_rate) {
  sample_rate_ = sample_rate;
  reset();
  return true;
}

void BuiltinFilm::process(float** /*inputs*/, float** outputs, int num_samples,
                          const HostProcessContext& context,
                          const std::vector<MidiNoteEvent>& events,
                          float** /*sidechain*/) {
  sample_rate_ = context.sampleRate;
  float sr = (float)sample_rate_;

  // Handle MIDI events.
  for (const auto& ev : events) {
    if (ev.isNoteOn && ev.velocity > 0) {
      noteOn(ev.pitch, ev.velocity);
    } else {
      noteOff(ev.pitch);
    }
  }

  float* outL = outputs[0];
  float* outR = outputs[1];
  for (int i = 0; i < num_samples; ++i) {
    outL[i] = 0;
    outR[i] = 0;
  }

  if (!enabled_) return;

  float master_vol = params_[G_MASTER_VOL];

  for (int v = 0; v < kMaxVoices; ++v) {
    auto& voice = voices_[v];
    // Check if any envelope is still active.
    bool any_active = false;
    for (int o = 0; o < kNumOps; ++o) {
      if (voice.ops[o].env.getStage() != Adsr::Stage::ENV_IDLE) {
        any_active = true;
        break;
      }
    }
    if (!voice.active && !any_active) continue;

    for (int i = 0; i < num_samples; ++i) {
      // --- Compute operator outputs ---
      // op_mod_out = envelope-shaped raw osc (used for FM modulation).
      // op_output  = op_mod_out * level (used for mixing/filter routing).
      // Process forward: ops 0→5. Higher-index ops modulate lower-index
      // ones via their prev_output (1-sample delay), matching Sytrus
      // convention.
      float op_output[kNumOps] = {};
      float op_mod_out[kNumOps] = {};

      for (int o = 0; o < kNumOps; ++o) {
        int base = opBase(o);
        float level = params_[base + OP_LEVEL];
        float env_val = voice.ops[o].env.process(sr);

        if (voice.ops[o].env.isIdle() && !voice.active) continue;

        float ratio = normToRatio(params_[base + OP_RATIO]);
        float fine_cents = normToFineCents(params_[base + OP_FINE]);
        float freq_offset = (params_[base + OP_FREQ_OFFSET] - 0.5f) * 200.0f;
        float freq =
            voice.base_freq * ratio * std::pow(2.0f, fine_cents / 1200.0f) +
            freq_offset;

        // Accumulate FM modulation input from all operators via matrix.
        float mod_input = 0;
        for (int src = 0; src < kNumOps; ++src) {
          float mod_depth = params_[matrixIdx(src, o)];
          float depth = (mod_depth - 0.5f) * 2.0f * kMaxModDepth;
          if (std::abs(depth) < 1e-6f) continue;
          if (src < o) {
            // Already computed this frame — use current output.
            mod_input += op_mod_out[src] * depth;
          } else {
            // Not yet computed — use previous frame's output (1-sample delay).
            mod_input += voice.ops[src].prev_output * depth;
          }
        }

        // Per-op LFO.
        float lfo_rate = normToLfoRate(params_[base + OP_LFO_RATE]);
        float lfo_depth = params_[base + OP_LFO_DEPTH];
        Waveform lfo_wf = normToWaveform(params_[base + OP_LFO_WAVE]);
        float lfo_val = lfoValue(lfo_wf, voice.ops[o].lfo_phase) * lfo_depth;
        voice.ops[o].lfo_phase += lfo_rate / sample_rate_;
        if (voice.ops[o].lfo_phase >= 1.0) voice.ops[o].lfo_phase -= 1.0;

        // Phase offset.
        float phase_offset = params_[base + OP_PHASE];

        // Generate oscillator.
        Waveform wf = normToWaveform(params_[base + OP_WAVEFORM]);
        double phase = voice.ops[o].phase + phase_offset;
        float sample = generateOsc(wf, phase + mod_input / kTwoPi);

        // Apply Sytrus shape modifiers.
        float sh = params_[base + OP_SHAPE];
        float tn = params_[base + OP_TENSION];
        float sk = params_[base + OP_SKEW];
        float sn = params_[base + OP_SINE_SHAPER];
        bool half_mode = params_[base + OP_HALF] >= 0.5f;
        bool even_mode = params_[base + OP_EVEN] >= 0.5f;
        bool abs_mode = params_[base + OP_ABSOLUTE] >= 0.5f;
        sample = applyShapeModifiers(sample, phase + mod_input / kTwoPi, sh, tn,
                                     sk, sn, half_mode, even_mode, abs_mode);

        // Mix noise (NS).
        float noise_mix = params_[base + OP_NOISE_MIX];
        if (noise_mix > 0.001f) {
          sample = sample * (1.0f - noise_mix) + whiteNoise() * noise_mix;
        }

        // Apply amplitude LFO modulation.
        sample *= (1.0f + lfo_val);

        // Modulation output (for FM): envelope-shaped, NO level.
        op_mod_out[o] = sample * env_val;
        voice.ops[o].prev_output = op_mod_out[o];

        // Mix output: includes level (for routing to filters/output).
        op_output[o] = op_mod_out[o] * level;

        // Advance phase.
        voice.ops[o].phase += freq / sample_rate_;
        if (voice.ops[o].phase >= 1.0) voice.ops[o].phase -= 1.0;
      }

      // --- Route operators through filters via matrix ---
      float filter_input[kNumFilters][2] = {};  // L, R per filter
      float direct_out_l = 0, direct_out_r = 0;

      for (int o = 0; o < kNumOps; ++o) {
        float sig = op_output[o] * voice.velocity;
        float op_pan = params_[opBase(o) + OP_PAN];
        float panL = std::cos(op_pan * 1.5708f);
        float panR = std::sin(op_pan * 1.5708f);

        // Filter sends (matrix cols 6,7,8).
        for (int f = 0; f < kNumFilters; ++f) {
          float send = params_[matrixIdx(o, kNumOps + f)];
          float amount = (send - 0.5f) * 2.0f;
          filter_input[f][0] += sig * panL * amount;
          filter_input[f][1] += sig * panR * amount;
        }

        // Pan send (matrix col 9) — modifies operator pan.
        // FX send (matrix col 10) — reserved.
        // Direct output (matrix col 11).
        float out_level = params_[matrixIdx(o, kNumOps + 3 + 2)];
        float out_amount = (out_level - 0.5f) * 2.0f;
        direct_out_l += sig * panL * out_amount;
        direct_out_r += sig * panR * out_amount;
      }

      // --- Process filters ---
      float filt_out_l = 0, filt_out_r = 0;
      for (int f = 0; f < kNumFilters; ++f) {
        int fbase = filterBase(f);
        auto filt_type = BiquadFilter::normToType(params_[fbase + FLT_TYPE]);
        float cutoff = BiquadFilter::normToCutoff(params_[fbase + FLT_CUTOFF]);
        float q = BiquadFilter::normToQ(params_[fbase + FLT_RESONANCE]);
        float filt_env_val = voice.filters[f].env.process(sr);
        float env_depth = (params_[fbase + FLT_ENV_DEPTH] - 0.5f) * 2.0f;
        float mix = params_[fbase + FLT_MIX];

        // Filter LFO.
        float lfo_rate = normToLfoRate(params_[fbase + FLT_LFO_RATE]);
        float lfo_depth = params_[fbase + FLT_LFO_DEPTH];
        Waveform lfo_wf = normToWaveform(params_[fbase + FLT_LFO_WAVE]);
        float lfo_mod =
            lfoValue(lfo_wf, voice.filters[f].lfo_phase) * lfo_depth;
        voice.filters[f].lfo_phase += lfo_rate / sample_rate_;
        if (voice.filters[f].lfo_phase >= 1.0)
          voice.filters[f].lfo_phase -= 1.0;

        // Modulated cutoff.
        float total_mod = env_depth * filt_env_val + lfo_mod;
        float mod_cutoff = cutoff * std::pow(2.0f, total_mod * 4.0f);
        mod_cutoff = std::clamp(mod_cutoff, 20.0f, 20000.0f);

        voice.filters[f].filterL.setParams(filt_type, mod_cutoff, q, 0.0f, sr);
        voice.filters[f].filterR.setParams(filt_type, mod_cutoff, q, 0.0f, sr);

        float dry_l = filter_input[f][0];
        float dry_r = filter_input[f][1];
        float wet_l = voice.filters[f].filterL.process(dry_l);
        float wet_r = voice.filters[f].filterR.process(dry_r);

        filt_out_l += dry_l * (1.0f - mix) + wet_l * mix;
        filt_out_r += dry_r * (1.0f - mix) + wet_r * mix;
      }

      outL[i] += (direct_out_l + filt_out_l) * master_vol;
      outR[i] += (direct_out_r + filt_out_r) * master_vol;
    }
  }
}

// --- IPlugin metadata ---

int BuiltinFilm::getParameterCount() const { return kTotalParams; }

bool BuiltinFilm::getParameterInfo(int index, VstParamInfo& info) const {
  if (index < 0 || index >= kTotalParams) return false;
  info.id = index;
  info.defaultValue = getDefaultValue(index);

  if (index < kOpParams) {
    int op = index / kParamsPerOp + 1;
    int p = index % kParamsPerOp;
    static const char* op_names[] = {
        "Waveform", "Level",     "Ratio",       "Fine",      "Env A",
        "Env D",    "Env S",     "Env R",       "Feedback",  "Pan",
        "LFO Rate", "LFO Depth", "LFO Wave",    "Phase",     "Shape",
        "Tension",  "Skew",      "Sine Shaper", "Noise Mix", "Freq Ofs",
        "Half",     "Even",      "Absolute"};
    info.name = "Op" + std::to_string(op) + " " + op_names[p];
  } else if (index < kOpParams + kFilterParams) {
    int fi = (index - kOpParams) / kParamsPerFilter + 1;
    int p = (index - kOpParams) % kParamsPerFilter;
    static const char* flt_names[] = {
        "Type",  "Cutoff",    "Resonance", "Env A",    "Env D",     "Env S",
        "Env R", "Env Depth", "Mix",       "LFO Rate", "LFO Depth", "LFO Wave"};
    info.name = "Flt" + std::to_string(fi) + " " + flt_names[p];
  } else if (index < kOpParams + kFilterParams + kGlobalParams) {
    static const char* global_names[] = {
        "Algorithm",     "Master Vol",    "Enable",    "Unison Voices",
        "Unison Detune", "Unison Spread", "Portamento"};
    info.name = global_names[index - kOpParams - kFilterParams];
  } else {
    int mi = index - kMatrixBase;
    int row = mi / kMatrixCols;
    int col = mi % kMatrixCols;
    static const char* col_names[] = {"1",  "2",  "3",  "4",   "5",  "6",
                                      "F1", "F2", "F3", "Pan", "FX", "Out"};
    info.name = "Mtx " + std::to_string(row + 1) + "→" + col_names[col];
  }
  return true;
}

void BuiltinFilm::setParameterValue(uint32_t id, double value) {
  if (id >= (uint32_t)kTotalParams) return;
  params_[id] = (float)value;
  if (id == (uint32_t)G_ENABLE) enabled_ = value >= 0.5;

  // Update op envelopes.
  if (id < (uint32_t)kOpParams) {
    int op = id / kParamsPerOp;
    int p = id % kParamsPerOp;
    if (p >= OP_ENV_A && p <= OP_ENV_R) {
      int base = opBase(op);
      for (auto& v : voices_) {
        v.ops[op].env.setNormalized(
            params_[base + OP_ENV_A], params_[base + OP_ENV_D],
            params_[base + OP_ENV_S], params_[base + OP_ENV_R]);
      }
    }
  }

  // Update filter envelopes.
  if (id >= (uint32_t)kOpParams && id < (uint32_t)(kOpParams + kFilterParams)) {
    int fi = (id - kOpParams) / kParamsPerFilter;
    int p = (id - kOpParams) % kParamsPerFilter;
    if (p >= FLT_ENV_A && p <= FLT_ENV_R) {
      int fbase = filterBase(fi);
      for (auto& v : voices_) {
        v.filters[fi].env.setNormalized(
            params_[fbase + FLT_ENV_A], params_[fbase + FLT_ENV_D],
            params_[fbase + FLT_ENV_S], params_[fbase + FLT_ENV_R]);
      }
    }
  }
}

double BuiltinFilm::getParameterValue(uint32_t id) const {
  return (id < (uint32_t)kTotalParams) ? params_[id] : 0;
}

const std::string& BuiltinFilm::getName() const {
  static const std::string n = kName;
  return n;
}

const std::string& BuiltinFilm::getPath() const {
  static const std::string p = kPath;
  return p;
}

int BuiltinFilm::getPluginIndex() const { return 0; }
bool BuiltinFilm::isInstrument() const { return true; }

// --- Private ---

BuiltinFilm::Waveform BuiltinFilm::normToWaveform(float norm) {
  if (norm < 0.2f) return Waveform::SINE;
  if (norm < 0.4f) return Waveform::SAW;
  if (norm < 0.6f) return Waveform::SQUARE;
  if (norm < 0.8f) return Waveform::TRIANGLE;
  return Waveform::NOISE;
}

float BuiltinFilm::generateOsc(Waveform wf, double phase) {
  // Normalize phase to [0, 1).
  double p = phase - std::floor(phase);
  switch (wf) {
    case Waveform::SINE:
      return std::sin((float)p * kTwoPi);
    case Waveform::SAW:
      return 2.0f * (float)p - 1.0f;
    case Waveform::SQUARE:
      return p < 0.5 ? 1.0f : -1.0f;
    case Waveform::TRIANGLE:
      return (float)(p < 0.5 ? (4.0 * p - 1.0) : (3.0 - 4.0 * p));
    case Waveform::NOISE:
      return whiteNoise();
  }
  ABSL_UNREACHABLE();
}

float BuiltinFilm::applyShapeModifiers(float sample, double phase, float shape,
                                       float tension, float skew,
                                       float sine_shaper, bool half, bool even,
                                       bool absolute) {
  float s = sample;

  // Shape (SH): 0=original, 0.5=neutral, 1.0=pulse-ish.
  // Morph by power-shaping the waveform.
  if (std::abs(shape - 0.5f) > 0.01f) {
    float amt = (shape - 0.5f) * 4.0f;  // -2..+2
    float sign = s >= 0 ? 1.0f : -1.0f;
    s = sign * std::pow(std::abs(s), std::pow(2.0f, -amt));
  }

  // Tension (TN): waveshaping distortion. 0.5=neutral.
  if (std::abs(tension - 0.5f) > 0.01f) {
    float t = (tension - 0.5f) * 6.0f;  // -3..+3
    if (t > 0) {
      // Soft clip toward square.
      s = std::tanh(s * (1.0f + t * 3.0f));
    } else {
      // Reduce toward sine (power curve).
      float sign = s >= 0 ? 1.0f : -1.0f;
      s = sign * std::pow(std::abs(s), 1.0f - t);
    }
  }

  // Skew (SK): time-axis distortion. 0.5=neutral.
  // Applied implicitly by warping the phase — here we approximate by
  // asymmetric gain on the two halves.
  if (std::abs(skew - 0.5f) > 0.01f) {
    double p = phase - std::floor(phase);
    float sk_amt = (skew - 0.5f) * 2.0f;  // -1..+1
    if (p < 0.5) {
      s *= (1.0f + sk_amt);
    } else {
      s *= (1.0f - sk_amt);
    }
  }

  // Sine Shaper (SN): pass through sin() again. 0=off, 1=full.
  if (sine_shaper > 0.01f) {
    float shaped = std::sin(s * 3.14159265f);
    s = s * (1.0f - sine_shaper) + shaped * sine_shaper;
  }

  // Half: use only first half of phase, zero the second half.
  if (half) {
    double p = phase - std::floor(phase);
    if (p >= 0.5) s = 0;
  }

  // Even: silence odd-numbered phases (creates sub-octave effect).
  if (even) {
    int cycle = (int)std::floor(phase);
    if (cycle % 2 != 0) s = 0;
  }

  // Absolute: rectify (full-wave).
  if (absolute) {
    s = std::abs(s) * 2.0f - 1.0f;  // map [0,1] back to [-1,1] range
  }

  return std::clamp(s, -1.0f, 1.0f);
}

float BuiltinFilm::lfoValue(Waveform wf, double phase) {
  double p = phase - std::floor(phase);
  switch (wf) {
    case Waveform::SINE:
      return std::sin((float)p * kTwoPi);
    case Waveform::TRIANGLE:
      return (float)(p < 0.5 ? (4.0 * p - 1.0) : (3.0 - 4.0 * p));
    case Waveform::SQUARE:
      return p < 0.5 ? 1.0f : -1.0f;
    default:
      return std::sin((float)p * kTwoPi);
  }
}

void BuiltinFilm::noteOn(int pitch, float velocity) {
  int target = -1;
  for (int i = 0; i < kMaxVoices; ++i) {
    bool idle = true;
    for (int o = 0; o < kNumOps; ++o) {
      if (!voices_[i].ops[o].env.isIdle()) {
        idle = false;
        break;
      }
    }
    if (!voices_[i].active && idle) {
      target = i;
      break;
    }
  }
  // Voice stealing: oldest voice.
  if (target < 0) {
    uint64_t min_age = UINT64_MAX;
    for (int i = 0; i < kMaxVoices; ++i) {
      if (voices_[i].age < min_age) {
        min_age = voices_[i].age;
        target = i;
      }
    }
  }
  if (target < 0) target = 0;

  auto& v = voices_[target];
  v.active = true;
  v.note = pitch;
  v.velocity = velocity;
  v.base_freq = 440.0f * std::pow(2.0f, (pitch - 69) / 12.0f);
  v.age = ++voice_counter_;

  for (int o = 0; o < kNumOps; ++o) {
    v.ops[o].phase = 0;
    v.ops[o].prev_output = 0;
    v.ops[o].lfo_phase = 0;
    int base = opBase(o);
    v.ops[o].env.setNormalized(
        params_[base + OP_ENV_A], params_[base + OP_ENV_D],
        params_[base + OP_ENV_S], params_[base + OP_ENV_R]);
    v.ops[o].env.noteOn();
  }

  for (int f = 0; f < kNumFilters; ++f) {
    v.filters[f].filterL.reset();
    v.filters[f].filterR.reset();
    v.filters[f].lfo_phase = 0;
    int fbase = filterBase(f);
    v.filters[f].env.setNormalized(
        params_[fbase + FLT_ENV_A], params_[fbase + FLT_ENV_D],
        params_[fbase + FLT_ENV_S], params_[fbase + FLT_ENV_R]);
    v.filters[f].env.noteOn();
  }
}

void BuiltinFilm::noteOff(int pitch) {
  for (int i = 0; i < kMaxVoices; ++i) {
    if (voices_[i].active && voices_[i].note == pitch) {
      for (int o = 0; o < kNumOps; ++o) {
        voices_[i].ops[o].env.noteOff();
      }
      for (int f = 0; f < kNumFilters; ++f) {
        voices_[i].filters[f].env.noteOff();
      }
      voices_[i].active = false;
    }
  }
}

double BuiltinFilm::getDefaultValue(int id) const {
  // Per-operator defaults.
  if (id < kOpParams) {
    int op = id / kParamsPerOp;
    int p = id % kParamsPerOp;
    switch (p) {
      case OP_WAVEFORM:
        return 0.0;  // sine
      case OP_LEVEL:
        return (op == 0) ? 1.0 : 0.0;  // only op1 audible
      case OP_RATIO:
        return 0.5;  // 1x
      case OP_FINE:
        return 0.5;  // 0 cents
      case OP_ENV_A:
        return 0.0;
      case OP_ENV_D:
        return 0.2;
      case OP_ENV_S:
        return 0.7;
      case OP_ENV_R:
        return 0.3;
      case OP_FEEDBACK:
        return 0.0;
      case OP_PAN:
        return 0.5;  // center
      case OP_LFO_RATE:
        return 0.3;  // ~1 Hz
      case OP_LFO_DEPTH:
        return 0.0;  // off
      case OP_LFO_WAVE:
        return 0.0;  // sine
      case OP_PHASE:
        return 0.0;
      case OP_SHAPE:
        return 0.5;  // neutral
      case OP_TENSION:
        return 0.5;  // neutral
      case OP_SKEW:
        return 0.5;  // neutral
      case OP_SINE_SHAPER:
        return 0.0;  // off
      case OP_NOISE_MIX:
        return 0.0;  // off
      case OP_FREQ_OFFSET:
        return 0.5;  // 0 Hz offset
      case OP_HALF:
        return 0.0;  // off
      case OP_EVEN:
        return 0.0;  // off
      case OP_ABSOLUTE:
        return 0.0;  // off
    }
  }

  // Per-filter defaults.
  if (id < kOpParams + kFilterParams) {
    int p = (id - kOpParams) % kParamsPerFilter;
    switch (p) {
      case FLT_TYPE:
        return 0.0;  // LP
      case FLT_CUTOFF:
        return 1.0;  // wide open
      case FLT_RESONANCE:
        return 0.0;  // no res
      case FLT_ENV_A:
        return 0.0;
      case FLT_ENV_D:
        return 0.2;
      case FLT_ENV_S:
        return 0.7;
      case FLT_ENV_R:
        return 0.3;
      case FLT_ENV_DEPTH:
        return 0.5;  // neutral
      case FLT_MIX:
        return 1.0;  // full wet
      case FLT_LFO_RATE:
        return 0.3;
      case FLT_LFO_DEPTH:
        return 0.0;
      case FLT_LFO_WAVE:
        return 0.0;
    }
  }

  // Global defaults.
  if (id == G_ALGORITHM) return 0.0;
  if (id == G_MASTER_VOL) return 0.8;
  if (id == G_ENABLE) return 1.0;
  if (id == G_UNISON_VOICES) return 0.0;  // 1 voice
  if (id == G_UNISON_DETUNE) return 0.0;
  if (id == G_UNISON_SPREAD) return 0.5;
  if (id == G_PORTAMENTO) return 0.0;

  // Matrix defaults: 0.5 = neutral (no modulation/send).
  if (id >= kMatrixBase) {
    int mi = id - kMatrixBase;
    int col = mi % kMatrixCols;
    // Direct output col for op1 defaults to 0.75 so op1 is audible.
    if (col == 11 && mi / kMatrixCols == 0) return 0.75;
    return 0.5;
  }

  return 0;
}

void BuiltinFilm::reset() {
  for (int i = 0; i < kTotalParams; ++i) {
    params_[i] = (float)getDefaultValue(i);
  }
  enabled_ = true;
  voice_counter_ = 0;
  for (auto& v : voices_) {
    v.active = false;
    v.note = -1;
    for (int o = 0; o < kNumOps; ++o) {
      v.ops[o].phase = 0;
      v.ops[o].prev_output = 0;
      v.ops[o].lfo_phase = 0;
    }
    for (int f = 0; f < kNumFilters; ++f) {
      v.filters[f].filterL.reset();
      v.filters[f].filterR.reset();
      v.filters[f].lfo_phase = 0;
    }
  }
}

}  // namespace hibiki
