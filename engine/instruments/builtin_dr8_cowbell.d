module engine.instruments.builtin_dr8_cowbell;

import core.stdc.math : exp, pow;
import engine.core.biquad_filter : BiquadFilter;
import engine.core.math : clampVal;

extern (C++, "hibiki") {

enum kTotalDr8CowbellParams = 4;

enum Dr8CowbellParamId {
    PARAM_PITCH = 0,
    PARAM_DECAY = 1,
    PARAM_DETUNE = 2,
    PARAM_VOLUME = 3,
}

__gshared immutable double[kTotalDr8CowbellParams] kDefaultDr8CowbellParams = [
    0.466667, // PITCH (~540 Hz)
    0.3,      // DECAY (~150ms)
    0.4,      // DETUNE (1.48)
    0.7,      // VOLUME
];

struct Dr8CowbellMidiEvent {
    int pitch;
    float velocity;
    bool is_note_on;
}

struct Dr8CowbellDspState {
    double[kTotalDr8CowbellParams] params = 0.0;
    double sample_rate = 44100.0;

    bool active = false;
    double phase1 = 0.0;
    double phase2 = 0.0;
    double env_amp = 0.0;
    float velocity = 0.0f;

    BiquadFilter bpf;

    void reset() nothrow @nogc @safe {
        active = false;
        phase1 = 0.0;
        phase2 = 0.0;
        env_amp = 0.0;
        velocity = 0.0f;
        bpf.reset();
        foreach (i; 0 .. kTotalDr8CowbellParams) {
            params[i] = kDefaultDr8CowbellParams[i];
        }
    }
}

pure nothrow @nogc @safe float d_dr8_cowbell_norm_to_pitch_hz(double norm) {
    return cast(float)(400.0 + norm * 300.0);
}

nothrow @nogc @trusted float d_dr8_cowbell_norm_to_decay_s(double norm) {
    return cast(float)(0.05 * pow(10.0, norm));
}

pure nothrow @nogc @safe float d_dr8_cowbell_norm_to_detune_ratio(double norm) {
    return cast(float)(1.40 + norm * 0.20);
}

Dr8CowbellDspState* d_dr8_cowbell_create() nothrow @nogc @trusted {
    import core.stdc.stdlib : malloc;
    auto s = cast(Dr8CowbellDspState*)malloc(Dr8CowbellDspState.sizeof);
    if (s !is null) {
        *s = Dr8CowbellDspState.init;
        s.reset();
    }
    return s;
}

void d_dr8_cowbell_destroy(Dr8CowbellDspState* state) nothrow @nogc @trusted {
    import core.stdc.stdlib : free;
    if (state !is null) free(state);
}

void d_dr8_cowbell_reset(Dr8CowbellDspState* state) nothrow @nogc @safe {
    if (state !is null) state.reset();
}

void d_dr8_cowbell_set_parameter(Dr8CowbellDspState* state, uint id, double value) nothrow @nogc @safe {
    if (state !is null && id < kTotalDr8CowbellParams) {
        state.params[id] = clampVal(value, 0.0, 1.0);
    }
}

double d_dr8_cowbell_get_parameter(const Dr8CowbellDspState* state, uint id) nothrow @nogc @safe {
    if (state !is null && id < kTotalDr8CowbellParams) {
        return state.params[id];
    }
    return 0.0;
}

void d_dr8_cowbell_process(
    Dr8CowbellDspState* state,
    float* out_l,
    float* out_r,
    int num_samples,
    double sample_rate,
    const(Dr8CowbellMidiEvent)* events,
    int num_events
) nothrow @nogc @trusted {
    if (state is null) return;
    state.sample_rate = sample_rate;

    if (events !is null) {
        foreach (i; 0 .. num_events) {
            if (events[i].is_note_on && events[i].velocity > 0.0f) {
                state.active = true;
                state.phase1 = 0.0;
                state.phase2 = 0.0;
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

    double base_freq = d_dr8_cowbell_norm_to_pitch_hz(state.params[Dr8CowbellParamId.PARAM_PITCH]);
    double decay_s = d_dr8_cowbell_norm_to_decay_s(state.params[Dr8CowbellParamId.PARAM_DECAY]);
    double detune_ratio = d_dr8_cowbell_norm_to_detune_ratio(state.params[Dr8CowbellParamId.PARAM_DETUNE]);
    double volume = state.params[Dr8CowbellParamId.PARAM_VOLUME];

    double f1 = base_freq;
    double f2 = base_freq * detune_ratio;

    double dt1 = f1 / state.sample_rate;
    double dt2 = f2 / state.sample_rate;

    double decay_coeff = exp(-1.0 / (decay_s * state.sample_rate));

    state.bpf.setParams(BiquadFilter.Type.BANDPASS, cast(float)base_freq, 2.0f, 0.0f, cast(float)state.sample_rate);

    foreach (i; 0 .. num_samples) {
        if (state.env_amp < 0.0001) {
            state.active = false;
            state.reset();
            break;
        }

        state.phase1 += dt1;
        if (state.phase1 >= 1.0) state.phase1 -= 1.0;
        state.phase2 += dt2;
        if (state.phase2 >= 1.0) state.phase2 -= 1.0;

        double osc1 = (state.phase1 < 0.5 ? 1.0 : -1.0);
        double osc2 = (state.phase2 < 0.5 ? 1.0 : -1.0);

        double sum = 0.5 * (osc1 + osc2);
        double raw = sum * state.env_amp;

        float filtered = state.bpf.process(cast(float)raw);

        float out_val = cast(float)(filtered * state.velocity * volume);
        out_l[i] = out_val;
        out_r[i] = out_val;

        state.env_amp *= decay_coeff;
    }
}

}
