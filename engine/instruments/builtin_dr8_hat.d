module engine.instruments.builtin_dr8_hat;

import core.stdc.math : exp, pow;
import engine.core.biquad_filter : BiquadFilter;
import engine.core.math : clampVal;

extern (C++, "hibiki") {

enum kTotalDr8HatParams = 5;

enum Dr8HatParamId {
    PARAM_DECAY = 0,
    PARAM_HPF_FREQ = 1,
    PARAM_BPF_FREQ = 2,
    PARAM_TENSION = 3,
    PARAM_VOLUME = 4,
}

__gshared immutable double[kTotalDr8HatParams] kDefaultDr8HatParams = [
    0.3, // DECAY
    0.5, // HPF_FREQ
    0.5, // BPF_FREQ
    0.4, // TENSION
    0.7, // VOLUME
];

__gshared immutable double[6] kHatOscRatios = [
    1.0, 1.315, 1.55, 1.715, 2.0, 2.25
];

struct Dr8HatMidiEvent {
    int pitch;
    float velocity;
    bool is_note_on;
}

struct Dr8HatDspState {
    double[kTotalDr8HatParams] params = 0.0;
    double sample_rate = 44100.0;

    bool active = false;
    double[6] osc_phase = 0.0;
    double env_amp = 0.0;
    float velocity = 0.0f;

    BiquadFilter bpf;
    BiquadFilter hpf;

    void reset() nothrow @nogc @safe {
        active = false;
        foreach (ref p; osc_phase) p = 0.0;
        env_amp = 0.0;
        velocity = 0.0f;
        bpf.reset();
        hpf.reset();
        foreach (i; 0 .. kTotalDr8HatParams) {
            params[i] = kDefaultDr8HatParams[i];
        }
    }
}

nothrow @nogc @trusted float d_dr8_hat_norm_to_decay_s(double norm) {
    return cast(float)(0.02 * pow(40.0, norm));
}

nothrow @nogc @safe float d_dr8_hat_norm_to_hpf_hz(double norm) {
    return cast(float)(3000.0 + norm * 9000.0);
}

nothrow @nogc @safe float d_dr8_hat_norm_to_bpf_hz(double norm) {
    return cast(float)(6000.0 + norm * 9000.0);
}

Dr8HatDspState* d_dr8_hat_create() nothrow @nogc @trusted {
    import core.stdc.stdlib : malloc;
    auto s = cast(Dr8HatDspState*)malloc(Dr8HatDspState.sizeof);
    if (s !is null) {
        *s = Dr8HatDspState.init;
        s.reset();
    }
    return s;
}

void d_dr8_hat_destroy(Dr8HatDspState* state) nothrow @nogc @trusted {
    import core.stdc.stdlib : free;
    if (state !is null) free(state);
}

void d_dr8_hat_reset(Dr8HatDspState* state) nothrow @nogc @safe {
    if (state !is null) state.reset();
}

void d_dr8_hat_set_parameter(Dr8HatDspState* state, uint id, double value) nothrow @nogc @safe {
    if (state !is null && id < kTotalDr8HatParams) {
        state.params[id] = clampVal(value, 0.0, 1.0);
    }
}

double d_dr8_hat_get_parameter(const(Dr8HatDspState)* state, uint id) nothrow @nogc @safe {
    if (state !is null && id < kTotalDr8HatParams) {
        return state.params[id];
    }
    return 0.0;
}

void d_dr8_hat_process(
    Dr8HatDspState* state,
    float* out_l,
    float* out_r,
    int num_samples,
    double sample_rate,
    const(Dr8HatMidiEvent)* events,
    int num_events
) nothrow @nogc @trusted {
    if (state is null) return;
    state.sample_rate = sample_rate;

    if (events !is null) {
        foreach (i; 0 .. num_events) {
            if (events[i].is_note_on && events[i].velocity > 0) {
                state.active = true;
                foreach (ref p; state.osc_phase) p = 0.0;
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

    double decay_s = d_dr8_hat_norm_to_decay_s(state.params[Dr8HatParamId.PARAM_DECAY]);
    double hpf_fc = d_dr8_hat_norm_to_hpf_hz(state.params[Dr8HatParamId.PARAM_HPF_FREQ]);
    double bpf_fc = d_dr8_hat_norm_to_bpf_hz(state.params[Dr8HatParamId.PARAM_BPF_FREQ]);
    double tension = state.params[Dr8HatParamId.PARAM_TENSION];
    double volume = state.params[Dr8HatParamId.PARAM_VOLUME];

    double base_freq = 150.0 + tension * 300.0;
    double amp_decay_coeff = exp(-1.0 / (decay_s * state.sample_rate));

    float sr_f = cast(float)state.sample_rate;
    state.bpf.setParams(BiquadFilter.Type.BANDPASS, cast(float)bpf_fc, 1.5f, 0.0f, sr_f);
    state.hpf.setParams(BiquadFilter.Type.HIGHPASS, cast(float)hpf_fc, 0.707f, 0.0f, sr_f);

    double[6] dt;
    foreach (d; 0 .. 6) {
        dt[d] = (base_freq * kHatOscRatios[d]) / state.sample_rate;
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

        float bpf_out = state.bpf.process(cast(float)metallic);
        float hpf_out = state.hpf.process(bpf_out);

        float out_val = cast(float)(hpf_out * state.env_amp * state.velocity * volume);

        out_l[i] = out_val;
        out_r[i] = out_val;

        state.env_amp *= amp_decay_coeff;
    }
}

}
