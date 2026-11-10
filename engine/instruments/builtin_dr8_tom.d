module engine.instruments.builtin_dr8_tom;

import core.stdc.math : exp, pow, sin;
import engine.core.biquad_filter : BiquadFilter;
import engine.core.math : clampVal, pi;

extern (C++, "hibiki") {

enum kTotalDr8TomParams = 6;

enum Dr8TomParamId {
    PARAM_PITCH = 0,
    PARAM_DECAY = 1,
    PARAM_PITCH_ENV_DECAY = 2,
    PARAM_PITCH_ENV_DEPTH = 3,
    PARAM_NOISE_ATTACK = 4,
    PARAM_VOLUME = 5,
}

__gshared immutable double[kTotalDr8TomParams] kDefaultDr8TomParams = [
    0.3,  // PITCH (~100 Hz)
    0.4,  // DECAY (~400ms)
    0.4,  // PITCH_ENV_DECAY (~100ms)
    0.4,  // PITCH_ENV_DEPTH (~40 Hz)
    0.15, // NOISE_ATTACK
    0.7,  // VOLUME
];

struct Dr8TomMidiEvent {
    int pitch;
    float velocity;
    bool is_note_on;
}

struct Dr8TomDspState {
    double[kTotalDr8TomParams] params = 0.0;
    double sample_rate = 44100.0;

    bool active = false;
    double osc_phase = 0.0;
    double env_amp = 0.0;
    double env_pitch = 0.0;
    double env_click = 0.0;
    float velocity = 0.0f;
    uint rng_state = 0x6789abcd;

    BiquadFilter lpf;

    void reset() nothrow @nogc @safe {
        active = false;
        osc_phase = 0.0;
        env_amp = 0.0;
        env_pitch = 0.0;
        env_click = 0.0;
        velocity = 0.0f;
        rng_state = 0x6789abcd;
        lpf.reset();
        foreach (i; 0 .. kTotalDr8TomParams) {
            params[i] = kDefaultDr8TomParams[i];
        }
    }

    float nextRandomFloat() nothrow @nogc @safe {
        rng_state ^= (rng_state << 13);
        rng_state ^= (rng_state >> 17);
        rng_state ^= (rng_state << 5);
        return (cast(float)(rng_state & 0x7FFFFFFF) / 1073741824.0f) - 1.0f;
    }
}

pure nothrow @nogc @safe float d_dr8_tom_norm_to_pitch_hz(double norm) {
    return cast(float)(70.0 + norm * 130.0);
}

nothrow @nogc @trusted float d_dr8_tom_norm_to_decay_s(double norm) {
    return cast(float)(0.1 * pow(15.0, norm));
}

pure nothrow @nogc @safe float d_dr8_tom_norm_to_pitch_env_decay_s(double norm) {
    return cast(float)(0.02 + norm * 0.28);
}

pure nothrow @nogc @safe float d_dr8_tom_norm_to_pitch_env_depth_hz(double norm) {
    return cast(float)(norm * 100.0);
}

Dr8TomDspState* d_dr8_tom_create() nothrow @nogc @trusted {
    import core.stdc.stdlib : malloc;
    auto s = cast(Dr8TomDspState*)malloc(Dr8TomDspState.sizeof);
    if (s !is null) {
        *s = Dr8TomDspState.init;
        s.reset();
    }
    return s;
}

void d_dr8_tom_destroy(Dr8TomDspState* state) nothrow @nogc @trusted {
    import core.stdc.stdlib : free;
    if (state !is null) free(state);
}

void d_dr8_tom_reset(Dr8TomDspState* state) nothrow @nogc @safe {
    if (state !is null) state.reset();
}

void d_dr8_tom_set_parameter(Dr8TomDspState* state, uint id, double value) nothrow @nogc @safe {
    if (state !is null && id < kTotalDr8TomParams) {
        state.params[id] = clampVal(value, 0.0, 1.0);
    }
}

double d_dr8_tom_get_parameter(const Dr8TomDspState* state, uint id) nothrow @nogc @safe {
    if (state !is null && id < kTotalDr8TomParams) {
        return state.params[id];
    }
    return 0.0;
}

void d_dr8_tom_process(
    Dr8TomDspState* state,
    float* out_l,
    float* out_r,
    int num_samples,
    double sample_rate,
    const(Dr8TomMidiEvent)* events,
    int num_events
) nothrow @nogc @trusted {
    if (state is null) return;
    state.sample_rate = sample_rate;

    if (events !is null) {
        foreach (i; 0 .. num_events) {
            if (events[i].is_note_on && events[i].velocity > 0.0f) {
                state.active = true;
                state.env_amp = 1.0;
                state.env_pitch = 1.0;
                state.env_click = 1.0;
                state.osc_phase = 0.0;
                state.velocity = events[i].velocity;
            }
        }
    }

    foreach (i; 0 .. num_samples) {
        out_l[i] = 0.0f;
        out_r[i] = 0.0f;
    }

    if (!state.active) return;

    double base_freq = d_dr8_tom_norm_to_pitch_hz(state.params[Dr8TomParamId.PARAM_PITCH]);
    double decay_s = d_dr8_tom_norm_to_decay_s(state.params[Dr8TomParamId.PARAM_DECAY]);
    double pitch_decay_s = d_dr8_tom_norm_to_pitch_env_decay_s(state.params[Dr8TomParamId.PARAM_PITCH_ENV_DECAY]);
    double pitch_depth = d_dr8_tom_norm_to_pitch_env_depth_hz(state.params[Dr8TomParamId.PARAM_PITCH_ENV_DEPTH]);
    double click_level = state.params[Dr8TomParamId.PARAM_NOISE_ATTACK];
    double volume = state.params[Dr8TomParamId.PARAM_VOLUME];

    double amp_decay_coeff = exp(-1.0 / (decay_s * state.sample_rate));
    double pitch_decay_coeff = exp(-1.0 / (pitch_decay_s * state.sample_rate));
    double click_decay_coeff = exp(-1.0 / (0.004 * state.sample_rate));

    foreach (i; 0 .. num_samples) {
        if (state.env_amp < 0.0001) {
            state.active = false;
            state.reset();
            break;
        }

        double current_freq = base_freq + state.env_pitch * pitch_depth;
        if (current_freq < 20.0) current_freq = 20.0;

        double dt = current_freq / state.sample_rate;
        state.osc_phase += dt;
        if (state.osc_phase >= 1.0) state.osc_phase -= 1.0;

        double sine_out = sin(2.0 * pi * state.osc_phase);
        double noise = state.nextRandomFloat();
        double click = noise * state.env_click * click_level;

        double raw = sine_out * state.env_amp * state.velocity + click;

        state.lpf.setParams(BiquadFilter.Type.LOWPASS, cast(float)(current_freq * 2.2), 0.707f, 0.0f, cast(float)state.sample_rate);
        float filtered = state.lpf.process(cast(float)raw);

        float out_val = cast(float)(filtered * volume);
        out_l[i] = out_val;
        out_r[i] = out_val;

        state.env_amp *= amp_decay_coeff;
        state.env_pitch *= pitch_decay_coeff;
        state.env_click *= click_decay_coeff;
    }
}

}
