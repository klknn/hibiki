#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <random>
#include <string>

namespace hibiki {

// Per-slot LFO modulator for plugin parameters.
// Each plugin can have up to kMaxSlots modulators running simultaneously.
struct Modulator {
  static constexpr int kMaxSlots = 3;

  enum Waveform { SINE = 0, SAW = 1, SQUARE = 2, RANDOM = 3 };

  Waveform waveform = SINE;
  float rate_hz = 1.0f;        // LFO frequency (Hz, or beat-relative if synced)
  float depth = 0.0f;          // -1.0 to 1.0 modulation scale
  int plugin_idx = -1;         // target plugin index on the track
  uint32_t param_id = 0;       // target parameter ID
  bool assigned = false;       // true if this slot targets a param
  bool sync_to_tempo = false;  // if true, rate is in beat divisions
  std::string param_name;      // human-readable param name (for UI)

  // Runtime state (audio thread only)
  double phase = 0.0;              // 0.0 - 1.0 free-running phase
  float last_random = 0.0f;        // S&H value for RANDOM waveform
  double random_phase_prev = 0.0;  // previous phase for edge detection

  // Advance phase by one block and return the modulation offset
  // (-depth..+depth)
  float tick(int num_samples, double sample_rate, double tempo) {
    if (!assigned || depth == 0.0f) return 0.0f;

    double freq = rate_hz;
    if (sync_to_tempo && tempo > 0) {
      // rate_hz represents beat divisions: 1.0 = 1 bar (4 beats)
      freq = (tempo / 60.0) / (rate_hz * 4.0);
    }

    // Advance phase
    double phase_inc = freq * num_samples / sample_rate;
    phase += phase_inc;
    if (phase >= 1.0) phase -= std::floor(phase);

    // Generate waveform value in [-1, 1]
    float val = 0.0f;
    switch (waveform) {
      case SINE:
        val = (float)std::sin(phase * 2.0 * std::numbers::pi_v<double>);
        break;
      case SAW:
        val = (float)(2.0 * phase - 1.0);
        break;
      case SQUARE:
        val = phase < 0.5 ? 1.0f : -1.0f;
        break;
      case RANDOM: {
        // Sample-and-hold: new random value each cycle
        if (phase < random_phase_prev) {
          // Phase wrapped — generate new S&H value
          static thread_local std::mt19937 rng(42);
          std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
          last_random = dist(rng);
        }
        random_phase_prev = phase;
        val = last_random;
        break;
      }
    }

    return val * depth;
  }

  void reset() {
    phase = 0.0;
    last_random = 0.0f;
    random_phase_prev = 0.0;
  }
};

// Per-plugin modulation storage (up to kMaxSlots modulators)
struct PluginModulation {
  Modulator slots[Modulator::kMaxSlots];
};

}  // namespace hibiki
