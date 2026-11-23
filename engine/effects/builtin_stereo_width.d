module engine.effects.builtin_stereo_width;

import core.stdc.math : exp, floor;
import engine.core.math : clampVal, pi;

extern (C++, "hibiki") {

/// Core DSP state for the BuiltinStereoWidth effect.
struct StereoWidthDspState {
    double lp_state_l = 0.0;
    double lp_state_r = 0.0;
    float[16384] delay_buf_l = 0.0f;
    float[16384] delay_buf_r = 0.0f;
    int write_idx = 0;

    void reset() pure nothrow @nogc @safe {
        lp_state_l = 0.0;
        lp_state_r = 0.0;
        delay_buf_l[] = 0.0f;
        delay_buf_r[] = 0.0f;
        write_idx = 0;
    }
}

pure nothrow @nogc @safe float d_stereo_width_norm_to_delay_ms(double norm) {
    return cast(float)(norm * 40.0);
}

pure nothrow @nogc @safe float d_stereo_width_norm_to_mono_freq(double norm) {
    return cast(float)(50.0 + norm * 450.0);
}

pure nothrow @nogc @safe float d_stereo_width_norm_to_width(double norm) {
    return cast(float)(norm * 2.0);
}

void d_stereo_width_process(
    ref StereoWidthDspState state,
    const(float)* in_l,
    const(float)* in_r,
    float* out_l,
    float* out_r,
    int num_samples,
    double sample_rate,
    double delay_norm,
    double channel_norm,
    double mono_freq_norm,
    double width_norm,
    bool enabled
) nothrow @nogc @trusted {
    if (!enabled) {
        foreach (i; 0 .. num_samples) {
            out_l[i] = in_l[i];
            out_r[i] = in_r[i];
        }
        return;
    }

    float delay_ms = d_stereo_width_norm_to_delay_ms(delay_norm);
    bool delay_left = channel_norm < 0.5;
    float crossover_fc = d_stereo_width_norm_to_mono_freq(mono_freq_norm);
    float width = d_stereo_width_norm_to_width(width_norm);

    double theta = 2.0 * pi * crossover_fc / sample_rate;
    double alpha = 1.0 - exp(-theta);
    alpha = clampVal(alpha, 0.0, 1.0);

    enum int buf_size = 16384;
    double delay_samples = delay_ms * 0.001 * sample_rate;

    foreach (i; 0 .. num_samples) {
        float in_sample_l = in_l[i];
        float in_sample_r = in_r[i];

        // Crossover filter: Low-pass band
        state.lp_state_l += alpha * (in_sample_l - state.lp_state_l);
        state.lp_state_r += alpha * (in_sample_r - state.lp_state_r);

        float low_l = cast(float)state.lp_state_l;
        float low_r = cast(float)state.lp_state_r;

        // Keep low frequencies centered in mono
        float low_mono = 0.5f * (low_l + low_r);

        // High-pass band (subtraction)
        float high_l = in_sample_l - low_l;
        float high_r = in_sample_r - low_r;

        // Write high band into circular delay buffers
        state.delay_buf_l[state.write_idx] = high_l;
        state.delay_buf_r[state.write_idx] = high_r;

        float high_l_proc = high_l;
        float high_r_proc = high_r;

        // Apply Haas delay with linear interpolation to high-pass band
        if (delay_left) {
            double read_pos_l = cast(double)state.write_idx - delay_samples;
            if (read_pos_l < 0.0) read_pos_l += buf_size;
            int idx_l1 = (cast(int)floor(read_pos_l)) % buf_size;
            int idx_l2 = (idx_l1 + 1) % buf_size;
            float frac_l = cast(float)(read_pos_l - floor(read_pos_l));
            high_l_proc = (1.0f - frac_l) * state.delay_buf_l[idx_l1] +
                          frac_l * state.delay_buf_l[idx_l2];
        } else {
            double read_pos_r = cast(double)state.write_idx - delay_samples;
            if (read_pos_r < 0.0) read_pos_r += buf_size;
            int idx_r1 = (cast(int)floor(read_pos_r)) % buf_size;
            int idx_r2 = (idx_r1 + 1) % buf_size;
            float frac_r = cast(float)(read_pos_r - floor(read_pos_r));
            high_r_proc = (1.0f - frac_r) * state.delay_buf_r[idx_r1] +
                          frac_r * state.delay_buf_r[idx_r2];
        }

        // Advance write pointer
        state.write_idx = (state.write_idx + 1) % buf_size;

        // Mid/Side width processing on high-pass band
        float mid = 0.5f * (high_l_proc + high_r_proc);
        float side = 0.5f * (high_l_proc - high_r_proc) * width;

        float high_l_out = mid + side;
        float high_r_out = mid - side;

        // Sum back low band (mono) and high band (widened)
        out_l[i] = low_mono + high_l_out;
        out_r[i] = low_mono + high_r_out;
    }
}

} // extern (C++, "hibiki")
