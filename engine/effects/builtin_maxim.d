module engine.effects.builtin_maxim;

import core.stdc.math : exp, fabs, log10, pow, tanh;
import engine.core.biquad_filter : BiquadFilter;
import engine.core.math : clampVal;

extern (C++, "hibiki") {

enum kTotalMaximParams = 44;
enum kNumMaximBands = 3;
enum kMaxLookahead = 1920;

enum MaximParamId {
    PARAM_LOW_XOVER = 0,
    PARAM_HIGH_XOVER = 1,
    PARAM_LOOKAHEAD = 2,
    PARAM_ENABLE = 3,

    // Low band
    PARAM_LOW_PREGAIN = 4,
    PARAM_LOW_POSTGAIN = 5,
    PARAM_LOW_SAT_AMOUNT = 6,
    PARAM_LOW_SAT_THRESHOLD = 7,
    PARAM_LOW_THRESH = 8,
    PARAM_LOW_RATIO = 9,
    PARAM_LOW_KNEE = 10,
    PARAM_LOW_ATTACK = 11,
    PARAM_LOW_RELEASE = 12,
    PARAM_LOW_CEILING = 13,

    // Mid band
    PARAM_MID_PREGAIN = 14,
    PARAM_MID_POSTGAIN = 15,
    PARAM_MID_SAT_AMOUNT = 16,
    PARAM_MID_SAT_THRESHOLD = 17,
    PARAM_MID_THRESH = 18,
    PARAM_MID_RATIO = 19,
    PARAM_MID_KNEE = 20,
    PARAM_MID_ATTACK = 21,
    PARAM_MID_RELEASE = 22,
    PARAM_MID_CEILING = 23,

    // High band
    PARAM_HIGH_PREGAIN = 24,
    PARAM_HIGH_POSTGAIN = 25,
    PARAM_HIGH_SAT_AMOUNT = 26,
    PARAM_HIGH_SAT_THRESHOLD = 27,
    PARAM_HIGH_THRESH = 28,
    PARAM_HIGH_RATIO = 29,
    PARAM_HIGH_KNEE = 30,
    PARAM_HIGH_ATTACK = 31,
    PARAM_HIGH_RELEASE = 32,
    PARAM_HIGH_CEILING = 33,

    // Master band
    PARAM_MASTER_PREGAIN = 34,
    PARAM_MASTER_POSTGAIN = 35,
    PARAM_MASTER_SAT_AMOUNT = 36,
    PARAM_MASTER_SAT_THRESHOLD = 37,
    PARAM_MASTER_THRESH = 38,
    PARAM_MASTER_RATIO = 39,
    PARAM_MASTER_KNEE = 40,
    PARAM_MASTER_ATTACK = 41,
    PARAM_MASTER_RELEASE = 42,
    PARAM_MASTER_CEILING = 43,
}

nothrow @nogc @trusted float d_maxim_norm_to_freq(double norm, float min_hz, float max_hz) {
    return min_hz * pow(max_hz / min_hz, cast(float)norm);
}

pure nothrow @nogc @safe float d_maxim_norm_to_gain_db(double norm) {
    return cast(float)(norm * 48.0 - 24.0);
}

pure nothrow @nogc @safe float d_maxim_norm_to_sat_thresh_db(double norm) {
    return cast(float)(norm * 30.0 - 30.0);
}

pure nothrow @nogc @safe float d_maxim_norm_to_thresh_db(double norm) {
    return cast(float)(norm * 60.0 - 60.0);
}

pure nothrow @nogc @safe float d_maxim_norm_to_ratio(double norm) {
    if (norm >= 0.999) return 1000.0f;
    return 1.0f / (1.0f - cast(float)norm);
}

pure nothrow @nogc @safe float d_maxim_norm_to_knee_db(double norm) {
    return cast(float)(norm * 24.0);
}

nothrow @nogc @trusted float d_maxim_norm_to_attack_ms(double norm) {
    return cast(float)(0.1 * pow(1000.0, norm));
}

nothrow @nogc @trusted float d_maxim_norm_to_release_ms(double norm) {
    return cast(float)(10.0 * pow(100.0, norm));
}

pure nothrow @nogc @safe float d_maxim_norm_to_ceiling_db(double norm) {
    return cast(float)(norm * 12.0 - 12.0);
}

nothrow @nogc @trusted float d_maxim_norm_to_lookahead_ms(double norm) {
    return cast(float)(0.1 * pow(100.0, norm));
}

struct MaximBandProcessor {
    double sample_rate = 44100.0;
    float envelope_db = 0.0f;
    float limiter_envelope = 1.0f;
    float[kMaxLookahead] la_l = 0.0f;
    float[kMaxLookahead] la_r = 0.0f;
    int la_write = 0;
    float max_gr_db = 0.0f;
    float peak_in_db = -200.0f;
    float peak_out_db = -200.0f;

    void reset() nothrow @nogc @safe {
        envelope_db = 0.0f;
        limiter_envelope = 1.0f;
        la_l[] = 0.0f;
        la_r[] = 0.0f;
        la_write = 0;
        max_gr_db = 0.0f;
        peak_in_db = -200.0f;
        peak_out_db = -200.0f;
    }

    static float saturate(float x, float threshold, float amount) nothrow @nogc @trusted {
        if (amount <= 0.001f) return x;
        float abs_x = fabs(x);
        if (abs_x <= threshold) return x;
        float sign = (x > 0.0f) ? 1.0f : -1.0f;
        float excess = abs_x - threshold;
        float denom = 1.0f - threshold;
        if (denom < 0.0001f) denom = 0.0001f;
        float clip = threshold + (1.0f - threshold) * tanh(excess / denom);
        return x * (1.0f - amount) + sign * clip * amount;
    }

    static float computeGainReduction(float input_db, float threshold, float ratio, float knee_db) nothrow @nogc @safe {
        if (input_db <= -100.0f) return 0.0f;
        float half_knee = knee_db / 2.0f;
        float gr_db = 0.0f;

        if (knee_db <= 0.01f) {
            if (input_db > threshold) {
                gr_db = (threshold - input_db) * (1.0f - 1.0f / ratio);
            }
        } else {
            float lower = threshold - half_knee;
            float upper = threshold + half_knee;
            if (input_db <= lower) {
                // no reduction
            } else if (input_db >= upper) {
                gr_db = (threshold - input_db) * (1.0f - 1.0f / ratio);
            } else {
                float x = input_db - lower;
                gr_db = -(1.0f - 1.0f / ratio) * x * x / (2.0f * knee_db);
            }
        }
        return gr_db;
    }

    void process(
        const(float)* in_l,
        const(float)* in_r,
        float* out_l,
        float* out_r,
        int num_samples,
        float pre_gain_db,
        float post_gain_db,
        float sat_amount,
        float sat_threshold_db,
        float thresh_db,
        float ratio,
        float knee_db,
        float attack_ms,
        float release_ms,
        float ceiling_db,
        float lookahead_ms
    ) nothrow @nogc @trusted {
        float pre_gain_lin = pow(10.0f, pre_gain_db / 20.0f);
        float post_gain_lin = pow(10.0f, post_gain_db / 20.0f);
        float sat_thresh_lin = pow(10.0f, sat_threshold_db / 20.0f);
        float ceiling_lin = pow(10.0f, ceiling_db / 20.0f);

        float att_coeff = exp(-1.0f / (attack_ms * 0.001f * cast(float)sample_rate));
        float rel_coeff = exp(-1.0f / (release_ms * 0.001f * cast(float)sample_rate));

        float lim_release_ms = (release_ms * 0.5f > 5.0f) ? release_ms * 0.5f : 5.0f;
        float lim_release_coeff = exp(-1.0f / (lim_release_ms * 0.001f * cast(float)sample_rate));

        int la_samples = clampVal(cast(int)(lookahead_ms * 0.001f * cast(float)sample_rate), 0, kMaxLookahead - 1);
        int buf_size = kMaxLookahead;

        float current_max_gr = 0.0f;
        float current_peak_in = -200.0f;
        float current_peak_out = -200.0f;

        foreach (i; 0 .. num_samples) {
            float x_l = in_l[i] * pre_gain_lin;
            float x_r = in_r[i] * pre_gain_lin;

            float in_abs = fabs(x_l) > fabs(x_r) ? fabs(x_l) : fabs(x_r);
            float in_db = (in_abs > 1e-10f) ? 20.0f * log10(in_abs) : -200.0f;
            if (in_db > current_peak_in) current_peak_in = in_db;

            x_l = saturate(x_l, sat_thresh_lin, sat_amount);
            x_r = saturate(x_r, sat_thresh_lin, sat_amount);

            float comp_in_abs = fabs(x_l) > fabs(x_r) ? fabs(x_l) : fabs(x_r);
            float comp_in_db = (comp_in_abs > 1e-10f) ? 20.0f * log10(comp_in_abs) : -200.0f;

            float gr_db = computeGainReduction(comp_in_db, thresh_db, ratio, knee_db);
            if (gr_db < envelope_db) {
                envelope_db = att_coeff * envelope_db + (1.0f - att_coeff) * gr_db;
            } else {
                envelope_db = rel_coeff * envelope_db + (1.0f - rel_coeff) * gr_db;
            }

            float comp_gain = pow(10.0f, envelope_db / 20.0f);
            x_l *= comp_gain;
            x_r *= comp_gain;

            la_l[la_write] = x_l;
            la_r[la_write] = x_r;

            float peak = fabs(x_l) > fabs(x_r) ? fabs(x_l) : fabs(x_r);
            float target = (peak > ceiling_lin) ? ceiling_lin / peak : 1.0f;

            if (target < limiter_envelope) {
                limiter_envelope = target;
            } else {
                limiter_envelope = limiter_envelope * lim_release_coeff + target * (1.0f - lim_release_coeff);
            }

            int la_read = (la_write - la_samples + buf_size) % buf_size;
            float out_sample_l = la_l[la_read] * limiter_envelope * post_gain_lin;
            float out_sample_r = la_r[la_read] * limiter_envelope * post_gain_lin;

            la_write = (la_write + 1) % buf_size;

            out_l[i] = out_sample_l;
            out_r[i] = out_sample_r;

            float out_abs = fabs(out_sample_l) > fabs(out_sample_r) ? fabs(out_sample_l) : fabs(out_sample_r);
            float out_db = (out_abs > 1e-10f) ? 20.0f * log10(out_abs) : -200.0f;
            if (out_db > current_peak_out) current_peak_out = out_db;

            float lim_env_clamped = (limiter_envelope > 1e-10f) ? limiter_envelope : 1e-10f;
            float lim_gr_db = 20.0f * log10(lim_env_clamped);
            float total_gr_db = envelope_db + lim_gr_db;
            if (total_gr_db < current_max_gr) current_max_gr = total_gr_db;
        }

        max_gr_db = current_max_gr;
        peak_in_db = current_peak_in;
        peak_out_db = current_peak_out;
    }
}

struct MaximDspState {
    double[kTotalMaximParams] params = 0.0;
    double sample_rate = 44100.0;
    bool enabled = true;
    long last_time_samples = -1;

    BiquadFilter[2] lp1_l, lp1_r;
    BiquadFilter[2] hp1_l, hp1_r;
    BiquadFilter[2] lp2_l, lp2_r;
    BiquadFilter[2] hp2_l, hp2_r;
    BiquadFilter[2] lp2_low_l, lp2_low_r;
    BiquadFilter[2] hp2_low_l, hp2_low_r;

    MaximBandProcessor[4] bands;

    float[4096][kNumMaximBands] band_buf_l;
    float[4096][kNumMaximBands] band_buf_r;
    float[4096] sum_l;
    float[4096] sum_r;

    void updateCrossoverCoeffs() nothrow @nogc @trusted {
        float low_freq = d_maxim_norm_to_freq(params[MaximParamId.PARAM_LOW_XOVER], 20.0f, 500.0f);
        float high_freq = d_maxim_norm_to_freq(params[MaximParamId.PARAM_HIGH_XOVER], 500.0f, 20000.0f);
        foreach (i; 0 .. 2) {
            lp1_l[i].setParams(BiquadFilter.Type.LOWPASS, low_freq, 0.7071f, 0.0f, cast(float)sample_rate);
            lp1_r[i].setParams(BiquadFilter.Type.LOWPASS, low_freq, 0.7071f, 0.0f, cast(float)sample_rate);
            hp1_l[i].setParams(BiquadFilter.Type.HIGHPASS, low_freq, 0.7071f, 0.0f, cast(float)sample_rate);
            hp1_r[i].setParams(BiquadFilter.Type.HIGHPASS, low_freq, 0.7071f, 0.0f, cast(float)sample_rate);

            lp2_l[i].setParams(BiquadFilter.Type.LOWPASS, high_freq, 0.7071f, 0.0f, cast(float)sample_rate);
            lp2_r[i].setParams(BiquadFilter.Type.LOWPASS, high_freq, 0.7071f, 0.0f, cast(float)sample_rate);
            hp2_l[i].setParams(BiquadFilter.Type.HIGHPASS, high_freq, 0.7071f, 0.0f, cast(float)sample_rate);
            hp2_r[i].setParams(BiquadFilter.Type.HIGHPASS, high_freq, 0.7071f, 0.0f, cast(float)sample_rate);

            lp2_low_l[i].setParams(BiquadFilter.Type.LOWPASS, high_freq, 0.7071f, 0.0f, cast(float)sample_rate);
            lp2_low_r[i].setParams(BiquadFilter.Type.LOWPASS, high_freq, 0.7071f, 0.0f, cast(float)sample_rate);
            hp2_low_l[i].setParams(BiquadFilter.Type.HIGHPASS, high_freq, 0.7071f, 0.0f, cast(float)sample_rate);
            hp2_low_r[i].setParams(BiquadFilter.Type.HIGHPASS, high_freq, 0.7071f, 0.0f, cast(float)sample_rate);
        }
    }

    void reset() nothrow @nogc @trusted {
        params[MaximParamId.PARAM_LOW_XOVER] = 0.461;
        params[MaximParamId.PARAM_HIGH_XOVER] = 0.436;
        params[MaximParamId.PARAM_LOOKAHEAD] = 0.2;
        params[MaximParamId.PARAM_ENABLE] = 1.0;
        enabled = true;

        foreach (b; 0 .. 4) {
            int offset = 4 + b * 10;
            params[offset + 0] = 0.5;
            params[offset + 1] = 0.5;
            params[offset + 2] = 0.0;
            params[offset + 3] = 1.0;
            params[offset + 4] = 1.0;
            params[offset + 5] = 0.0;
            params[offset + 6] = 0.0;
            params[offset + 7] = 0.3;
            params[offset + 8] = 0.3;
            params[offset + 9] = (b == 3) ? 0.975 : 1.0;
        }

        foreach (s; 0 .. 2) {
            lp1_l[s].reset();
            lp1_r[s].reset();
            hp1_l[s].reset();
            hp1_r[s].reset();
            lp2_l[s].reset();
            lp2_r[s].reset();
            hp2_l[s].reset();
            hp2_r[s].reset();
            lp2_low_l[s].reset();
            lp2_low_r[s].reset();
            hp2_low_l[s].reset();
            hp2_low_r[s].reset();
        }

        foreach (b; 0 .. 4) {
            bands[b].sample_rate = sample_rate;
            bands[b].reset();
        }

        updateCrossoverCoeffs();
    }
}

MaximDspState* d_maxim_create(double sample_rate) nothrow @nogc @trusted {
    import core.stdc.stdlib : malloc;
    auto s = cast(MaximDspState*)malloc(MaximDspState.sizeof);
    if (s !is null) {
        *s = MaximDspState.init;
        s.sample_rate = sample_rate;
        s.reset();
    }
    return s;
}

void d_maxim_destroy(MaximDspState* state) nothrow @nogc @trusted {
    import core.stdc.stdlib : free;
    if (state !is null) free(state);
}

void d_maxim_reset(MaximDspState* state) nothrow @nogc @safe {
    if (state !is null) state.reset();
}

void d_maxim_set_parameter(MaximDspState* state, uint id, double value) nothrow @nogc @safe {
    if (state !is null && id < kTotalMaximParams) {
        state.params[id] = value;
        if (id == MaximParamId.PARAM_ENABLE) {
            state.enabled = (value >= 0.5);
        }
    }
}

double d_maxim_get_parameter(const MaximDspState* state, uint id) nothrow @nogc @safe {
    if (state !is null && id < kTotalMaximParams) {
        return state.params[id];
    }
    return 0.0;
}

float d_maxim_get_band_gain_reduction_db(const MaximDspState* state, int band) nothrow @nogc @safe {
    if (state !is null && band >= 0 && band < 4) {
        return state.bands[band].max_gr_db;
    }
    return 0.0f;
}

float d_maxim_get_band_input_db(const MaximDspState* state, int band) nothrow @nogc @safe {
    if (state !is null && band >= 0 && band < 4) {
        return state.bands[band].peak_in_db;
    }
    return -200.0f;
}

float d_maxim_get_band_output_db(const MaximDspState* state, int band) nothrow @nogc @safe {
    if (state !is null && band >= 0 && band < 4) {
        return state.bands[band].peak_out_db;
    }
    return -200.0f;
}

void d_maxim_process(
    MaximDspState* state,
    const(float)* in_l,
    const(float)* in_r,
    float* out_l,
    float* out_r,
    int num_samples,
    double sample_rate,
    long continuous_time_samples
) nothrow @nogc @trusted {
    if (state is null) return;

    if (state.sample_rate != sample_rate) {
        state.sample_rate = sample_rate;
        state.updateCrossoverCoeffs();
        foreach (b; 0 .. 4) {
            state.bands[b].sample_rate = sample_rate;
        }
    }

    if (state.last_time_samples >= 0 && continuous_time_samples < state.last_time_samples) {
        foreach (s; 0 .. 2) {
            state.lp1_l[s].reset();
            state.lp1_r[s].reset();
            state.hp1_l[s].reset();
            state.hp1_r[s].reset();
            state.lp2_l[s].reset();
            state.lp2_r[s].reset();
            state.hp2_l[s].reset();
            state.hp2_r[s].reset();
            state.lp2_low_l[s].reset();
            state.lp2_low_r[s].reset();
            state.hp2_low_l[s].reset();
            state.hp2_low_r[s].reset();
        }
        foreach (b; 0 .. 4) {
            state.bands[b].reset();
        }
    }
    state.last_time_samples = continuous_time_samples + num_samples;

    if (in_l !is null && in_l != out_l) {
        foreach (i; 0 .. num_samples) {
            out_l[i] = in_l[i];
            out_r[i] = in_r[i];
        }
    }

    if (!state.enabled) return;

    state.updateCrossoverCoeffs();

    int samples_processed = 0;
    while (samples_processed < num_samples) {
        int chunk = num_samples - samples_processed;
        if (chunk > 4096) chunk = 4096;

        float* cur_out_l = out_l + samples_processed;
        float* cur_out_r = out_r + samples_processed;

        foreach (i; 0 .. chunk) {
            float in_l_samp = cur_out_l[i];
            float in_r_samp = cur_out_r[i];

            float lp1_l_val = state.lp1_l[1].process(state.lp1_l[0].process(in_l_samp));
            float hp1_l_val = state.hp1_l[1].process(state.hp1_l[0].process(in_l_samp));
            float lp1_r_val = state.lp1_r[1].process(state.lp1_r[0].process(in_r_samp));
            float hp1_r_val = state.hp1_r[1].process(state.hp1_r[0].process(in_r_samp));

            float lp2_l_val = state.lp2_l[1].process(state.lp2_l[0].process(hp1_l_val));
            float hp2_l_val = state.hp2_l[1].process(state.hp2_l[0].process(hp1_l_val));
            float lp2_r_val = state.lp2_r[1].process(state.lp2_r[0].process(hp1_r_val));
            float hp2_r_val = state.hp2_r[1].process(state.hp2_r[0].process(hp1_r_val));

            float lp2_low_l_val = state.lp2_low_l[1].process(state.lp2_low_l[0].process(lp1_l_val));
            float hp2_low_l_val = state.hp2_low_l[1].process(state.hp2_low_l[0].process(lp1_l_val));
            float lp2_low_r_val = state.lp2_low_r[1].process(state.lp2_low_r[0].process(lp1_r_val));
            float hp2_low_r_val = state.hp2_low_r[1].process(state.hp2_low_r[0].process(lp1_r_val));

            state.band_buf_l[0][i] = lp2_low_l_val + hp2_low_l_val;
            state.band_buf_r[0][i] = lp2_low_r_val + hp2_low_r_val;
            state.band_buf_l[1][i] = lp2_l_val;
            state.band_buf_r[1][i] = lp2_r_val;
            state.band_buf_l[2][i] = hp2_l_val;
            state.band_buf_r[2][i] = hp2_r_val;
        }

        float lookahead_ms = d_maxim_norm_to_lookahead_ms(state.params[MaximParamId.PARAM_LOOKAHEAD]);

        foreach (b; 0 .. kNumMaximBands) {
            int offset = 4 + b * 10;
            state.bands[b].process(
                state.band_buf_l[b].ptr,
                state.band_buf_r[b].ptr,
                state.band_buf_l[b].ptr,
                state.band_buf_r[b].ptr,
                chunk,
                d_maxim_norm_to_gain_db(state.params[offset + 0]),
                d_maxim_norm_to_gain_db(state.params[offset + 1]),
                cast(float)state.params[offset + 2],
                d_maxim_norm_to_sat_thresh_db(state.params[offset + 3]),
                d_maxim_norm_to_thresh_db(state.params[offset + 4]),
                d_maxim_norm_to_ratio(state.params[offset + 5]),
                d_maxim_norm_to_knee_db(state.params[offset + 6]),
                d_maxim_norm_to_attack_ms(state.params[offset + 7]),
                d_maxim_norm_to_release_ms(state.params[offset + 8]),
                d_maxim_norm_to_ceiling_db(state.params[offset + 9]),
                lookahead_ms
            );
        }

        foreach (i; 0 .. chunk) {
            float s_l = 0.0f;
            float s_r = 0.0f;
            foreach (b; 0 .. kNumMaximBands) {
                s_l += state.band_buf_l[b][i];
                s_r += state.band_buf_r[b][i];
            }
            state.sum_l[i] = s_l;
            state.sum_r[i] = s_r;
        }

        int master_offset = 4 + 3 * 10;
        state.bands[3].process(
            state.sum_l.ptr,
            state.sum_r.ptr,
            cur_out_l,
            cur_out_r,
            chunk,
            d_maxim_norm_to_gain_db(state.params[master_offset + 0]),
            d_maxim_norm_to_gain_db(state.params[master_offset + 1]),
            cast(float)state.params[master_offset + 2],
            d_maxim_norm_to_sat_thresh_db(state.params[master_offset + 3]),
            d_maxim_norm_to_thresh_db(state.params[master_offset + 4]),
            d_maxim_norm_to_ratio(state.params[master_offset + 5]),
            d_maxim_norm_to_knee_db(state.params[master_offset + 6]),
            d_maxim_norm_to_attack_ms(state.params[master_offset + 7]),
            d_maxim_norm_to_release_ms(state.params[master_offset + 8]),
            d_maxim_norm_to_ceiling_db(state.params[master_offset + 9]),
            lookahead_ms
        );

        samples_processed += chunk;
    }
}

}
