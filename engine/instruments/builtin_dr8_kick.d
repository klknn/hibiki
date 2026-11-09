module engine.instruments.builtin_dr8_kick;

import core.stdc.math : exp, pow, sin, tanh;
import engine.core.math : pi;

extern (C++, "hibiki") {

struct Dr8KickMidiEvent {
    float velocity;
    bool is_note_on;
}

struct Dr8KickDspState {
    bool active = false;
    double osc_phase = 0.0;
    double env_amp = 0.0;
    double env_pitch = 0.0;
    double env_click = 0.0;
    float velocity = 0.0f;
    uint rng_state = 123456789;

    void reset() pure nothrow @nogc @safe {
        active = false;
        osc_phase = 0.0;
        env_amp = 0.0;
        env_pitch = 0.0;
        env_click = 0.0;
        velocity = 0.0f;
    }
}

pure nothrow @nogc @safe float d_dr8_kick_norm_to_pitch_hz(double norm) {
    return cast(float)(40.0 + norm * 40.0);
}

nothrow @nogc @trusted float d_dr8_kick_norm_to_decay_s(double norm) {
    return cast(float)(0.05 * pow(20.0, norm));
}

pure nothrow @nogc @safe float d_dr8_kick_norm_to_pitch_env_decay_s(double norm) {
    return cast(float)(0.01 + norm * 0.14);
}

pure nothrow @nogc @safe float d_dr8_kick_norm_to_pitch_env_depth_hz(double norm) {
    return cast(float)(norm * 300.0);
}

void d_dr8_kick_process(
    ref Dr8KickDspState state,
    float* out_l,
    float* out_r,
    int num_samples,
    double sample_rate,
    const(Dr8KickMidiEvent)* events,
    int num_events,
    double pitch_norm,
    double decay_norm,
    double pitch_env_decay_norm,
    double pitch_env_depth_norm,
    double click_level_norm,
    double distortion_norm,
    double volume_norm
) nothrow @nogc @trusted {
    foreach (e; 0 .. num_events) {
        const ev = events[e];
        if (ev.is_note_on && ev.velocity > 0) {
            state.active = true;
            state.env_amp = 1.0;
            state.env_pitch = 1.0;
            state.env_click = 1.0;
            state.osc_phase = 0.0;
            state.velocity = ev.velocity;
        }
    }

    foreach (i; 0 .. num_samples) {
        out_l[i] = 0.0f;
        out_r[i] = 0.0f;
    }

    if (!state.active) return;

    double base_freq = d_dr8_kick_norm_to_pitch_hz(pitch_norm);
    double decay_s = d_dr8_kick_norm_to_decay_s(decay_norm);
    double pitch_decay_s = d_dr8_kick_norm_to_pitch_env_decay_s(pitch_env_decay_norm);
    double pitch_depth = d_dr8_kick_norm_to_pitch_env_depth_hz(pitch_env_depth_norm);
    double click_level = click_level_norm;
    double distortion = distortion_norm;
    double volume = volume_norm;

    double amp_decay_coeff = exp(-1.0 / (decay_s * sample_rate));
    double pitch_decay_coeff = exp(-1.0 / (pitch_decay_s * sample_rate));
    double click_decay_coeff = exp(-1.0 / (0.005 * sample_rate));

    foreach (i; 0 .. num_samples) {
        if (state.env_amp < 0.0001) {
            state.active = false;
            state.reset();
            break;
        }

        double current_freq = base_freq + state.env_pitch * pitch_depth;
        if (current_freq < 10.0) current_freq = 10.0;

        double dt = current_freq / sample_rate;
        state.osc_phase += dt;
        if (state.osc_phase >= 1.0) state.osc_phase -= 1.0;

        double sine_out = sin(2.0 * pi * state.osc_phase);

        // Simple LCG PRNG for noise click
        state.rng_state = 1664525u * state.rng_state + 1013904223u;
        double noise = (cast(double)(state.rng_state & 0x7FFFFFFF) / 1073741824.0) - 1.0;
        double click = noise * state.env_click * click_level;

        double raw = sine_out * state.env_amp * state.velocity + click;
        double drive = distortion * 5.0;
        double saturated = tanh(raw * (1.0 + drive));

        float out_val = cast(float)(saturated * volume);
        out_l[i] = out_val;
        out_r[i] = out_val;

        state.env_amp *= amp_decay_coeff;
        state.env_pitch *= pitch_decay_coeff;
        state.env_click *= click_decay_coeff;
    }
}

}
