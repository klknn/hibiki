module engine.instruments.builtin_dr8_clap;

import core.stdc.math : exp, pow;
import engine.core.biquad_filter : BiquadFilter;
import engine.core.math : clampVal;

extern (C++, "hibiki") {

enum kTotalDr8ClapParams = 4;

enum Dr8ClapParamId {
    PARAM_DECAY = 0,
    PARAM_FILTER_CUTOFF = 1,
    PARAM_SPREAD = 2,
    PARAM_VOLUME = 3,
}

__gshared immutable double[kTotalDr8ClapParams] kDefaultDr8ClapParams = [
    0.3,      // DECAY
    0.3,      // FILTER_CUTOFF
    0.466667, // SPREAD
    0.7,      // VOLUME
];

struct Dr8ClapMidiEvent {
    int pitch;
    float velocity;
    bool is_note_on;
}

struct Dr8ClapDspState {
    double[kTotalDr8ClapParams] params = 0.0;
    double sample_rate = 44100.0;

    bool active = false;
    int pre_clap_index = -1;
    int sample_since_last_trigger = 0;
    double env_amp = 0.0;
    float velocity = 0.0f;
    uint rng_state = 0x5a5a5a5a;

    BiquadFilter bpf;

    void reset() nothrow @nogc @safe {
        active = false;
        pre_clap_index = -1;
        sample_since_last_trigger = 0;
        env_amp = 0.0;
        velocity = 0.0f;
        rng_state = 0x5a5a5a5a;
        bpf.reset();
        foreach (i; 0 .. kTotalDr8ClapParams) {
            params[i] = kDefaultDr8ClapParams[i];
        }
    }

    float nextRandomFloat() nothrow @nogc @safe {
        rng_state ^= (rng_state << 13);
        rng_state ^= (rng_state >> 17);
        rng_state ^= (rng_state << 5);
        return (cast(float)(rng_state & 0x7FFFFFFF) / 1073741824.0f) - 1.0f;
    }
}

nothrow @nogc @trusted float d_dr8_clap_norm_to_decay_s(double norm) {
    return cast(float)(0.05 * pow(20.0, norm));
}

nothrow @nogc @trusted float d_dr8_clap_norm_to_cutoff_hz(double norm) {
    return cast(float)(500.0 * pow(6.0, norm));
}

nothrow @nogc @safe float d_dr8_clap_norm_to_spread_s(double norm) {
    return cast(float)(0.005 + norm * 0.015);
}

Dr8ClapDspState* d_dr8_clap_create() nothrow @nogc @trusted {
    import core.stdc.stdlib : malloc;
    auto s = cast(Dr8ClapDspState*)malloc(Dr8ClapDspState.sizeof);
    if (s !is null) {
        *s = Dr8ClapDspState.init;
        s.reset();
    }
    return s;
}

void d_dr8_clap_destroy(Dr8ClapDspState* state) nothrow @nogc @trusted {
    import core.stdc.stdlib : free;
    if (state !is null) free(state);
}

void d_dr8_clap_reset(Dr8ClapDspState* state) nothrow @nogc @safe {
    if (state !is null) state.reset();
}

void d_dr8_clap_set_parameter(Dr8ClapDspState* state, uint id, double value) nothrow @nogc @safe {
    if (state !is null && id < kTotalDr8ClapParams) {
        state.params[id] = clampVal(value, 0.0, 1.0);
    }
}

double d_dr8_clap_get_parameter(const(Dr8ClapDspState)* state, uint id) nothrow @nogc @safe {
    if (state !is null && id < kTotalDr8ClapParams) {
        return state.params[id];
    }
    return 0.0;
}

void d_dr8_clap_process(
    Dr8ClapDspState* state,
    float* out_l,
    float* out_r,
    int num_samples,
    double sample_rate,
    const(Dr8ClapMidiEvent)* events,
    int num_events
) nothrow @nogc @trusted {
    if (state is null) return;
    state.sample_rate = sample_rate;

    if (events !is null) {
        foreach (i; 0 .. num_events) {
            if (events[i].is_note_on && events[i].velocity > 0) {
                state.active = true;
                state.pre_clap_index = 0;
                state.sample_since_last_trigger = 0;
                state.env_amp = 1.0;
                state.velocity = events[i].velocity;
            }
        }
    }

    foreach (i; 0 .. num_samples) {
        out_l[i] = 0.0f;
        out_r[i] = 0.0f;
    }

    if (!state.active) return;

    double decay_s = d_dr8_clap_norm_to_decay_s(state.params[Dr8ClapParamId.PARAM_DECAY]);
    double cutoff_fc = d_dr8_clap_norm_to_cutoff_hz(state.params[Dr8ClapParamId.PARAM_FILTER_CUTOFF]);
    double spread_s = d_dr8_clap_norm_to_spread_s(state.params[Dr8ClapParamId.PARAM_SPREAD]);
    double volume = state.params[Dr8ClapParamId.PARAM_VOLUME];

    int spread_samples = cast(int)(spread_s * state.sample_rate);
    if (spread_samples < 1) spread_samples = 1;

    double pre_clap_decay_coeff = exp(-1.0 / (0.010 * state.sample_rate));
    double main_decay_coeff = exp(-1.0 / (decay_s * state.sample_rate));

    state.bpf.setParams(BiquadFilter.Type.BANDPASS, cast(float)cutoff_fc, 1.2f, 0.0f, cast(float)state.sample_rate);

    foreach (i; 0 .. num_samples) {
        if (state.pre_clap_index >= 0 && state.pre_clap_index < 3) {
            if (state.sample_since_last_trigger >= spread_samples) {
                state.pre_clap_index++;
                state.sample_since_last_trigger = 0;
                state.env_amp = 1.0;
            }
        }

        if (state.pre_clap_index >= 3 && state.env_amp < 0.0001) {
            state.active = false;
            state.reset();
            break;
        }

        double noise = state.nextRandomFloat();
        double raw = noise * state.env_amp;
        float filtered = state.bpf.process(cast(float)raw);
        float out_val = cast(float)(filtered * state.velocity * volume);

        out_l[i] = out_val;
        out_r[i] = out_val;

        if (state.pre_clap_index < 3) {
            state.env_amp *= pre_clap_decay_coeff;
        } else {
            state.env_amp *= main_decay_coeff;
        }

        state.sample_since_last_trigger++;
    }
}

}
