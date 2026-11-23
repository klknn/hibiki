module engine.effects.builtin_compressor;

import core.stdc.math : exp, expf, log10f, pow, powf, sqrtf;

extern (C++, "hibiki") {

enum kRmsWindowSize = 128;

struct CompressorDspState {
    float envelope_db = 0.0f;
    float gain_reduction_db = 0.0f;
    float input_db = -200.0f;
    float output_db = -200.0f;
    float sidechain_db = -200.0f;

    float[kRmsWindowSize] rms_buf_l = 0.0f;
    float[kRmsWindowSize] rms_buf_r = 0.0f;
    float rms_sum_l = 0.0f;
    float rms_sum_r = 0.0f;
    int rms_index = 0;

    void reset() pure nothrow @nogc @safe {
        envelope_db = 0.0f;
        gain_reduction_db = 0.0f;
        input_db = -200.0f;
        output_db = -200.0f;
        sidechain_db = -200.0f;
        rms_buf_l[] = 0.0f;
        rms_buf_r[] = 0.0f;
        rms_sum_l = 0.0f;
        rms_sum_r = 0.0f;
        rms_index = 0;
    }
}

pure nothrow @nogc @safe float d_compressor_norm_to_threshold(double norm) {
    return cast(float)(norm * 60.0 - 60.0);
}

pure nothrow @nogc @safe float d_compressor_norm_to_ratio(double norm) {
    if (norm >= 0.999) return 1000.0f;
    return 1.0f / (1.0f - cast(float)norm);
}

nothrow @nogc @trusted float d_compressor_norm_to_attack(double norm) {
    return cast(float)(0.1 * pow(1000.0, norm));
}

nothrow @nogc @trusted float d_compressor_norm_to_release(double norm) {
    return cast(float)(10.0 * pow(100.0, norm));
}

pure nothrow @nogc @safe float d_compressor_norm_to_up_threshold(double norm) {
    return cast(float)(norm * 72.0 - 60.0);
}

pure nothrow @nogc @safe float d_compressor_compute_gain_reduction(
    float input_db,
    float threshold,
    float ratio,
    float knee_db,
    float up_threshold,
    float up_ratio
) {
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
            // no downward reduction
        } else if (input_db >= upper) {
            gr_db = (threshold - input_db) * (1.0f - 1.0f / ratio);
        } else {
            float x = input_db - lower;
            gr_db = -(1.0f - 1.0f / ratio) * x * x / (2.0f * knee_db);
        }
    }

    if (up_ratio > 1.001f && input_db < up_threshold && input_db > -100.0f) {
        float under = up_threshold - input_db;
        float target = up_threshold - under / up_ratio;
        gr_db += (target - input_db);
    }

    return gr_db;
}

