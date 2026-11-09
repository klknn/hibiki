module engine.effects.builtin_chorus;

import core.stdc.math : cos, floor, sin;
import engine.core.math : pi;

extern (C++, "hibiki") {

/// Core DSP state for the BuiltinChorus effect.
struct ChorusDspState {
    double lfo_phase = 0.0;
    float[16384] delay_buf_l = 0.0f;
    float[16384] delay_buf_r = 0.0f;
    int write_idx = 0;

    void reset() pure nothrow @nogc @safe {
        lfo_phase = 0.0;
        delay_buf_l[] = 0.0f;
        delay_buf_r[] = 0.0f;
        write_idx = 0;
    }
}

pure nothrow @nogc @safe float d_chorus_norm_to_rate_hz(double norm) {
    return cast(float)(0.1 + norm * 9.9);
}

pure nothrow @nogc @safe float d_chorus_norm_to_depth_ms(double norm) {
    return cast(float)(norm * 5.0);
}

pure nothrow @nogc @safe float d_chorus_norm_to_delay_ms(double norm) {
    return cast(float)(5.0 + norm * 25.0);
}

pure nothrow @nogc @safe float d_chorus_norm_to_feedback(double norm) {
    return cast(float)(norm * 0.95);
}

void d_chorus_process(
    ref ChorusDspState state,
    const(float)* in_l,
    const(float)* in_r,
    float* out_l,
    float* out_r,
    int num_samples,
    double sample_rate,
    double rate_norm,
    double depth_norm,
    double delay_norm,
    double feedback_norm,
    double wet_dry_norm,
    bool enabled
) nothrow @nogc @trusted {
    if (!enabled) {
        foreach (i; 0 .. num_samples) {
            out_l[i] = in_l[i];
            out_r[i] = in_r[i];
        }
        return;
    }

    float rate_hz = d_chorus_norm_to_rate_hz(rate_norm);
    float depth_ms = d_chorus_norm_to_depth_ms(depth_norm);
    float delay_ms = d_chorus_norm_to_delay_ms(delay_norm);
    float feedback = d_chorus_norm_to_feedback(feedback_norm);
    float wet = cast(float)wet_dry_norm;
    float dry = 1.0f - wet;

    float sample_rate_f = cast(float)sample_rate;
    float base_delay_samples = delay_ms * 0.001f * sample_rate_f;
    float depth_samples = depth_ms * 0.001f * sample_rate_f;

    double phase_inc = 2.0 * pi * rate_hz / sample_rate;
    enum int buf_size = 16384;

    foreach (i; 0 .. num_samples) {
        float in_sample_l = in_l[i];
        float in_sample_r = in_r[i];

        // Increment and wrap LFO phase
        state.lfo_phase += phase_inc;
        if (state.lfo_phase >= 2.0 * pi) {
            state.lfo_phase -= 2.0 * pi;
        }

        // LFO outputs with 90-degree phase offset for stereo width
        float lfo_l = cast(float)sin(state.lfo_phase);
        float lfo_r = cast(float)cos(state.lfo_phase);

        // Compute modulated delay times
        float delay_samples_l = base_delay_samples + depth_samples * lfo_l;
        float delay_samples_r = base_delay_samples + depth_samples * lfo_r;

        // Linear interpolation for Left channel read pointer
        double read_pos_l = cast(double)state.write_idx - delay_samples_l;
        if (read_pos_l < 0.0) read_pos_l += buf_size;
        int idx_l1 = (cast(int)floor(read_pos_l)) % buf_size;
        int idx_l2 = (idx_l1 + 1) % buf_size;
        float frac_l = cast(float)(read_pos_l - floor(read_pos_l));
        float delayed_l = (1.0f - frac_l) * state.delay_buf_l[idx_l1] +
                          frac_l * state.delay_buf_l[idx_l2];

        // Linear interpolation for Right channel read pointer
        double read_pos_r = cast(double)state.write_idx - delay_samples_r;
        if (read_pos_r < 0.0) read_pos_r += buf_size;
        int idx_r1 = (cast(int)floor(read_pos_r)) % buf_size;
        int idx_r2 = (idx_r1 + 1) % buf_size;
        float frac_r = cast(float)(read_pos_r - floor(read_pos_r));
        float delayed_r = (1.0f - frac_r) * state.delay_buf_r[idx_r1] +
                          frac_r * state.delay_buf_r[idx_r2];

        // Write to circular delay lines
        state.delay_buf_l[state.write_idx] = in_sample_l + delayed_l * feedback;
        state.delay_buf_r[state.write_idx] = in_sample_r + delayed_r * feedback;
        state.write_idx = (state.write_idx + 1) % buf_size;

        // Dry/Wet Mix
        out_l[i] = delayed_l * wet + in_sample_l * dry;
        out_r[i] = delayed_r * wet + in_sample_r * dry;
    }
}

} // extern (C++, "hibiki")
