module engine.instruments.builtin_dr8_crash;

import core.stdc.math : exp, pow;
import engine.core.biquad_filter : BiquadFilter;
import engine.core.math : clampVal;

extern (C++, "hibiki") {

enum kTotalDr8CrashParams = 4;

enum Dr8CrashParamId {
    PARAM_DECAY = 0,
    PARAM_TONE = 1,
    PARAM_TENSION = 2,
    PARAM_VOLUME = 3,
}

__gshared immutable double[kTotalDr8CrashParams] kDefaultDr8CrashParams = [
    0.4, // DECAY
    0.4, // TONE
    0.3, // TENSION
    0.7, // VOLUME
];

__gshared immutable double[6] kCymbalOscRatios = [1.0, 1.8, 1.48, 2.54, 3.89, 4.15];

struct Dr8CrashMidiEvent {
    int pitch;
    float velocity;
    bool is_note_on;
}

struct Dr8CrashDspState {
    double[kTotalDr8CrashParams] params = 0.0;
    double sample_rate = 44100.0;

    bool active = false;
    double[6] osc_phase = 0.0;
    double env_amp = 0.0;
    float velocity = 0.0f;
    uint rng_state = 0xa5a5a5a5;

    BiquadFilter sizzle_hpf;
    BiquadFilter bpf1;
    BiquadFilter bpf2;

    void reset() nothrow @nogc @safe {
        active = false;
        foreach (i; 0 .. 6) osc_phase[i] = 0.0;
        env_amp = 0.0;
        velocity = 0.0f;
        rng_state = 0xa5a5a5a5;
        sizzle_hpf.reset();
        bpf1.reset();
        bpf2.reset();
        foreach (i; 0 .. kTotalDr8CrashParams) {
            params[i] = kDefaultDr8CrashParams[i];
        }
    }

    float nextRandomFloat() nothrow @nogc @safe {
        rng_state ^= (rng_state << 13);
        rng_state ^= (rng_state >> 17);
        rng_state ^= (rng_state << 5);
        return (cast(float)(rng_state & 0x7FFFFFFF) / 1073741824.0f) - 1.0f;
    }
}

nothrow @nogc @trusted float d_dr8_crash_norm_to_decay_s(double norm) {
    return cast(float)(0.2 * pow(15.0, norm));
}

Dr8CrashDspState* d_dr8_crash_create() nothrow @nogc @trusted {
    import core.stdc.stdlib : malloc;
    auto s = cast(Dr8CrashDspState*)malloc(Dr8CrashDspState.sizeof);
    if (s !is null) {
        *s = Dr8CrashDspState.init;
        s.reset();
    }
    return s;
}

void d_dr8_crash_destroy(Dr8CrashDspState* state) nothrow @nogc @trusted {
    import core.stdc.stdlib : free;
    if (state !is null) free(state);
}

void d_dr8_crash_reset(Dr8CrashDspState* state) nothrow @nogc @safe {
    if (state !is null) state.reset();
}

void d_dr8_crash_set_parameter(Dr8CrashDspState* state, uint id, double value) nothrow @nogc @safe {
    if (state !is null && id < kTotalDr8CrashParams) {
        state.params[id] = clampVal(value, 0.0, 1.0);
    }
}

double d_dr8_crash_get_parameter(const Dr8CrashDspState* state, uint id) nothrow @nogc @safe {
    if (state !is null && id < kTotalDr8CrashParams) {
        return state.params[id];
    }
    return 0.0;
}

void d_dr8_crash_process(
    Dr8CrashDspState* state,
    float* out_l,
    float* out_r,
    int num_samples,
    double sample_rate,
    const(Dr8CrashMidiEvent)* events,
    int num_events
) nothrow @nogc @trusted {
    if (state is null) return;
    state.sample_rate = sample_rate;

    if (events !is null) {
        foreach (i; 0 .. num_events) {
            if (events[i].is_note_on && events[i].velocity > 0.0f) {
                state.active = true;
                foreach (j; 0 .. 6) state.osc_phase[j] = 0.0;
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

    double decay_s = d_dr8_crash_norm_to_decay_s(state.params[Dr8CrashParamId.PARAM_DECAY]);
    double tone = state.params[Dr8CrashParamId.PARAM_TONE];
    double tension = state.params[Dr8CrashParamId.PARAM_TENSION];
    double volume = state.params[Dr8CrashParamId.PARAM_VOLUME];

    double base_freq = 150.0 + tension * 250.0;
    double decay_coeff = exp(-1.0 / (decay_s * state.sample_rate));

    state.sizzle_hpf.setParams(BiquadFilter.Type.HIGHPASS, 7000.0f, 0.707f, 0.0f, cast(float)state.sample_rate);
    state.bpf1.setParams(BiquadFilter.Type.BANDPASS, 800.0f, 1.0f, 0.0f, cast(float)state.sample_rate);
    state.bpf2.setParams(BiquadFilter.Type.BANDPASS, 1200.0f, 1.2f, 0.0f, cast(float)state.sample_rate);

    double[6] dt;
    foreach (d; 0 .. 6) {
        dt[d] = (base_freq * kCymbalOscRatios[d]) / state.sample_rate;
    }

    foreach (i; 0 .. num_samples) {
        if (state.env_amp < 0.0001) {
            state.active = false;
            state.reset();
            break;
        }

        double osc_sum = 0.0;
        foreach (d; 0 .. 6) {
            state.osc_phase[d] += dt[d];
            if (state.osc_phase[d] >= 1.0) state.osc_phase[d] -= 1.0;
            osc_sum += (state.osc_phase[d] < 0.5 ? 1.0 : -1.0);
        }
        double metallic = osc_sum / 6.0;

        double noise = state.nextRandomFloat();
        float sizzle = state.sizzle_hpf.process(cast(float)noise);

        double raw = (1.0 - tone) * metallic + tone * sizzle;

        float f1 = state.bpf1.process(cast(float)raw);
        float f2 = state.bpf2.process(cast(float)raw);
        float filtered = 0.5f * (f1 + f2);

        float out_val = cast(float)(filtered * state.env_amp * state.velocity * volume);
        out_l[i] = out_val;
        out_r[i] = out_val;

        state.env_amp *= decay_coeff;
    }
}

}