void d_compressor_process(
    ref CompressorDspState state,
    const(float)* in_l,
    const(float)* in_r,
    float* out_l,
    float* out_r,
    int num_samples,
    double sample_rate,
    double threshold_norm,
    double ratio_norm,
    double attack_norm,
    double release_norm,
    double knee_norm,
    double makeup_norm,
    double up_threshold_norm,
    double up_ratio_norm,
    bool use_rms,
    bool enabled,
    const(float)* sc_l,
    const(float)* sc_r
) nothrow @nogc @trusted {
    if (in_l && in_l != out_l) {
        foreach (i; 0 .. num_samples) {
            out_l[i] = in_l[i];
            out_r[i] = in_r[i];
        }
    }

    if (!enabled) {
        state.gain_reduction_db = 0.0f;
        return;
    }

    float threshold = d_compressor_norm_to_threshold(threshold_norm);
    float ratio = d_compressor_norm_to_ratio(ratio_norm);
    float attack_ms = d_compressor_norm_to_attack(attack_norm);
    float release_ms = d_compressor_norm_to_release(release_norm);
    float knee_db = cast(float)(knee_norm * 30.0);
    float makeup_lin = cast(float)pow(10.0, makeup_norm * 30.0 / 20.0);
    float up_threshold = d_compressor_norm_to_up_threshold(up_threshold_norm);
    float up_ratio = d_compressor_norm_to_ratio(up_ratio_norm);

    float attack_coeff = cast(float)exp(-1.0 / (attack_ms * 0.001 * sample_rate));
    float release_coeff = cast(float)exp(-1.0 / (release_ms * 0.001 * sample_rate));

    float peak_gr = 0.0f;
    float peak_input_db = -200.0f;
    float peak_output_db = -200.0f;

    const(float)* det_l = (sc_l !is null) ? sc_l : out_l;
    const(float)* det_r = (sc_r !is null) ? sc_r : out_r;
    bool has_sidechain = (sc_l !is null && sc_r !is null);
    float peak_sidechain_db = -200.0f;

    float env_db = state.envelope_db;
    float rms_sum_l = state.rms_sum_l;
    float rms_sum_r = state.rms_sum_r;
    int rms_idx = state.rms_index;
    const float one_minus_att = 1.0f - attack_coeff;
    const float one_minus_rel = 1.0f - release_coeff;
    enum float kInvRmsWindow = 1.0f / cast(float)kRmsWindowSize;
    enum float kLn10Div20 = 0.11512925464970228f; // ln(10) / 20

    foreach (i; 0 .. num_samples) {
        float input_abs;
        if (use_rms) {
            float dl = det_l[i];
            float dr = det_r[i];
            float s_l = dl * dl;
            float s_r = dr * dr;
            rms_sum_l -= state.rms_buf_l[rms_idx];
            rms_sum_r -= state.rms_buf_r[rms_idx];
            state.rms_buf_l[rms_idx] = s_l;
            state.rms_buf_r[rms_idx] = s_r;
            rms_sum_l += s_l;
            rms_sum_r += s_r;
            rms_idx = (rms_idx + 1) & (kRmsWindowSize - 1);
            float sum_l = rms_sum_l > 0.0f ? rms_sum_l : 0.0f;
            float sum_r = rms_sum_r > 0.0f ? rms_sum_r : 0.0f;
            float rms_l = sqrtf(sum_l * kInvRmsWindow);
            float rms_r = sqrtf(sum_r * kInvRmsWindow);
            input_abs = (rms_l > rms_r) ? rms_l : rms_r;
        } else {
            float dl = det_l[i];
            float dr = det_r[i];
            float abs_l = dl >= 0 ? dl : -dl;
            float abs_r = dr >= 0 ? dr : -dr;
            input_abs = (abs_l > abs_r) ? abs_l : abs_r;
        }

        float input_db = (input_abs > 1e-10f) ? (20.0f * log10f(input_abs)) : -200.0f;
        if (input_db > peak_input_db) peak_input_db = input_db;
        if (has_sidechain && input_db > peak_sidechain_db) peak_sidechain_db = input_db;

        float gr_db = d_compressor_compute_gain_reduction(input_db, threshold, ratio, knee_db, up_threshold, up_ratio);

        if (gr_db < env_db) {
            env_db = attack_coeff * env_db + one_minus_att * gr_db;
        } else {
            env_db = release_coeff * env_db + one_minus_rel * gr_db;
        }

        float gain = expf(env_db * kLn10Div20) * makeup_lin;
        float ol = out_l[i] * gain;
        float or_ = out_r[i] * gain;
        out_l[i] = ol;
        out_r[i] = or_;

        float out_abs_l = ol >= 0 ? ol : -ol;
        float out_abs_r = or_ >= 0 ? or_ : -or_;
        float out_abs = (out_abs_l > out_abs_r) ? out_abs_l : out_abs_r;
        float output_db = (out_abs > 1e-10f) ? (20.0f * log10f(out_abs)) : -200.0f;
        if (output_db > peak_output_db) peak_output_db = output_db;

        if (env_db < peak_gr) peak_gr = env_db;
    }

    state.envelope_db = env_db;
    state.rms_sum_l = rms_sum_l;
    state.rms_sum_r = rms_sum_r;
    state.rms_index = rms_idx;
    state.gain_reduction_db = peak_gr;
    state.input_db = peak_input_db;
    state.output_db = peak_output_db;
    state.sidechain_db = has_sidechain ? peak_sidechain_db : -200.0f;
}

}
