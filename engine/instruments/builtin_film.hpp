#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "engine/core/biquad_filter.hpp"
#include "engine/instruments/adsr.hpp"
#include "engine/plugin/iplugin.hpp"

namespace hibiki {

// FilM: 6-operator FM synthesizer with 3 filter modules and modulation matrix.
// Inspired by Sytrus/FM8. Supports FM and RM modes, per-op waveform selection,
// per-op/filter LFO, unison, and 8-voice polyphony.
class BuiltinFilm : public IPlugin {
 public:
  static constexpr int kMaxVoices = 8;
  static constexpr int kNumOps = 6;
  static constexpr int kNumFilters = 3;
  static constexpr int kParamsPerOp = 23;
  static constexpr int kParamsPerFilter = 12;
  static constexpr int kOpParams = kNumOps * kParamsPerOp;              // 138
  static constexpr int kFilterParams = kNumFilters * kParamsPerFilter;  // 36
  static constexpr int kGlobalParams = 7;
  static constexpr int kMatrixCols = 12;  // 6 ops + 3 filters + pan + fx + out
  static constexpr int kMatrixParams = kNumOps * kMatrixCols;  // 72
  static constexpr int kTotalParams =
      kOpParams + kFilterParams + kGlobalParams + kMatrixParams;  // 253

  static constexpr const char* kPath = "builtin://film";
  static constexpr const char* kName = "FilM";

  // Waveform types per operator.
  enum class Waveform { SINE = 0, SAW, SQUARE, TRIANGLE, NOISE };

  // --- Per-operator param offsets (relative to op base) ---
  enum OpParam {
    OP_WAVEFORM = 0,
    OP_LEVEL = 1,
    OP_RATIO = 2,
    OP_FINE = 3,
    OP_ENV_A = 4,
    OP_ENV_D = 5,
    OP_ENV_S = 6,
    OP_ENV_R = 7,
    OP_FEEDBACK = 8,
    OP_PAN = 9,
    OP_LFO_RATE = 10,
    OP_LFO_DEPTH = 11,
    OP_LFO_WAVE = 12,
    OP_PHASE = 13,
    // Sytrus shape modifiers
    OP_SHAPE = 14,        // SH: morph sine→triangle→pulse
    OP_TENSION = 15,      // TN: distortion/tension
    OP_SKEW = 16,         // SK: time-axis skew
    OP_SINE_SHAPER = 17,  // SN: sine shaper transform
    OP_NOISE_MIX = 18,    // NS: noise mix amount
    OP_FREQ_OFFSET = 19,  // Hz offset added to pitch
    // Phase modes
    OP_HALF = 20,      // Half: use only first half of phase
    OP_EVEN = 21,      // Even: silence odd phases
    OP_ABSOLUTE = 22,  // Absolute: rectify waveform
  };

  // --- Per-filter param offsets (relative to filter base) ---
  enum FilterParam {
    FLT_TYPE = 0,
    FLT_CUTOFF = 1,
    FLT_RESONANCE = 2,
    FLT_ENV_A = 3,
    FLT_ENV_D = 4,
    FLT_ENV_S = 5,
    FLT_ENV_R = 6,
    FLT_ENV_DEPTH = 7,
    FLT_MIX = 8,
    FLT_LFO_RATE = 9,
    FLT_LFO_DEPTH = 10,
    FLT_LFO_WAVE = 11,
  };

  // --- Global param offsets ---
  enum GlobalParam {
    G_ALGORITHM = kOpParams + kFilterParams,       // 120
    G_MASTER_VOL = kOpParams + kFilterParams + 1,  // 121
    G_ENABLE = kOpParams + kFilterParams + 2,      // 122
    G_UNISON_VOICES = kOpParams + kFilterParams + 3,
    G_UNISON_DETUNE = kOpParams + kFilterParams + 4,
    G_UNISON_SPREAD = kOpParams + kFilterParams + 5,
    G_PORTAMENTO = kOpParams + kFilterParams + 6,
  };

  // Mod matrix base offset.
  static constexpr int kMatrixBase =
      kOpParams + kFilterParams + kGlobalParams;  // 181

  // DX7 SysEx support.
  struct Dx7Voice {
    char name[11];      // 10 chars + null
    uint8_t data[128];  // packed voice data
  };

  // Parse a 32-voice bulk dump (.syx file). Returns number of voices parsed.
  static int parseDx7Sysex(const uint8_t* data, size_t len,
                           Dx7Voice voices[32]);
  // Apply a parsed DX7 voice to this instance's parameters.
  void loadDx7Voice(const Dx7Voice& voice);
  // Convenience: parse sysex and return patch names.
  static std::vector<std::string> getDx7PatchNames(const uint8_t* data,
                                                   size_t len);

  BuiltinFilm();

  // IPlugin interface
  bool load(const std::string& path, int plugin_index = 0,
            double sample_rate = 44100.0) override;
  void showEditor() override {}
  void stopEditor() override {}
  void process(float** inputs, float** outputs, int num_samples,
               const HostProcessContext& context,
               const std::vector<MidiNoteEvent>& events,
               float** sidechain = nullptr) override;
  int getParameterCount() const override;
  bool getParameterInfo(int index, VstParamInfo& info) const override;
  void setParameterValue(uint32_t id, double value) override;
  double getParameterValue(uint32_t id) const override;
  const std::string& getName() const override;
  const std::string& getPath() const override;
  int getPluginIndex() const override;
  bool isInstrument() const override;

 private:
  struct OpState {
    double phase = 0;
    float prev_output = 0;  // for feedback
    Adsr env;
    double lfo_phase = 0;
  };

  struct FilterState {
    BiquadFilter filterL, filterR;
    Adsr env;
    double lfo_phase = 0;
  };

  struct Voice {
    bool active = false;
    int note = -1;
    float velocity = 0;
    float base_freq = 0;
    OpState ops[kNumOps];
    FilterState filters[kNumFilters];
    uint64_t age = 0;
  };

  static Waveform normToWaveform(float norm);
  static float generateOsc(Waveform wf, double phase);
  static float applyShapeModifiers(float sample, double phase, float shape,
                                   float tension, float skew, float sine_shaper,
                                   bool half, bool even, bool absolute);
  static float lfoValue(Waveform wf, double phase);

  void noteOn(int pitch, float velocity);
  void noteOff(int pitch);
  double getDefaultValue(int id) const;
  void reset();

  // Param access helpers.
  int opBase(int op) const { return op * kParamsPerOp; }
  int filterBase(int flt) const { return kOpParams + flt * kParamsPerFilter; }
  int matrixIdx(int row, int col) const {
    return kMatrixBase + row * kMatrixCols + col;
  }

  double sample_rate_ = 44100.0;
  bool enabled_ = true;
  float params_[kTotalParams] = {};
  Voice voices_[kMaxVoices];
  uint64_t voice_counter_ = 0;
};

}  // namespace hibiki
