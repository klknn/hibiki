module engine.core.modulator;

import core.stdc.math : floor, sin;
import engine.core.math : pi;

/// Real-time @nogc LFO modulator for plugin parameters.
struct ModulatorD {
    enum int kMaxSlots = 3;

    enum Waveform : int {
        SINE = 0,
        SAW = 1,
        SQUARE = 2,
        RANDOM = 3,
    }

    Waveform waveform = Waveform.SINE;
    float rate_hz = 1.0f;
    float depth = 0.0f;
    int plugin_idx = -1;
    uint param_id = 0;
    bool assigned = false;
    bool sync_to_tempo = false;

    double phase = 0.0;
    float last_random = 0.0f;
    double random_phase_prev = 0.0;
    uint rng_state = 42;

    float tick(int num_samples, double sample_rate, double tempo) @nogc nothrow {
        if (!assigned || depth == 0.0f || sample_rate <= 0.0) {
            return 0.0f;
        }
        double freq = rate_hz;
        if (sync_to_tempo && tempo > 0.0 && rate_hz > 0.0f) {
            freq = (tempo / 60.0) / (rate_hz * 4.0);
        }
        double phase_inc = freq * num_samples / sample_rate;
        phase += phase_inc;
        if (phase >= 1.0) {
            phase -= floor(phase);
        }

        float val = 0.0f;
        final switch (waveform) {
            case Waveform.SINE:
                val = cast(float)sin(phase * 2.0 * pi);
                break;
            case Waveform.SAW:
                val = cast(float)(2.0 * phase - 1.0);
                break;
            case Waveform.SQUARE:
                val = (phase < 0.5) ? 1.0f : -1.0f;
                break;
            case Waveform.RANDOM:
                if (phase < random_phase_prev) {
                    // Xorshift32 PRNG for deterministic @nogc S&H
                    uint x = rng_state ? rng_state : 42;
                    x ^= x << 13;
                    x ^= x >> 17;
                    x ^= x << 5;
                    rng_state = x;
                    last_random = (cast(float)(x & 0xFFFF) / 32767.5f) - 1.0f;
                }
                random_phase_prev = phase;
                val = last_random;
                break;
        }
        return val * depth;
    }

    void reset() @nogc nothrow pure @safe {
        phase = 0.0;
        last_random = 0.0f;
        random_phase_prev = 0.0;
    }
}
