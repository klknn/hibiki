module engine.instruments.builtin_dr8_rim;

import core.stdc.math : exp, sin;
import engine.core.biquad_filter : BiquadFilter;
import engine.core.math : clampVal, pi;

extern (C++, "hibiki") {

enum kTotalDr8RimParams = 3;

enum Dr8RimParamId {
    PARAM_PITCH = 0,
    PARAM_DECAY = 1,
    PARAM_VOLUME = 2,
}

__gshared immutable double[kTotalDr8RimParams] kDefaultDr8RimParams = [
    0.4, // PITCH (~320 Hz)
    0.3, // DECAY (~37ms)
    0.7, // VOLUME
];

struct Dr8RimMidiEvent {
    int pitch;
    float velocity;
    bool is_note_on;
}

struct Dr8RimDspState {
    double[kTotalDr8RimParams] params = 0.0;
    double sample_rate = 44100.0;

    bool active = false;
    double[3] osc_phase = 0.0;
    double[3] env_amp = 0.0;
    float velocity = 0.0f;

    BiquadFilter[3] bpf;

    void reset() nothrow @nogc @safe {
        active = false;
        foreach (i; 0 .. 3) {
            osc_phase[i] = 0.0;
            env_amp[i] = 0.0;
            bpf[i].reset();
        }
        velocity = 0.0f;
        foreach (i; 0 .. kTotalDr8RimParams) {
            params[i] = kDefaultDr8RimParams[i];
        }
    }
}

pure nothrow @nogc @safe float d_dr8_rim_norm_to_pitch_hz(double norm) {
    return cast(float)(200.0 + norm * 300.0);
}

pure nothrow @nogc @safe float d_dr8_rim_norm_to_decay_s(double norm) {
    return cast(float)(0.01 + norm * 0.09);
}

Dr8RimDspState* d_dr8_rim_create() nothrow @nogc @trusted {
    import core.stdc.stdlib : malloc;
    auto s = cast(Dr8RimDspState*)malloc(Dr8RimDspState.sizeof);
    if (s !is null) {
        *s = Dr8RimDspState.init;
        s.reset();
    }
    return s;
}

void d_dr8_rim_destroy(Dr8RimDspState* state) nothrow @nogc @trusted {
    import core.stdc.stdlib : free;
    if (state !is null) free(state);
}

void d_dr8_rim_reset(Dr8RimDspState* state) nothrow @nogc @safe {
    if (state !is null) state.reset();
}

void d_dr8_rim_set_parameter(Dr8RimDspState* state, uint id, double value) nothrow @nogc @safe {
    if (state !is null && id < kTotalDr8RimParams) {
        state.params[id] = clampVal(value, 0.0, 1.0);
    }
}

double d_dr8_rim_get_parameter(const Dr8RimDspState* state, uint id) nothrow @nogc @safe {
    if (state !is null && id < kTotalDr8RimParams) {
        return state.params[id];
    }
    return 0.0;
}

void d_dr8_rim_process(
    Dr8RimDspState* state,
    float* out_l,
    float* out_r,
    int num_samples,
    double sample_rate,
    const(Dr8RimMidiEvent)* events,
    int num_events
) nothrow @nogc @trusted {
    if (state is null) return;
    state.sample_rate = sample_rate;

    if (events !is null) {
        foreach (i; 0 .. num_events) {
            if (events[i].is_note_on && events[i].velocity > 0.0f) {
                state.active = true;
                foreach (j; 0 .. 3) {
                    state.osc_phase[j] = 0.0;
                    state.env_amp[j] = 1.0;
                }
                state.velocity = events[i].velocity;
            }
        }
    }

    foreach (i; 0 .. num_samples) {
        out_l[i] = 0.0f;
        out_r[i] = 0.0f;
    }

    if (!state.active) return;

    double base_freq = d_dr8_rim_norm_to_pitch_hz(state.params[Dr8RimParamId.PARAM_PITCH]);
    double decay_s = d_dr8_rim_norm_to_decay_s(state.params[Dr8RimParamId.PARAM_DECAY]);
    double volume = state.params[Dr8RimParamId.PARAM_VOLUME];

    double[3] f;
    f[0] = base_freq;
    f[1] = base_freq * 2.6;
    f[2] = base_freq * 4.6;

    double nyquist = state.sample_rate * 0.49;
    foreach (j; 0 .. 3) {
        if (f[j] > nyquist) f[j] = nyquist;
    }

    double[3] dt;
    foreach (j; 0 .. 3) {
        dt[j] = f[j] / state.sample_rate;
    }

    double[3] decay_s_val;
    decay_s_val[0] = decay_s;
    decay_s_val[1] = decay_s * 0.5;
    decay_s_val[2] = decay_s * 0.25;

    double[3] decay_coeff;
    foreach (j; 0 .. 3) {
        decay_coeff[j] = exp(-1.0 / (decay_s_val[j] * state.sample_rate));
    }

    foreach (j; 0 .. 3) {
        state.bpf[j].setParams(BiquadFilter.Type.BANDPASS, cast(float)f[j], 4.0f, 0.0f, cast(float)state.sample_rate);
    }

    foreach (i; 0 .. num_samples) {
        if (state.env_amp[0] < 0.0001 && state.env_amp[1] < 0.0001 && state.env_amp[2] < 0.0001) {
            state.active = false;
            state.reset();
            break;
        }

        double signal_sum = 0.0;

        // Component 1 (fundamental)
        state.osc_phase[0] += dt[0];
        if (state.osc_phase[0] >= 1.0) state.osc_phase[0] -= 1.0;
        double osc0 = sin(2.0 * pi * state.osc_phase[0]);
        float filt0 = state.bpf[0].process(cast(float)osc0);
        signal_sum += 0.4 * filt0 * state.env_amp[0];

        // Component 2 (2.6x harmonic)
        state.osc_phase[1] += dt[1];
        if (state.osc_phase[1] >= 1.0) state.osc_phase[1] -= 1.0;
        double osc1 = sin(2.0 * pi * state.osc_phase[1]);
        float filt1 = state.bpf[1].process(cast(float)osc1);
        signal_sum += 0.4 * filt1 * state.env_amp[1];

        // Component 3 (4.6x harmonic)
        state.osc_phase[2] += dt[2];
        if (state.osc_phase[2] >= 1.0) state.osc_phase[2] -= 1.0;
        double osc2 = sin(2.0 * pi * state.osc_phase[2]);
        float filt2 = state.bpf[2].process(cast(float)osc2);
        signal_sum += 0.2 * filt2 * state.env_amp[2];

        float out_val = cast(float)(signal_sum * state.velocity * volume);
        out_l[i] = out_val;
        out_r[i] = out_val;

        foreach (j; 0 .. 3) {
            state.env_amp[j] *= decay_coeff[j];
        }
    }
}

}
