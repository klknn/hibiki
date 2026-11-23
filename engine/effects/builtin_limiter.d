module engine.effects.builtin_limiter;

import core.stdc.math : exp, log10, pow;

extern (C++, "hibiki") {

enum kMaxLimiterLookahead = 960; // 5ms @ 192kHz

/// Core DSP state for BuiltinLimiter.
struct LimiterDspState {
    float[kMaxLimiterLookahead] lookahead_l = 0.0f;
    float[kMaxLimiterLookahead] lookahead_r = 0.0f;
    int la_write = 0;
    float envelope = 1.0f;
    float gain_reduction_db = 0.0f;

    void reset() pure nothrow @nogc @safe {
        lookahead_l[] = 0.0f;
        lookahead_r[] = 0.0f;
        la_write = 0;
        envelope = 1.0f;
        gain_reduction_db = 0.0f;
    }
}

nothrow @nogc @safe float d_limiter_norm_to_ceiling_db(double norm) {
    return cast(float)(norm * 12.0 - 12.0);
}

nothrow @nogc @trusted float d_limiter_norm_to_release_ms(double norm) {
    return 10.0f * pow(100.0f, cast(float)norm);
}

nothrow @nogc @trusted float d_limiter_norm_to_lookahead_ms(double norm) {
    return 0.1f * pow(50.0f, cast(float)norm);
}

nothrow @nogc @safe float d_limiter_norm_to_gain_db(double norm) {
    return cast(float)(norm * 24.0);
}

void d_limiter_process(
    ref LimiterDspState state,
    const(float)* in_l,
    const(float)* in_r,
    float* out_l,
    float* out_r,
    int num_samples,
    double sample_rate,
    double ceiling_norm,
    double release_norm,
    double lookahead_norm,
    double gain_norm,
    bool link_stereo,
    bool enabled
) nothrow @nogc @trusted {
    if (!enabled) {
        foreach (i; 0 .. num_samples) {
            out_l[i] = in_l[i];
            out_r[i] = in_r[i];
        }
        return;
    }

    float ceiling_db = d_limiter_norm_to_ceiling_db(ceiling_norm);
    float ceiling_lin = pow(10.0f, ceiling_db / 20.0f);
    float release_ms = d_limiter_norm_to_release_ms(release_norm);
    float la_ms = d_limiter_norm_to_lookahead_ms(lookahead_norm);
    float gain_db = d_limiter_norm_to_gain_db(gain_norm);
    float gain_lin = pow(10.0f, gain_db / 20.0f);

    int la_samples = cast(int)(la_ms * 0.001f * cast(float)sample_rate);
    if (la_samples < 1) la_samples = 1;
    if (la_samples > kMaxLimiterLookahead - 1) la_samples = kMaxLimiterLookahead - 1;

    float release_coeff = exp(-1.0f / (release_ms * 0.001f * cast(float)sample_rate));
    enum buf_size = kMaxLimiterLookahead;
    float max_gr_db = 0.0f;

    foreach (i; 0 .. num_samples) {
        float sample_in_l = in_l[i] * gain_lin;
        float sample_in_r = in_r[i] * gain_lin;

        state.lookahead_l[state.la_write] = sample_in_l;
        state.lookahead_r[state.la_write] = sample_in_r;

        float abs_l = sample_in_l >= 0 ? sample_in_l : -sample_in_l;
        float abs_r = sample_in_r >= 0 ? sample_in_r : -sample_in_r;
        float peak = (abs_l > abs_r) ? abs_l : abs_r;

        float target = (peak > ceiling_lin) ? (ceiling_lin / peak) : 1.0f;

        if (target < state.envelope) {
            state.envelope = target;
        } else {
            state.envelope = state.envelope * release_coeff + target * (1.0f - release_coeff);
        }

        int la_read = (state.la_write - la_samples + buf_size) % buf_size;
        float del_l = state.lookahead_l[la_read];
        float del_r = state.lookahead_r[la_read];

        out_l[i] = del_l * state.envelope;
        out_r[i] = del_r * state.envelope;

        state.la_write = (state.la_write + 1) % buf_size;

        float env_val = state.envelope > 1e-10f ? state.envelope : 1e-10f;
        float gr_db = 20.0f * log10(env_val);
        if (gr_db < max_gr_db) max_gr_db = gr_db;
    }

    state.gain_reduction_db = max_gr_db;
}

}
