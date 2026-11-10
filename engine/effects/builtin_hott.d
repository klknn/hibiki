module engine.effects.builtin_hott;

import core.stdc.math : exp, fabs, log10, pow;
import engine.core.biquad_filter : BiquadFilter;
import engine.core.math : clampVal;
import engine.effects.builtin_compressor : CompressorDspState, d_compressor_process;

extern (C++, "hibiki") {

enum kTotalHottParams = 32;
enum kNumHottBands = 3;

enum HottParamId {
    PARAM_LOW_CROSSOVER = 0,
    PARAM_HIGH_CROSSOVER = 1,
    PARAM_AMOUNT = 2,
    PARAM_TIME = 3,
    PARAM_OUTPUT = 4,
    PARAM_LOW_OUT = 5,
    PARAM_MID_OUT = 6,
    PARAM_HIGH_OUT = 7,
    PARAM_ENABLE = 8,
    PARAM_LOW_DOWN_THRESH = 9,
    PARAM_MID_DOWN_THRESH = 10,
    PARAM_HIGH_DOWN_THRESH = 11,
    PARAM_LOW_UP_THRESH = 12,
    PARAM_MID_UP_THRESH = 13,
    PARAM_HIGH_UP_THRESH = 14,
    PARAM_SOFT_KNEE = 15,
    PARAM_RMS_MODE = 16,
    PARAM_LOW_ATTACK = 17,
    PARAM_MID_ATTACK = 18,
    PARAM_HIGH_ATTACK = 19,
    PARAM_LOW_RELEASE = 20,
    PARAM_MID_RELEASE = 21,
    PARAM_HIGH_RELEASE = 22,
    PARAM_LOW_DOWN_RATIO = 23,
    PARAM_MID_DOWN_RATIO = 24,
    PARAM_HIGH_DOWN_RATIO = 25,
    PARAM_LOW_UP_RATIO = 26,
    PARAM_MID_UP_RATIO = 27,
    PARAM_HIGH_UP_RATIO = 28,
    PARAM_LOW_IN = 29,
    PARAM_MID_IN = 30,
    PARAM_HIGH_IN = 31,
}

enum float kDefaultLowDownThresh = -33.8f;
enum float kDefaultMidDownThresh = -30.2f;
enum float kDefaultHighDownThresh = -35.5f;
enum float kDefaultLowDownRatio = 66.7f;
enum float kDefaultMidDownRatio = 66.7f;
enum float kDefaultHighDownRatio = 1000.0f;

enum float kDefaultLowUpThresh = -40.8f;
enum float kDefaultMidUpThresh = -41.8f;
enum float kDefaultHighUpThresh = -40.8f;
enum float kDefaultLowUpRatio = 4.17f;
enum float kDefaultMidUpRatio = 4.17f;
enum float kDefaultHighUpRatio = 4.17f;

enum float kDefaultLowAttack = 47.8f;
enum float kDefaultLowRelease = 282.0f;
enum float kDefaultMidAttack = 22.4f;
enum float kDefaultMidRelease = 282.0f;
enum float kDefaultHighAttack = 13.5f;
enum float kDefaultHighRelease = 132.0f;

struct BandDefaults {
    double threshold_norm;
    double ratio_norm;
    double attack_norm;
    double release_norm;
}

BandDefaults computeBandDefaults(float threshold_db, float ratio, float attack_ms, float release_ms) nothrow @nogc @trusted {
    BandDefaults d;
    d.threshold_norm = (threshold_db + 60.0) / 60.0;
    d.ratio_norm = (ratio >= 999.0f) ? 0.999 : (1.0 - 1.0 / ratio);
    d.attack_norm = log10(attack_ms / 0.1) / 3.0;
    d.release_norm = log10(release_ms / 10.0) / 2.0;
    if (d.threshold_norm < 0.0) d.threshold_norm = 0.0;
    if (d.threshold_norm > 1.0) d.threshold_norm = 1.0;
    if (d.ratio_norm < 0.0) d.ratio_norm = 0.0;
    if (d.ratio_norm > 1.0) d.ratio_norm = 1.0;
    if (d.attack_norm < 0.0) d.attack_norm = 0.0;
    if (d.attack_norm > 1.0) d.attack_norm = 1.0;
    if (d.release_norm < 0.0) d.release_norm = 0.0;
    if (d.release_norm > 1.0) d.release_norm = 1.0;
    return d;
}

nothrow @nogc @trusted float d_hott_norm_to_freq(double norm, float min_hz, float max_hz) {
    return min_hz * pow(max_hz / min_hz, cast(float)norm);
}

pure nothrow @nogc @safe float d_hott_norm_to_db24(double norm) {
    return cast(float)(norm * 48.0 - 24.0);
}

struct HottDspState {
    double[kTotalHottParams] params = 0.0;
    double sample_rate = 44100.0;
    bool enabled = true;
    long last_time_samples = -1;

    float input_db = -200.0f;
    float output_db = -200.0f;

    BiquadFilter[2] lp1_l, lp1_r;
    BiquadFilter[2] hp1_l, hp1_r;
    BiquadFilter[2] lp2_l, lp2_r;
    BiquadFilter[2] hp2_l, hp2_r;

    CompressorDspState[kNumHottBands] band_comp;

    // Temporary scratch buffers for 3 bands up to 4096 samples
    float[4096][kNumHottBands] band_buf_l;
    float[4096][kNumHottBands] band_buf_r;

    void updateCrossoverCoeffs() nothrow @nogc @trusted {
        float low_freq = d_hott_norm_to_freq(params[HottParamId.PARAM_LOW_CROSSOVER], 20.0f, 500.0f);
        float high_freq = d_hott_norm_to_freq(params[HottParamId.PARAM_HIGH_CROSSOVER], 500.0f, 20000.0f);
        foreach (i; 0 .. 2) {
            lp1_l[i].setParams(BiquadFilter.Type.LOWPASS, low_freq, 0.7071f, 0.0f, cast(float)sample_rate);
            lp1_r[i].setParams(BiquadFilter.Type.LOWPASS, low_freq, 0.7071f, 0.0f, cast(float)sample_rate);
            hp1_l[i].setParams(BiquadFilter.Type.HIGHPASS, low_freq, 0.7071f, 0.0f, cast(float)sample_rate);
            hp1_r[i].setParams(BiquadFilter.Type.HIGHPASS, low_freq, 0.7071f, 0.0f, cast(float)sample_rate);

            lp2_l[i].setParams(BiquadFilter.Type.LOWPASS, high_freq, 0.7071f, 0.0f, cast(float)sample_rate);
            lp2_r[i].setParams(BiquadFilter.Type.LOWPASS, high_freq, 0.7071f, 0.0f, cast(float)sample_rate);
            hp2_l[i].setParams(BiquadFilter.Type.HIGHPASS, high_freq, 0.7071f, 0.0f, cast(float)sample_rate);
            hp2_r[i].setParams(BiquadFilter.Type.HIGHPASS, high_freq, 0.7071f, 0.0f, cast(float)sample_rate);
        }
    }

    void reset() nothrow @nogc @trusted {
        params[HottParamId.PARAM_LOW_CROSSOVER] = 0.461;
        params[HottParamId.PARAM_HIGH_CROSSOVER] = 0.436;
        params[HottParamId.PARAM_AMOUNT] = 1.0;
        params[HottParamId.PARAM_TIME] = 1.0;
        params[HottParamId.PARAM_OUTPUT] = 0.5;
        params[HottParamId.PARAM_LOW_OUT] = 0.715;
        params[HottParamId.PARAM_MID_OUT] = 0.619;
        params[HottParamId.PARAM_HIGH_OUT] = 0.715;
        params[HottParamId.PARAM_ENABLE] = 1.0;
        enabled = true;

        params[HottParamId.PARAM_LOW_DOWN_THRESH] = (kDefaultLowDownThresh + 60.0) / 60.0;
        params[HottParamId.PARAM_MID_DOWN_THRESH] = (kDefaultMidDownThresh + 60.0) / 60.0;
        params[HottParamId.PARAM_HIGH_DOWN_THRESH] = (kDefaultHighDownThresh + 60.0) / 60.0;

        params[HottParamId.PARAM_LOW_UP_THRESH] = (kDefaultLowUpThresh + 60.0) / 72.0;
        params[HottParamId.PARAM_MID_UP_THRESH] = (kDefaultMidUpThresh + 60.0) / 72.0;
        params[HottParamId.PARAM_HIGH_UP_THRESH] = (kDefaultHighUpThresh + 60.0) / 72.0;

        params[HottParamId.PARAM_SOFT_KNEE] = 1.0;
        params[HottParamId.PARAM_RMS_MODE] = 1.0;

        auto bd_low = computeBandDefaults(0, 1, kDefaultLowAttack, kDefaultLowRelease);
        auto bd_mid = computeBandDefaults(0, 1, kDefaultMidAttack, kDefaultMidRelease);
        auto bd_hi = computeBandDefaults(0, 1, kDefaultHighAttack, kDefaultHighRelease);
        params[HottParamId.PARAM_LOW_ATTACK] = bd_low.attack_norm;
        params[HottParamId.PARAM_MID_ATTACK] = bd_mid.attack_norm;
        params[HottParamId.PARAM_HIGH_ATTACK] = bd_hi.attack_norm;
        params[HottParamId.PARAM_LOW_RELEASE] = bd_low.release_norm;
        params[HottParamId.PARAM_MID_RELEASE] = bd_mid.release_norm;
        params[HottParamId.PARAM_HIGH_RELEASE] = bd_hi.release_norm;

        auto r_low_down = computeBandDefaults(0, kDefaultLowDownRatio, 1, 10);
        auto r_mid_down = computeBandDefaults(0, kDefaultMidDownRatio, 1, 10);
        auto r_hi_down = computeBandDefaults(0, kDefaultHighDownRatio, 1, 10);
        params[HottParamId.PARAM_LOW_DOWN_RATIO] = r_low_down.ratio_norm;
        params[HottParamId.PARAM_MID_DOWN_RATIO] = r_mid_down.ratio_norm;
        params[HottParamId.PARAM_HIGH_DOWN_RATIO] = r_hi_down.ratio_norm;

        auto r_low_up = computeBandDefaults(0, kDefaultLowUpRatio, 1, 10);
        auto r_mid_up = computeBandDefaults(0, kDefaultMidUpRatio, 1, 10);
        auto r_hi_up = computeBandDefaults(0, kDefaultHighUpRatio, 1, 10);
        params[HottParamId.PARAM_LOW_UP_RATIO] = r_low_up.ratio_norm;
        params[HottParamId.PARAM_MID_UP_RATIO] = r_mid_up.ratio_norm;
        params[HottParamId.PARAM_HIGH_UP_RATIO] = r_hi_up.ratio_norm;

        params[HottParamId.PARAM_LOW_IN] = 0.608;
        params[HottParamId.PARAM_MID_IN] = 0.608;
        params[HottParamId.PARAM_HIGH_IN] = 0.608;

        foreach (b; 0 .. kNumHottBands) {
            band_comp[b].reset();
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
        }

        updateCrossoverCoeffs();
    }
}

HottDspState* d_hott_create() nothrow @nogc @trusted {
    import core.stdc.stdlib : malloc;
    auto s = cast(HottDspState*)malloc(HottDspState.sizeof);
    if (s !is null) {
        *s = HottDspState.init;
        s.reset();
    }
    return s;
}

void d_hott_destroy(HottDspState* state) nothrow @nogc @trusted {
    import core.stdc.stdlib : free;
    if (state !is null) free(state);
}

void d_hott_reset(HottDspState* state) nothrow @nogc @safe {
    if (state !is null) state.reset();
}

void d_hott_set_parameter(HottDspState* state, uint id, double value) nothrow @nogc @safe {
    if (state !is null && id < kTotalHottParams) {
        state.params[id] = value;
        if (id == HottParamId.PARAM_ENABLE) {
            state.enabled = (value >= 0.5);
        }
    }
}

double d_hott_get_parameter(const HottDspState* state, uint id) nothrow @nogc @safe {
    if (state !is null && id < kTotalHottParams) {
        return state.params[id];
    }
    return 0.0;
}

float d_hott_get_band_gain_reduction(const HottDspState* state, int band) nothrow @nogc @safe {
    if (state !is null && band >= 0 && band < kNumHottBands) {
        return state.band_comp[band].gain_reduction_db;
    }
    return 0.0f;
}

float d_hott_get_input_db(const HottDspState* state) nothrow @nogc @safe {
    return state !is null ? state.input_db : -200.0f;
}

float d_hott_get_output_db(const HottDspState* state) nothrow @nogc @safe {
    return state !is null ? state.output_db : -200.0f;
}

void d_hott_process(
    HottDspState* state,
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

    float amount = cast(float)state.params[HottParamId.PARAM_AMOUNT];
    float global_out_db = d_hott_norm_to_db24(state.params[HottParamId.PARAM_OUTPUT]);
    float global_out_lin = pow(10.0f, global_out_db / 20.0f);

    float[kNumHottBands] band_in_lin;
    band_in_lin[0] = pow(10.0f, d_hott_norm_to_db24(state.params[HottParamId.PARAM_LOW_IN]) / 20.0f);
    band_in_lin[1] = pow(10.0f, d_hott_norm_to_db24(state.params[HottParamId.PARAM_MID_IN]) / 20.0f);
    band_in_lin[2] = pow(10.0f, d_hott_norm_to_db24(state.params[HottParamId.PARAM_HIGH_IN]) / 20.0f);

    float[kNumHottBands] band_out_lin;
    band_out_lin[0] = pow(10.0f, d_hott_norm_to_db24(state.params[HottParamId.PARAM_LOW_OUT]) / 20.0f);
    band_out_lin[1] = pow(10.0f, d_hott_norm_to_db24(state.params[HottParamId.PARAM_MID_OUT]) / 20.0f);
    band_out_lin[2] = pow(10.0f, d_hott_norm_to_db24(state.params[HottParamId.PARAM_HIGH_OUT]) / 20.0f);

    state.updateCrossoverCoeffs();

    float peak_input_db = -200.0f;
    float peak_output_db = -200.0f;

    // Process in chunks of up to 4096
    int samples_processed = 0;
    while (samples_processed < num_samples) {
        int chunk = num_samples - samples_processed;
        if (chunk > 4096) chunk = 4096;

        float* cur_out_l = out_l + samples_processed;
        float* cur_out_r = out_r + samples_processed;

        // Split into 3 bands using LR4 crossover
        foreach (i; 0 .. chunk) {
            float inl = cur_out_l[i];
            float inr = cur_out_r[i];

            float in_abs = fabs(inl) > fabs(inr) ? fabs(inl) : fabs(inr);
            float in_db = (in_abs > 1e-10f) ? 20.0f * log10(in_abs) : -200.0f;
            if (in_db > peak_input_db) peak_input_db = in_db;

            float lp1_l_val = state.lp1_l[1].process(state.lp1_l[0].process(inl));
            float hp1_l_val = state.hp1_l[1].process(state.hp1_l[0].process(inl));

            float lp1_r_val = state.lp1_r[1].process(state.lp1_r[0].process(inr));
            float hp1_r_val = state.hp1_r[1].process(state.hp1_r[0].process(inr));

            float lp2_l_val = state.lp2_l[1].process(state.lp2_l[0].process(hp1_l_val));
            float hp2_l_val = state.hp2_l[1].process(state.hp2_l[0].process(hp1_l_val));

            float lp2_r_val = state.lp2_r[1].process(state.lp2_r[0].process(hp1_r_val));
            float hp2_r_val = state.hp2_r[1].process(state.hp2_r[0].process(hp1_r_val));

            state.band_buf_l[0][i] = lp1_l_val;
            state.band_buf_r[0][i] = lp1_r_val;
            state.band_buf_l[1][i] = lp2_l_val;
            state.band_buf_r[1][i] = lp2_r_val;
            state.band_buf_l[2][i] = hp2_l_val;
            state.band_buf_r[2][i] = hp2_r_val;
        }

        float time_mult = cast(float)(state.params[HottParamId.PARAM_TIME] * 2.0);
        if (time_mult < 0.01f) time_mult = 0.01f;

        double[kNumHottBands] down_thresh_norm = [
            state.params[HottParamId.PARAM_LOW_DOWN_THRESH],
            state.params[HottParamId.PARAM_MID_DOWN_THRESH],
            state.params[HottParamId.PARAM_HIGH_DOWN_THRESH]
        ];
        double[kNumHottBands] up_thresh_norm = [
            state.params[HottParamId.PARAM_LOW_UP_THRESH],
            state.params[HottParamId.PARAM_MID_UP_THRESH],
            state.params[HottParamId.PARAM_HIGH_UP_THRESH]
        ];
        double[kNumHottBands] down_ratio_norm = [
            state.params[HottParamId.PARAM_LOW_DOWN_RATIO],
            state.params[HottParamId.PARAM_MID_DOWN_RATIO],
            state.params[HottParamId.PARAM_HIGH_DOWN_RATIO]
        ];
        double[kNumHottBands] up_ratio_norm = [
            state.params[HottParamId.PARAM_LOW_UP_RATIO],
            state.params[HottParamId.PARAM_MID_UP_RATIO],
            state.params[HottParamId.PARAM_HIGH_UP_RATIO]
        ];
        double[kNumHottBands] att_norm = [
            state.params[HottParamId.PARAM_LOW_ATTACK],
            state.params[HottParamId.PARAM_MID_ATTACK],
            state.params[HottParamId.PARAM_HIGH_ATTACK]
        ];
        double[kNumHottBands] rel_norm = [
            state.params[HottParamId.PARAM_LOW_RELEASE],
            state.params[HottParamId.PARAM_MID_RELEASE],
            state.params[HottParamId.PARAM_HIGH_RELEASE]
        ];

        double knee_norm = (state.params[HottParamId.PARAM_SOFT_KNEE] >= 0.5) ? 0.33 : 0.0;
        bool use_rms = (state.params[HottParamId.PARAM_RMS_MODE] >= 0.5);

        foreach (b; 0 .. kNumHottBands) {
            foreach (i; 0 .. chunk) {
                state.band_buf_l[b][i] *= band_in_lin[b];
                state.band_buf_r[b][i] *= band_in_lin[b];
            }

            float att_ms = 0.1f * pow(1000.0f, cast(float)att_norm[b]) * time_mult;
            float rel_ms = 10.0f * pow(100.0f, cast(float)rel_norm[b]) * time_mult;
            auto bd = computeBandDefaults(0.0f, 1.0f, att_ms, rel_ms);

            d_compressor_process(
                state.band_comp[b],
                state.band_buf_l[b].ptr,
                state.band_buf_r[b].ptr,
                state.band_buf_l[b].ptr,
                state.band_buf_r[b].ptr,
                chunk,
                sample_rate,
                down_thresh_norm[b],
                down_ratio_norm[b],
                bd.attack_norm,
                bd.release_norm,
                knee_norm,
                0.0, // makeup
                up_thresh_norm[b],
                up_ratio_norm[b],
                use_rms,
                true, // enabled
                null,
                null
            );
        }

        foreach (i; 0 .. chunk) {
            float dry_l = cur_out_l[i];
            float dry_r = cur_out_r[i];

            float sum_l = 0.0f;
            float sum_r = 0.0f;
            foreach (b; 0 .. kNumHottBands) {
                sum_l += state.band_buf_l[b][i] * band_out_lin[b];
                sum_r += state.band_buf_r[b][i] * band_out_lin[b];
            }

            cur_out_l[i] = (dry_l * (1.0f - amount) + sum_l * amount) * global_out_lin;
            cur_out_r[i] = (dry_r * (1.0f - amount) + sum_r * amount) * global_out_lin;

            float out_abs = fabs(cur_out_l[i]) > fabs(cur_out_r[i]) ? fabs(cur_out_l[i]) : fabs(cur_out_r[i]);
            float out_db = (out_abs > 1e-10f) ? 20.0f * log10(out_abs) : -200.0f;
            if (out_db > peak_output_db) peak_output_db = out_db;
        }

        samples_processed += chunk;
    }

    state.input_db = peak_input_db;
    state.output_db = peak_output_db;
}

}
