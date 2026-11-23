module engine.effects.builtin_vocodey;

import core.stdc.math : exp, fabs, pow, tanh;
import engine.core.biquad_filter : BiquadFilter;
import engine.core.math : clampVal;

extern (C++, "hibiki") {

enum kTotalVocodeyParams = 8;
enum kMaxVocodeyVoices = 16;
enum kNumVocodeyBands = 24;

enum VocodeyParamId {
    PARAM_ATTACK = 0,
    PARAM_DECAY = 1,
    PARAM_BANDWIDTH = 2,
    PARAM_NOISE_BLEED = 3,
    PARAM_SYNTH_DETUNE = 4,
    PARAM_DRY = 5,
    PARAM_WET = 6,
    PARAM_VOLUME = 7,
}

__gshared immutable double[kTotalVocodeyParams] kDefaultVocodeyParams = [
    0.15, // Attack (~5ms)
    0.25, // Decay (~150ms)
    0.35, // Bandwidth (Q ~4.0)
    0.3,  // Noise Bleed
    0.15, // Detune
    0.0,  // Dry
    1.0,  // Wet
    0.7,  // Volume
];

struct VocodeyMidiEvent {
    int pitch;
    float velocity;
    bool is_note_on;
}

struct VocodeySynthVoice {
    bool active = false;
    int pitch = 0;
    double phase1 = 0.0;
    double phase2 = 0.0;
    double amp = 0.0;

    void reset() nothrow @nogc @safe {
        active = false;
        pitch = 0;
        phase1 = 0.0;
        phase2 = 0.0;
        amp = 0.0;
    }
}

pure nothrow @nogc @safe double poly_blep(double t, double dt) {
    if (t < dt) {
        double x = t / dt;
        return 2.0 * x - x * x - 1.0;
    } else if (t > 1.0 - dt) {
        double x = (t - 1.0) / dt;
        return x * x + 2.0 * x + 1.0;
    }
    return 0.0;
}

pure nothrow @nogc @safe float d_vocodey_norm_to_attack_s(double norm) {
    return cast(float)(0.001 + norm * 0.099);
}

pure nothrow @nogc @safe float d_vocodey_norm_to_decay_s(double norm) {
    return cast(float)(0.01 + norm * 1.99);
}

pure nothrow @nogc @safe float d_vocodey_norm_to_bandwidth_q(double norm) {
    return cast(float)(0.5 + norm * 9.5);
}

struct VocodeyDspState {
    double[kTotalVocodeyParams] params = 0.0;
    double sample_rate = 44100.0;

    VocodeySynthVoice[kMaxVocodeyVoices] voices;

    double[kNumVocodeyBands] band_freqs = 0.0;
    BiquadFilter[kNumVocodeyBands] mod_filter_l;
    BiquadFilter[kNumVocodeyBands] mod_filter_r;
    BiquadFilter[kNumVocodeyBands] car_filter_l;
    BiquadFilter[kNumVocodeyBands] car_filter_r;
    double[kNumVocodeyBands] env_mod_l = 0.0;
    double[kNumVocodeyBands] env_mod_r = 0.0;

    uint rng_state = 0xbeefcafe;

    float nextRandomFloat() nothrow @nogc @safe {
        rng_state ^= (rng_state << 13);
        rng_state ^= (rng_state >> 17);
        rng_state ^= (rng_state << 5);
        return (cast(float)(rng_state & 0x7FFFFFFF) / 1073741824.0f) - 1.0f;
    }

    void initBandFreqs() nothrow @nogc @trusted {
        double f_min = 80.0;
        double f_max = 12000.0;
        foreach (j; 0 .. kNumVocodeyBands) {
            band_freqs[j] = f_min * pow(f_max / f_min, j / 23.0);
        }
    }

    void reset() nothrow @nogc @safe {
        foreach (i; 0 .. kMaxVocodeyVoices) {
            voices[i].reset();
        }
        foreach (j; 0 .. kNumVocodeyBands) {
            mod_filter_l[j].reset();
            mod_filter_r[j].reset();
            car_filter_l[j].reset();
            car_filter_r[j].reset();
            env_mod_l[j] = 0.0;
            env_mod_r[j] = 0.0;
        }
        rng_state = 0xbeefcafe;
        foreach (i; 0 .. kTotalVocodeyParams) {
            params[i] = kDefaultVocodeyParams[i];
        }
    }
}

VocodeyDspState* d_vocodey_create() nothrow @nogc @trusted {
    import core.stdc.stdlib : malloc;
    auto s = cast(VocodeyDspState*)malloc(VocodeyDspState.sizeof);
    if (s !is null) {
        *s = VocodeyDspState.init;
        s.initBandFreqs();
        s.reset();
    }
    return s;
}

void d_vocodey_destroy(VocodeyDspState* state) nothrow @nogc @trusted {
    import core.stdc.stdlib : free;
    if (state !is null) free(state);
}

void d_vocodey_reset(VocodeyDspState* state) nothrow @nogc @safe {
    if (state !is null) state.reset();
}

void d_vocodey_set_parameter(VocodeyDspState* state, uint id, double value) nothrow @nogc @safe {
    if (state !is null && id < kTotalVocodeyParams) {
        state.params[id] = clampVal(value, 0.0, 1.0);
    }
}

double d_vocodey_get_parameter(const VocodeyDspState* state, uint id) nothrow @nogc @safe {
    if (state !is null && id < kTotalVocodeyParams) {
        return state.params[id];
    }
    return 0.0;
}

void d_vocodey_process(
    VocodeyDspState* state,
    const(float)* in_l,
    const(float)* in_r,
    float* out_l,
    float* out_r,
    int num_samples,
    double sample_rate,
    const(VocodeyMidiEvent)* events,
    int num_events,
    const(float)* sc_l,
    const(float)* sc_r
) nothrow @nogc @trusted {
    if (state is null) return;
    state.sample_rate = sample_rate;

    if (events !is null) {
        foreach (i; 0 .. num_events) {
            const auto ev = events[i];
            if (ev.is_note_on) {
                if (ev.velocity > 0.0f) {
                    int free_idx = -1;
                    foreach (v_idx; 0 .. kMaxVocodeyVoices) {
                        if (state.voices[v_idx].active && state.voices[v_idx].pitch == ev.pitch) {
                            free_idx = v_idx;
                            break;
                        }
                    }
                    if (free_idx == -1) {
                        foreach (v_idx; 0 .. kMaxVocodeyVoices) {
                            if (!state.voices[v_idx].active && state.voices[v_idx].amp < 0.001) {
                                free_idx = v_idx;
                                break;
                            }
                        }
                    }
                    if (free_idx == -1) free_idx = 0;
                    state.voices[free_idx].active = true;
                    state.voices[free_idx].pitch = ev.pitch;
                    state.voices[free_idx].phase1 = 0.0;
                    state.voices[free_idx].phase2 = 0.0;
                    state.voices[free_idx].amp = ev.velocity;
                } else {
                    foreach (v_idx; 0 .. kMaxVocodeyVoices) {
                        if (state.voices[v_idx].pitch == ev.pitch) {
                            state.voices[v_idx].active = false;
                        }
                    }
                }
            }
        }
    }

    double att_s = d_vocodey_norm_to_attack_s(state.params[VocodeyParamId.PARAM_ATTACK]);
    double dec_s = d_vocodey_norm_to_decay_s(state.params[VocodeyParamId.PARAM_DECAY]);
    double q = d_vocodey_norm_to_bandwidth_q(state.params[VocodeyParamId.PARAM_BANDWIDTH]);
    double noise_bleed = state.params[VocodeyParamId.PARAM_NOISE_BLEED];
    double detune_depth = state.params[VocodeyParamId.PARAM_SYNTH_DETUNE];
    double dry_gain = state.params[VocodeyParamId.PARAM_DRY];
    double wet_gain = state.params[VocodeyParamId.PARAM_WET];
    double volume = state.params[VocodeyParamId.PARAM_VOLUME];

    double att_coeff = exp(-1.0 / (att_s * state.sample_rate));
    double rel_coeff = exp(-1.0 / (dec_s * state.sample_rate));
    double synth_release_coeff = exp(-1.0 / (0.008 * state.sample_rate));

    foreach (j; 0 .. kNumVocodeyBands) {
        float freq = cast(float)state.band_freqs[j];
        state.mod_filter_l[j].setParams(BiquadFilter.Type.BANDPASS, freq, cast(float)q, 0.0f, cast(float)state.sample_rate);
        state.mod_filter_r[j].setParams(BiquadFilter.Type.BANDPASS, freq, cast(float)q, 0.0f, cast(float)state.sample_rate);
        state.car_filter_l[j].setParams(BiquadFilter.Type.BANDPASS, freq, cast(float)q, 0.0f, cast(float)state.sample_rate);
        state.car_filter_r[j].setParams(BiquadFilter.Type.BANDPASS, freq, cast(float)q, 0.0f, cast(float)state.sample_rate);
    }

    bool use_sidechain = (sc_l !is null);

    foreach (i; 0 .. num_samples) {
        double modulator_l = in_l !is null ? in_l[i] : out_l[i];
        double modulator_r = in_r !is null ? in_r[i] : out_r[i];

        double carrier_l = 0.0;
        double carrier_r = 0.0;

        if (use_sidechain) {
            carrier_l = sc_l[i];
            carrier_r = (sc_r !is null) ? sc_r[i] : sc_l[i];
        } else {
            double synth_l = 0.0;
            double synth_r = 0.0;

            foreach (v_idx; 0 .. kMaxVocodeyVoices) {
                auto v = &state.voices[v_idx];
                if (v.amp < 0.0001) {
                    v.active = false;
                    v.amp = 0.0;
                    continue;
                }

                double midi_pitch = v.pitch;
                double f_base = 440.0 * pow(2.0, (midi_pitch - 69.0) / 12.0);

                double detune_semitones = detune_depth * 0.4;
                double f1 = f_base * pow(2.0, -detune_semitones / 12.0);
                double f2 = f_base * pow(2.0, detune_semitones / 12.0);

                double dt1 = f1 / state.sample_rate;
                double dt2 = f2 / state.sample_rate;

                v.phase1 += dt1;
                if (v.phase1 >= 1.0) v.phase1 -= 1.0;
                double saw1 = 2.0 * v.phase1 - 1.0;
                saw1 -= poly_blep(v.phase1, dt1);

                v.phase2 += dt2;
                if (v.phase2 >= 1.0) v.phase2 -= 1.0;
                double saw2 = 2.0 * v.phase2 - 1.0;
                saw2 -= poly_blep(v.phase2, dt2);

                synth_l += saw1 * v.amp;
                synth_r += saw2 * v.amp;

                if (!v.active) {
                    v.amp *= synth_release_coeff;
                }
            }

            carrier_l = tanh(synth_l);
            carrier_r = tanh(synth_r);
        }

        double vocoded_sum_l = 0.0;
        double vocoded_sum_r = 0.0;

        double noise = state.nextRandomFloat();

        double active_noise_bleed = noise_bleed;
        if (!use_sidechain) {
            double total_amp = 0.0;
            foreach (v_idx; 0 .. kMaxVocodeyVoices) {
                total_amp += state.voices[v_idx].amp;
            }
            if (total_amp < 0.001) {
                active_noise_bleed = 0.0;
            } else {
                active_noise_bleed = noise_bleed * (total_amp < 1.0 ? total_amp : 1.0);
            }
        }

        foreach (j; 0 .. kNumVocodeyBands) {
            float mod_bp_l = state.mod_filter_l[j].process(cast(float)modulator_l);
            float mod_bp_r = state.mod_filter_r[j].process(cast(float)modulator_r);

            double rect_l = fabs(mod_bp_l);
            double rect_r = fabs(mod_bp_r);

            if (rect_l > state.env_mod_l[j]) {
                state.env_mod_l[j] = rect_l * (1.0 - att_coeff) + state.env_mod_l[j] * att_coeff;
            } else {
                state.env_mod_l[j] = rect_l * (1.0 - rel_coeff) + state.env_mod_l[j] * rel_coeff;
            }

            if (rect_r > state.env_mod_r[j]) {
                state.env_mod_r[j] = rect_r * (1.0 - att_coeff) + state.env_mod_r[j] * att_coeff;
            } else {
                state.env_mod_r[j] = rect_r * (1.0 - rel_coeff) + state.env_mod_r[j] * rel_coeff;
            }

            double band_carrier_l = carrier_l;
            double band_carrier_r = carrier_r;

            if (j >= 16) {
                band_carrier_l = (1.0 - active_noise_bleed) * carrier_l + active_noise_bleed * noise;
                band_carrier_r = (1.0 - active_noise_bleed) * carrier_r + active_noise_bleed * noise;
            }

            float car_bp_l = state.car_filter_l[j].process(cast(float)band_carrier_l);
            float car_bp_r = state.car_filter_r[j].process(cast(float)band_carrier_r);

            vocoded_sum_l += car_bp_l * state.env_mod_l[j];
            vocoded_sum_r += car_bp_r * state.env_mod_r[j];
        }

        double wet_l = vocoded_sum_l * 2.0;
        double wet_r = vocoded_sum_r * 2.0;

        out_l[i] = cast(float)((dry_gain * modulator_l + wet_gain * wet_l) * volume);
        out_r[i] = cast(float)((dry_gain * modulator_r + wet_gain * wet_r) * volume);
    }
}

}
