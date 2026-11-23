module engine.effects.builtin_phaser;

import core.stdc.math : fmod, pow, sin, sqrt, tan;
import engine.core.math : clampVal, pi;

extern (C++, "hibiki") {

enum kMaxPhaserStages = 12;
enum kPhaserScopeSize = 256;
enum kMaxCombDelay = 4096;
enum kTotalPhaserParams = 12;

enum PhaserParamId {
    PARAM_RATE = 0,
    PARAM_DEPTH = 1,
    PARAM_FEEDBACK = 2,
    PARAM_STAGES = 3,
    PARAM_MIX = 4,
    PARAM_STEREO = 5,
    PARAM_MODE = 6,
    PARAM_LFO_SHAPE = 7,
    PARAM_CENTER = 8,
    PARAM_SPREAD = 9,
    PARAM_SYNC = 10,
    PARAM_ENABLE = 11,
}

enum PhaserMode {
    MODE_PHASER = 0,
    MODE_CHORUS = 1,
    MODE_FLANGER = 2,
    MODE_RINGMOD = 3,
    MODE_DISPERSER = 4,
}

struct PhaserDspState {
    double[kTotalPhaserParams] params = 0.0;
    double sample_rate = 44100.0;
    bool enabled = true;

    double lfo_phase = 0.0;
    float[kMaxPhaserStages] ap_state_l = 0.0f;
    float[kMaxPhaserStages] ap_state_r = 0.0f;
    float feedback_l = 0.0f;
    float feedback_r = 0.0f;

    float[kMaxCombDelay] comb_buf_l = 0.0f;
    float[kMaxCombDelay] comb_buf_r = 0.0f;
    int comb_write = 0;

    float[kPhaserScopeSize] scope_l = 0.0f;
    float[kPhaserScopeSize] scope_r = 0.0f;
    int scope_write = 0;

    float input_rms = 0.0f;
    float output_rms = 0.0f;

    void reset() nothrow @nogc @safe {
        foreach (ref s; ap_state_l) s = 0.0f;
        foreach (ref s; ap_state_r) s = 0.0f;
        feedback_l = 0.0f;
        feedback_r = 0.0f;
        lfo_phase = 0.0;
        comb_write = 0;
        foreach (ref s; comb_buf_l) s = 0.0f;
        foreach (ref s; comb_buf_r) s = 0.0f;
        scope_write = 0;
        foreach (ref s; scope_l) s = 0.0f;
        foreach (ref s; scope_r) s = 0.0f;

        params[PhaserParamId.PARAM_RATE] = 0.3;
        params[PhaserParamId.PARAM_DEPTH] = 0.7;
        params[PhaserParamId.PARAM_FEEDBACK] = 0.4;
        params[PhaserParamId.PARAM_STAGES] = 0.33;
        params[PhaserParamId.PARAM_MIX] = 0.5;
        params[PhaserParamId.PARAM_STEREO] = 0.5;
        params[PhaserParamId.PARAM_MODE] = 0.0;
        params[PhaserParamId.PARAM_LFO_SHAPE] = 0.0;
        params[PhaserParamId.PARAM_CENTER] = 0.5;
        params[PhaserParamId.PARAM_SPREAD] = 0.5;
        params[PhaserParamId.PARAM_SYNC] = 0.0;
        params[PhaserParamId.PARAM_ENABLE] = 1.0;
        enabled = true;
    }
}

nothrow @nogc @trusted float d_phaser_norm_to_rate(double norm) {
    return cast(float)(0.01 * pow(2000.0, norm));
}

nothrow @nogc @safe int d_phaser_norm_to_stages(double norm) {
    int s = cast(int)(norm * 5.0 + 0.5);
    return clampVal(s * 2 + 2, 2, kMaxPhaserStages);
}

nothrow @nogc @safe int d_phaser_norm_to_mode(double norm) {
    return clampVal(cast(int)(norm * 4.0 + 0.5), 0, 4);
}

float d_phaser_lfo(double phase, int shape) nothrow @nogc @trusted {
    float p = cast(float)fmod(phase, 1.0);
    if (p < 0) p += 1.0f;

    switch (shape) {
        case 0:
            return cast(float)sin(p * 2.0 * pi);
        case 1:
            return (p < 0.5f) ? (4.0f * p - 1.0f) : (3.0f - 4.0f * p);
        case 2:
            return 2.0f * p - 1.0f;
        case 3:
            return (p < 0.5f) ? 1.0f : -1.0f;
        default:
            return cast(float)sin(p * 2.0 * pi);
    }
}

float d_phaser_allpass(float input_val, float coeff, ref float state) nothrow @nogc pure @safe {
    float output = coeff * input_val + state;
    state = input_val - coeff * output;
    return output;
}

PhaserDspState* d_phaser_create() nothrow @nogc @trusted {
    import core.stdc.stdlib : malloc;
    auto s = cast(PhaserDspState*)malloc(PhaserDspState.sizeof);
    if (s !is null) {
        *s = PhaserDspState.init;
        s.reset();
    }
    return s;
}

void d_phaser_destroy(PhaserDspState* state) nothrow @nogc @trusted {
    import core.stdc.stdlib : free;
    if (state !is null) free(state);
}

void d_phaser_reset(PhaserDspState* state) nothrow @nogc @safe {
    if (state !is null) state.reset();
}

void d_phaser_set_parameter(PhaserDspState* state, uint id, double value) nothrow @nogc @safe {
    if (state !is null && id < kTotalPhaserParams) {
        state.params[id] = value;
        if (id == PhaserParamId.PARAM_ENABLE) {
            state.enabled = value >= 0.5;
        }
    }
}

double d_phaser_get_parameter(const(PhaserDspState)* state, uint id) nothrow @nogc @safe {
    if (state !is null && id < kTotalPhaserParams) {
        return state.params[id];
    }
    return 0.0;
}

float d_phaser_get_input_db(const(PhaserDspState)* state) nothrow @nogc @trusted {
    import core.stdc.math : log10;
    if (state is null || state.input_rms <= 0) return -100.0f;
    return cast(float)(20.0 * log10(state.input_rms));
}

float d_phaser_get_output_db(const(PhaserDspState)* state) nothrow @nogc @trusted {
    import core.stdc.math : log10;
    if (state is null || state.output_rms <= 0) return -100.0f;
    return cast(float)(20.0 * log10(state.output_rms));
}

void d_phaser_get_scope_data(const(PhaserDspState)* state, float* left, float* right, int size) nothrow @nogc @trusted {
    if (state is null || left is null || right is null) return;
    int count = (size < kPhaserScopeSize) ? size : kPhaserScopeSize;
    int read_pos = (state.scope_write - count + kPhaserScopeSize) % kPhaserScopeSize;
    foreach (i; 0 .. count) {
        left[i] = state.scope_l[read_pos];
        right[i] = state.scope_r[read_pos];
        read_pos = (read_pos + 1) % kPhaserScopeSize;
    }
}

void d_phaser_process(
    PhaserDspState* state,
    const(float)* in_l,
    const(float)* in_r,
    float* out_l,
    float* out_r,
    int num_samples,
    double sample_rate,
    double tempo
) nothrow @nogc @trusted {
    if (state is null) return;
    state.sample_rate = sample_rate;

    if (!state.enabled) {
        foreach (i; 0 .. num_samples) {
            out_l[i] = in_l[i];
            out_r[i] = in_r[i];
        }
        return;
    }

    float mix = clampVal(cast(float)state.params[PhaserParamId.PARAM_MIX], 0.0f, 1.0f);
    float depth = clampVal(cast(float)state.params[PhaserParamId.PARAM_DEPTH], 0.0f, 1.0f);
    float fb = clampVal(cast(float)state.params[PhaserParamId.PARAM_FEEDBACK] * 2.0f - 1.0f, -0.95f, 0.95f);
    int stages = d_phaser_norm_to_stages(state.params[PhaserParamId.PARAM_STAGES]);
    float stereo_offset = cast(float)state.params[PhaserParamId.PARAM_STEREO];
    int mode = d_phaser_norm_to_mode(state.params[PhaserParamId.PARAM_MODE]);
    int lfo_shape = clampVal(cast(int)(state.params[PhaserParamId.PARAM_LFO_SHAPE] * 3.0 + 0.5), 0, 3);
    float center = cast(float)state.params[PhaserParamId.PARAM_CENTER];
    float spread = cast(float)state.params[PhaserParamId.PARAM_SPREAD];
    bool sync = state.params[PhaserParamId.PARAM_SYNC] >= 0.5;

    float rate_hz = d_phaser_norm_to_rate(state.params[PhaserParamId.PARAM_RATE]);
    double lfo_inc;
    if (sync && tempo > 0) {
        float beats = cast(float)(0.25 * pow(64.0, state.params[PhaserParamId.PARAM_RATE]));
        double freq = tempo / (60.0 * beats);
        lfo_inc = freq / state.sample_rate;
    } else {
        lfo_inc = rate_hz / state.sample_rate;
    }

    float center_hz = cast(float)(200.0 * pow(40.0, center));

    float in_sum_sq = 0.0f, out_sum_sq = 0.0f;

    foreach (i; 0 .. num_samples) {
        float inl = in_l[i];
        float inr = in_r[i];

        float lfo_l = d_phaser_lfo(state.lfo_phase, lfo_shape);
        float lfo_r = d_phaser_lfo(state.lfo_phase + stereo_offset, lfo_shape);

        float mod_l = lfo_l * depth;
        float mod_r = lfo_r * depth;

        float wet_l = 0.0f, wet_r = 0.0f;

        switch (mode) {
            case PhaserMode.MODE_PHASER: {
                float sig_l = inl + state.feedback_l * fb;
                float sig_r = inr + state.feedback_r * fb;

                int max_s = (stages - 1 > 1) ? (stages - 1) : 1;
                foreach (s; 0 .. stages) {
                    float stage_ratio = cast(float)s / max_s;
                    float freq = cast(float)(center_hz * pow(2.0, (stage_ratio - 0.5f) * spread * 4.0f));
                    float mod_freq_l = cast(float)(freq * pow(2.0, mod_l * 2.0f));
                    float mod_freq_r = cast(float)(freq * pow(2.0, mod_r * 2.0f));

                    float tan_l = cast(float)tan(pi * mod_freq_l / state.sample_rate);
                    float tan_r = cast(float)tan(pi * mod_freq_r / state.sample_rate);
                    float coeff_l = (tan_l - 1.0f) / (tan_l + 1.0f);
                    float coeff_r = (tan_r - 1.0f) / (tan_r + 1.0f);

                    sig_l = d_phaser_allpass(sig_l, coeff_l, state.ap_state_l[s]);
                    sig_r = d_phaser_allpass(sig_r, coeff_r, state.ap_state_r[s]);
                }
                state.feedback_l = sig_l;
                state.feedback_r = sig_r;
                wet_l = sig_l;
                wet_r = sig_r;
                break;
            }

            case PhaserMode.MODE_CHORUS:
            case PhaserMode.MODE_FLANGER: {
                float max_delay = (mode == PhaserMode.MODE_CHORUS) ? 0.025f : 0.005f;
                float delay_l = max_delay * (0.5f + 0.5f * mod_l) * cast(float)state.sample_rate;
                float delay_r = max_delay * (0.5f + 0.5f * mod_r) * cast(float)state.sample_rate;
                int buf_size = kMaxCombDelay;

                state.comb_buf_l[state.comb_write] = inl + state.feedback_l * fb;
                state.comb_buf_r[state.comb_write] = inr + state.feedback_r * fb;

                float read_l = cast(float)state.comb_write - delay_l;
                float read_r = cast(float)state.comb_write - delay_r;
                while (read_l < 0) read_l += buf_size;
                while (read_r < 0) read_r += buf_size;
                int idx_l = cast(int)read_l;
                int idx_r = cast(int)read_r;
                float frac_l = read_l - idx_l;
                float frac_r = read_r - idx_r;
                idx_l = idx_l % buf_size;
                idx_r = idx_r % buf_size;
                int next_l = (idx_l + 1) % buf_size;
                int next_r = (idx_r + 1) % buf_size;

                wet_l = state.comb_buf_l[idx_l] * (1.0f - frac_l) + state.comb_buf_l[next_l] * frac_l;
                wet_r = state.comb_buf_r[idx_r] * (1.0f - frac_r) + state.comb_buf_r[next_r] * frac_r;
                state.feedback_l = wet_l;
                state.feedback_r = wet_r;
                state.comb_write = (state.comb_write + 1) % buf_size;
                break;
            }

            case PhaserMode.MODE_RINGMOD: {
                wet_l = inl * (0.5f + 0.5f * mod_l);
                wet_r = inr * (0.5f + 0.5f * mod_r);
                break;
            }

            case PhaserMode.MODE_DISPERSER: {
                float sig_l = inl;
                float sig_r = inr;
                int max_s = (stages - 1 > 1) ? (stages - 1) : 1;
                foreach (s; 0 .. stages) {
                    float stage_ratio = cast(float)s / max_s;
                    float freq = cast(float)(center_hz * pow(2.0, (stage_ratio - 0.5f) * spread * 4.0f));
                    float tan_v = cast(float)tan(pi * freq / state.sample_rate);
                    float coeff = (tan_v - 1.0f) / (tan_v + 1.0f);
                    sig_l = d_phaser_allpass(sig_l, coeff, state.ap_state_l[s]);
                    sig_r = d_phaser_allpass(sig_r, coeff, state.ap_state_r[s]);
                }
                wet_l = inl * (1.0f - depth) + sig_l * depth;
                wet_r = inr * (1.0f - depth) + sig_r * depth;
                break;
            }

            default:
                wet_l = inl;
                wet_r = inr;
                break;
        }

        float ol = inl * (1.0f - mix) + wet_l * mix;
        float or = inr * (1.0f - mix) + wet_r * mix;
        out_l[i] = ol;
        out_r[i] = or;

        in_sum_sq += inl * inl + inr * inr;
        out_sum_sq += ol * ol + or * or;

        state.scope_l[state.scope_write] = ol;
        state.scope_r[state.scope_write] = or;
        state.scope_write = (state.scope_write + 1) % kPhaserScopeSize;

        state.lfo_phase += lfo_inc;
        if (state.lfo_phase >= 1.0) state.lfo_phase -= 1.0;
    }

    float in_rms = sqrt(in_sum_sq / (2.0f * num_samples));
    float out_rms = sqrt(out_sum_sq / (2.0f * num_samples));
    enum float kSmooth = 0.15f;
    state.input_rms = state.input_rms * (1.0f - kSmooth) + in_rms * kSmooth;
    state.output_rms = state.output_rms * (1.0f - kSmooth) + out_rms * kSmooth;
}

}
