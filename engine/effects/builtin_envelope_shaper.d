module engine.effects.builtin_envelope_shaper;

import core.stdc.math : exp, fmod, pow, sqrt;
import engine.core.math : clampVal;

extern (C++, "hibiki") {

enum kMaxEnvShaperPoints = 16;
enum kTotalEnvShaperParams = 22;

enum EnvShaperParamId {
    PARAM_MIX = 0,
    PARAM_RATE = 1,
    PARAM_SMOOTH = 2,
    PARAM_MODE = 3,
    PARAM_ENABLE = 4,
    PARAM_POINT_Y_0 = 5,
    PARAM_NUM_POINTS = 21,
}

struct EnvShaperRate {
    float beats;
    const(char)* label;
}

__gshared immutable EnvShaperRate[14] kEnvShaperRateTable = [
    EnvShaperRate(0.25f,  "1/16"),
    EnvShaperRate(0.333f, "1/8 T"),
    EnvShaperRate(0.375f, "1/8 d"),
    EnvShaperRate(0.5f,   "1/8"),
    EnvShaperRate(0.667f, "1/4 T"),
    EnvShaperRate(0.75f,  "1/4 d"),
    EnvShaperRate(1.0f,   "1/4"),
    EnvShaperRate(1.333f, "1/2 T"),
    EnvShaperRate(1.5f,   "1/2 d"),
    EnvShaperRate(2.0f,   "1/2"),
    EnvShaperRate(3.0f,   "3/4"),
    EnvShaperRate(4.0f,   "1 bar"),
    EnvShaperRate(8.0f,   "2 bars"),
    EnvShaperRate(16.0f,  "4 bars"),
];

struct EnvShaperDspState {
    double[kTotalEnvShaperParams] params = 0.0;
    double sample_rate = 44100.0;
    bool enabled = true;

    double phase = 0.0;
    float lp_state_l = 0.0f;
    float lp_state_r = 0.0f;
    float hp_state_l = 0.0f;
    float hp_state_r = 0.0f;

    float input_rms = 0.0f;
    float output_rms = 0.0f;
    float current_phase = 0.0f;

    void reset() nothrow @nogc @safe {
        phase = 0.0;
        lp_state_l = 0.0f;
        lp_state_r = 0.0f;
        hp_state_l = 0.0f;
        hp_state_r = 0.0f;
        params[EnvShaperParamId.PARAM_MIX] = 1.0;
        params[EnvShaperParamId.PARAM_RATE] = 0.43;
        params[EnvShaperParamId.PARAM_SMOOTH] = 0.3;
        params[EnvShaperParamId.PARAM_MODE] = 0.0;
        params[EnvShaperParamId.PARAM_ENABLE] = 1.0;
        params[EnvShaperParamId.PARAM_NUM_POINTS] = 0.25;

        params[EnvShaperParamId.PARAM_POINT_Y_0] = 1.0;
        params[EnvShaperParamId.PARAM_POINT_Y_0 + 1] = 0.0;
        params[EnvShaperParamId.PARAM_POINT_Y_0 + 2] = 0.05;
        params[EnvShaperParamId.PARAM_POINT_Y_0 + 3] = 0.2;
        params[EnvShaperParamId.PARAM_POINT_Y_0 + 4] = 0.6;
        params[EnvShaperParamId.PARAM_POINT_Y_0 + 5] = 1.0;
        foreach (i; 6 .. kMaxEnvShaperPoints) {
            params[EnvShaperParamId.PARAM_POINT_Y_0 + i] = 1.0;
        }
        enabled = true;
    }

    int getNumPoints() const nothrow @nogc pure @safe {
        int n = cast(int)(params[EnvShaperParamId.PARAM_NUM_POINTS] * 14 + 2);
        return clampVal(n, 2, kMaxEnvShaperPoints);
    }
}

float d_env_shaper_catmull_rom(float p0, float p1, float p2, float p3, float t) nothrow @nogc pure @safe {
    float t2 = t * t;
    float t3 = t2 * t;
    return 0.5f * ((2.0f * p1) + (-p0 + p2) * t +
                   (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                   (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

float d_env_shaper_evaluate_curve(const(EnvShaperDspState)* state, float t) nothrow @nogc pure @safe {
    if (state is null) return 1.0f;
    int n = state.getNumPoints();
    if (n < 2) return 1.0f;

    float scaled = t * (n - 1);
    int seg = clampVal(cast(int)scaled, 0, n - 2);
    float frac = scaled - seg;

    float smooth = clampVal(cast(float)state.params[EnvShaperParamId.PARAM_SMOOTH], 0.0f, 1.0f);

    if (smooth < 0.01f) {
        float y0 = cast(float)state.params[EnvShaperParamId.PARAM_POINT_Y_0 + seg];
        float y1 = cast(float)state.params[EnvShaperParamId.PARAM_POINT_Y_0 + seg + 1];
        return y0 + (y1 - y0) * frac;
    }

    int i0 = (seg - 1 > 0) ? (seg - 1) : 0;
    int i1 = seg;
    int i2 = (seg + 1 < n - 1) ? (seg + 1) : (n - 1);
    int i3 = (seg + 2 < n - 1) ? (seg + 2) : (n - 1);

    float p0 = cast(float)state.params[EnvShaperParamId.PARAM_POINT_Y_0 + i0];
    float p1 = cast(float)state.params[EnvShaperParamId.PARAM_POINT_Y_0 + i1];
    float p2 = cast(float)state.params[EnvShaperParamId.PARAM_POINT_Y_0 + i2];
    float p3 = cast(float)state.params[EnvShaperParamId.PARAM_POINT_Y_0 + i3];

    float spline = d_env_shaper_catmull_rom(p0, p1, p2, p3, frac);
    float linear = p1 + (p2 - p1) * frac;

    float val = linear * (1.0f - smooth) + spline * smooth;
    return clampVal(val, 0.0f, 1.0f);
}

float d_env_shaper_norm_to_rate_beats(double norm) nothrow @nogc @safe {
    int idx = clampVal(cast(int)(norm * (kEnvShaperRateTable.length - 1) + 0.5), 0, cast(int)kEnvShaperRateTable.length - 1);
    return kEnvShaperRateTable[idx].beats;
}

const(char)* d_env_shaper_norm_to_rate_label(double norm) nothrow @nogc @safe {
    int idx = clampVal(cast(int)(norm * (kEnvShaperRateTable.length - 1) + 0.5), 0, cast(int)kEnvShaperRateTable.length - 1);
    return kEnvShaperRateTable[idx].label;
}

EnvShaperDspState* d_env_shaper_create() nothrow @nogc @trusted {
    import core.stdc.stdlib : malloc;
    auto s = cast(EnvShaperDspState*)malloc(EnvShaperDspState.sizeof);
    if (s !is null) {
        *s = EnvShaperDspState.init;
        s.reset();
    }
    return s;
}

void d_env_shaper_destroy(EnvShaperDspState* state) nothrow @nogc @trusted {
    import core.stdc.stdlib : free;
    if (state !is null) free(state);
}

void d_env_shaper_reset(EnvShaperDspState* state) nothrow @nogc @safe {
    if (state !is null) state.reset();
}

void d_env_shaper_set_parameter(EnvShaperDspState* state, uint id, double value) nothrow @nogc @safe {
    if (state !is null && id < kTotalEnvShaperParams) {
        state.params[id] = value;
        if (id == EnvShaperParamId.PARAM_ENABLE) {
            state.enabled = value >= 0.5;
        }
    }
}

double d_env_shaper_get_parameter(const(EnvShaperDspState)* state, uint id) nothrow @nogc @safe {
    if (state !is null && id < kTotalEnvShaperParams) {
        return state.params[id];
    }
    return 0.0;
}

float d_env_shaper_get_input_db(const(EnvShaperDspState)* state) nothrow @nogc @trusted {
    import core.stdc.math : log10;
    if (state is null || state.input_rms <= 0) return -100.0f;
    return cast(float)(20.0 * log10(state.input_rms));
}

float d_env_shaper_get_output_db(const(EnvShaperDspState)* state) nothrow @nogc @trusted {
    import core.stdc.math : log10;
    if (state is null || state.output_rms <= 0) return -100.0f;
    return cast(float)(20.0 * log10(state.output_rms));
}

float d_env_shaper_get_current_phase(const(EnvShaperDspState)* state) nothrow @nogc @safe {
    return (state !is null) ? state.current_phase : 0.0f;
}

void d_env_shaper_process(
    EnvShaperDspState* state,
    const(float)* in_l,
    const(float)* in_r,
    float* out_l,
    float* out_r,
    int num_samples,
    double sample_rate,
    double tempo,
    double project_time_music
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

    float mix = clampVal(cast(float)state.params[EnvShaperParamId.PARAM_MIX], 0.0f, 1.0f);
    float mode_val = clampVal(cast(float)state.params[EnvShaperParamId.PARAM_MODE], 0.0f, 1.0f);
    int filter_mode = 0;
    if (mode_val > 0.08f && mode_val < 0.25f)
        filter_mode = 1; // HPF
    else if (mode_val >= 0.25f && mode_val < 0.58f)
        filter_mode = 2; // BPF
    else if (mode_val >= 0.58f)
        filter_mode = 3; // LPF

    float rate_beats = d_env_shaper_norm_to_rate_beats(state.params[EnvShaperParamId.PARAM_RATE]);
    double cycle_samples = 1.0;
    if (tempo > 0 && state.sample_rate > 0) {
        double cycle_sec = rate_beats * 60.0 / tempo;
        cycle_samples = cycle_sec * state.sample_rate;
    }
    if (cycle_samples < 1.0) cycle_samples = 1.0;

    double phase_inc = 1.0 / cycle_samples;

    if (tempo > 0 && project_time_music >= 0) {
        if (rate_beats > 0) {
            state.phase = fmod(project_time_music / rate_beats, 1.0);
        }
    }

    float in_sum_sq = 0.0f, out_sum_sq = 0.0f;

    foreach (i; 0 .. num_samples) {
        float inl = in_l[i];
        float inr = in_r[i];

        float env = d_env_shaper_evaluate_curve(state, cast(float)state.phase);
        float ol, or;

        if (filter_mode == 0) {
            float gain = 1.0f * (1.0f - mix) + env * mix;
            ol = inl * gain;
            or = inr * gain;
        } else {
            float cutoff_hz = 80.0f * pow(250.0f, env);
            float lp_coeff = 1.0f - exp(-2.0f * 3.14159f * cutoff_hz / cast(float)state.sample_rate);

            switch (filter_mode) {
                case 1: { // HPF
                    state.lp_state_l += lp_coeff * (inl - state.lp_state_l);
                    state.lp_state_r += lp_coeff * (inr - state.lp_state_r);
                    float wet_l = inl - state.lp_state_l;
                    float wet_r = inr - state.lp_state_r;
                    ol = inl * (1.0f - mix) + wet_l * mix;
                    or = inr * (1.0f - mix) + wet_r * mix;
                    break;
                }
                case 2: { // BPF
                    state.lp_state_l += lp_coeff * (inl - state.lp_state_l);
                    state.lp_state_r += lp_coeff * (inr - state.lp_state_r);
                    state.hp_state_l += lp_coeff * (state.lp_state_l - state.hp_state_l);
                    state.hp_state_r += lp_coeff * (state.lp_state_r - state.hp_state_r);
                    float wet_l = state.lp_state_l - state.hp_state_l;
                    float wet_r = state.lp_state_r - state.hp_state_r;
                    ol = inl * (1.0f - mix) + wet_l * mix;
                    or = inr * (1.0f - mix) + wet_r * mix;
                    break;
                }
                case 3:
                default: { // LPF
                    state.lp_state_l += lp_coeff * (inl - state.lp_state_l);
                    state.lp_state_r += lp_coeff * (inr - state.lp_state_r);
                    float wet_l = state.lp_state_l;
                    float wet_r = state.lp_state_r;
                    ol = inl * (1.0f - mix) + wet_l * mix;
                    or = inr * (1.0f - mix) + wet_r * mix;
                    break;
                }
            }
        }

        out_l[i] = ol;
        out_r[i] = or;

        in_sum_sq += inl * inl + inr * inr;
        out_sum_sq += ol * ol + or * or;

        state.phase += phase_inc;
        if (state.phase >= 1.0) state.phase -= 1.0;
    }

    float in_rms = sqrt(in_sum_sq / (2.0f * num_samples));
    float out_rms = sqrt(out_sum_sq / (2.0f * num_samples));
    enum float kSmooth = 0.15f;
    state.input_rms = state.input_rms * (1.0f - kSmooth) + in_rms * kSmooth;
    state.output_rms = state.output_rms * (1.0f - kSmooth) + out_rms * kSmooth;
    state.current_phase = cast(float)state.phase;
}

}
