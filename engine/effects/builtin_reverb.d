module engine.effects.builtin_reverb;

import core.stdc.math : exp, log10, pow, sqrt;
import core.stdc.stdlib : free, malloc;
import engine.core.math : clampVal;

extern (C++, "hibiki") {

enum kTotalReverbParams = 8;
enum kNumCombs = 8;
enum kNumAllpasses = 4;

enum ReverbParamId {
    PARAM_ROOM_SIZE = 0,
    PARAM_DAMPING = 1,
    PARAM_MIX = 2,
    PARAM_PRE_DELAY = 3,
    PARAM_HP_FREQ = 4,
    PARAM_LP_FREQ = 5,
    PARAM_WIDTH = 6,
    PARAM_ENABLE = 7,
}

__gshared immutable double[kTotalReverbParams] kDefaultReverbParams = [
    0.5, 0.5, 0.3, 0.1, 0.15, 0.85, 1.0, 1.0
];

__gshared immutable int[8] kCombLengths = [1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617];
enum int kStereoSpread = 23;
__gshared immutable int[4] kAllpassLengths = [556, 441, 341, 225];
enum float kAllpassFeedback = 0.5f;

enum float kRoomScaleMin = 0.7f;
enum float kRoomScaleRange = 0.28f;
enum float kDampScaleMax = 0.4f;
enum float kFixedGain = 0.015f;

struct CombFilter {
    float* buffer = null;
    int size = 0;
    int pos = 0;
    float feedback = 0.0f;
    float damp1 = 0.0f;
    float damp2 = 0.0f;
    float filter_store = 0.0f;

    void freeBuf() nothrow @nogc @trusted {
        if (buffer !is null) {
            free(buffer);
            buffer = null;
        }
        size = 0;
        pos = 0;
        filter_store = 0.0f;
    }

    void allocBuf(int new_size) nothrow @nogc @trusted {
        freeBuf();
        size = new_size;
        buffer = cast(float*)malloc(new_size * float.sizeof);
        if (buffer !is null) {
            foreach (i; 0 .. new_size) buffer[i] = 0.0f;
        }
    }

    void clear() nothrow @nogc @trusted {
        if (buffer !is null) {
            foreach (i; 0 .. size) buffer[i] = 0.0f;
        }
        pos = 0;
        filter_store = 0.0f;
    }

    float process(float input) nothrow @nogc @trusted {
        if (buffer is null || size == 0) return input;
        float output = buffer[pos];
        filter_store = output * damp2 + filter_store * damp1;
        buffer[pos] = input + filter_store * feedback;
        if (++pos >= size) pos = 0;
        return output;
    }
}

struct AllpassFilter {
    float* buffer = null;
    int size = 0;
    int pos = 0;

    void freeBuf() nothrow @nogc @trusted {
        if (buffer !is null) {
            free(buffer);
            buffer = null;
        }
        size = 0;
        pos = 0;
    }

    void allocBuf(int new_size) nothrow @nogc @trusted {
        freeBuf();
        size = new_size;
        buffer = cast(float*)malloc(new_size * float.sizeof);
        if (buffer !is null) {
            foreach (i; 0 .. new_size) buffer[i] = 0.0f;
        }
    }

    void clear() nothrow @nogc @trusted {
        if (buffer !is null) {
            foreach (i; 0 .. size) buffer[i] = 0.0f;
        }
        pos = 0;
    }

    float process(float input) nothrow @nogc @trusted {
        if (buffer is null || size == 0) return input;
        float buffered = buffer[pos];
        float output = -input + buffered;
        buffer[pos] = input + buffered * kAllpassFeedback;
        if (++pos >= size) pos = 0;
        return output;
    }
}

pure nothrow @nogc @safe float d_reverb_norm_to_predelay_ms(double norm) {
    return cast(float)(norm * 100.0);
}

struct ReverbDspState {
    double[kTotalReverbParams] params = 0.0;
    double sample_rate = 44100.0;
    bool enabled = true;

    CombFilter[kNumCombs] combs_l;
    CombFilter[kNumCombs] combs_r;
    AllpassFilter[kNumAllpasses] allpasses_l;
    AllpassFilter[kNumAllpasses] allpasses_r;

    float* predelay_l = null;
    float* predelay_r = null;
    int predelay_size = 0;
    int predelay_pos = 0;

    float hp_state_l = 0.0f;
    float hp_state_r = 0.0f;
    float lp_state_l = 0.0f;
    float lp_state_r = 0.0f;

    float input_rms = 0.0f;
    float output_rms = 0.0f;

    void freeBuffers() nothrow @nogc @trusted {
        foreach (i; 0 .. kNumCombs) {
            combs_l[i].freeBuf();
            combs_r[i].freeBuf();
        }
        foreach (i; 0 .. kNumAllpasses) {
            allpasses_l[i].freeBuf();
            allpasses_r[i].freeBuf();
        }
        if (predelay_l !is null) {
            free(predelay_l);
            predelay_l = null;
        }
        if (predelay_r !is null) {
            free(predelay_r);
            predelay_r = null;
        }
        predelay_size = 0;
    }

    void initBuffers(double sr) nothrow @nogc @trusted {
        freeBuffers();
        sample_rate = sr;
        double scale = sample_rate / 44100.0;
        foreach (i; 0 .. kNumCombs) {
            int len_l = cast(int)(kCombLengths[i] * scale);
            int len_r = cast(int)((kCombLengths[i] + kStereoSpread) * scale);
            combs_l[i].allocBuf(len_l);
            combs_r[i].allocBuf(len_r);
        }
        foreach (i; 0 .. kNumAllpasses) {
            int len_l = cast(int)(kAllpassLengths[i] * scale);
            int len_r = cast(int)((kAllpassLengths[i] + kStereoSpread) * scale);
            allpasses_l[i].allocBuf(len_l);
            allpasses_r[i].allocBuf(len_r);
        }
        predelay_size = cast(int)(0.1 * sample_rate) + 1;
        predelay_l = cast(float*)malloc(predelay_size * float.sizeof);
        predelay_r = cast(float*)malloc(predelay_size * float.sizeof);
        if (predelay_l !is null) {
            foreach (i; 0 .. predelay_size) predelay_l[i] = 0.0f;
        }
        if (predelay_r !is null) {
            foreach (i; 0 .. predelay_size) predelay_r[i] = 0.0f;
        }
    }

    void reset() nothrow @nogc @trusted {
        foreach (i; 0 .. kNumCombs) {
            combs_l[i].clear();
            combs_r[i].clear();
        }
        foreach (i; 0 .. kNumAllpasses) {
            allpasses_l[i].clear();
            allpasses_r[i].clear();
        }
        if (predelay_l !is null) {
            foreach (i; 0 .. predelay_size) predelay_l[i] = 0.0f;
        }
        if (predelay_r !is null) {
            foreach (i; 0 .. predelay_size) predelay_r[i] = 0.0f;
        }
        predelay_pos = 0;
        hp_state_l = 0.0f;
        hp_state_r = 0.0f;
        lp_state_l = 0.0f;
        lp_state_r = 0.0f;
        foreach (i; 0 .. kTotalReverbParams) {
            params[i] = kDefaultReverbParams[i];
        }
        enabled = true;
    }

    void updateParams() nothrow @nogc @safe {
        float room = cast(float)params[ReverbParamId.PARAM_ROOM_SIZE];
        float damp = cast(float)params[ReverbParamId.PARAM_DAMPING];

        float feedback = kRoomScaleMin + room * kRoomScaleRange;
        float damp1 = damp * kDampScaleMax;
        float damp2 = 1.0f - damp1;

        foreach (i; 0 .. kNumCombs) {
            combs_l[i].feedback = feedback;
            combs_l[i].damp1 = damp1;
            combs_l[i].damp2 = damp2;
            combs_r[i].feedback = feedback;
            combs_r[i].damp1 = damp1;
            combs_r[i].damp2 = damp2;
        }
    }
}

ReverbDspState* d_reverb_create(double sample_rate) nothrow @nogc @trusted {
    auto s = cast(ReverbDspState*)malloc(ReverbDspState.sizeof);
    if (s !is null) {
        *s = ReverbDspState.init;
        s.initBuffers(sample_rate);
        s.reset();
    }
    return s;
}

void d_reverb_destroy(ReverbDspState* state) nothrow @nogc @trusted {
    if (state !is null) {
        state.freeBuffers();
        free(state);
    }
}

void d_reverb_reset(ReverbDspState* state) nothrow @nogc @trusted {
    if (state !is null) state.reset();
}

void d_reverb_init_buffers(ReverbDspState* state, double sample_rate) nothrow @nogc @trusted {
    if (state !is null) {
        state.initBuffers(sample_rate);
        state.reset();
    }
}

void d_reverb_set_parameter(ReverbDspState* state, uint id, double value) nothrow @nogc @safe {
    if (state !is null && id < kTotalReverbParams) {
        state.params[id] = value;
        if (id == ReverbParamId.PARAM_ENABLE) {
            state.enabled = (value >= 0.5);
        }
    }
}

double d_reverb_get_parameter(const ReverbDspState* state, uint id) nothrow @nogc @safe {
    if (state !is null && id < kTotalReverbParams) {
        return state.params[id];
    }
    return 0.0;
}

float d_reverb_get_input_db(const ReverbDspState* state) nothrow @nogc @trusted {
    if (state is null || state.input_rms <= 0.0f) return -100.0f;
    return 20.0f * log10(state.input_rms);
}

float d_reverb_get_output_db(const ReverbDspState* state) nothrow @nogc @trusted {
    if (state is null || state.output_rms <= 0.0f) return -100.0f;
    return 20.0f * log10(state.output_rms);
}

void d_reverb_process(
    ReverbDspState* state,
    const(float)* in_l,
    const(float)* in_r,
    float* out_l,
    float* out_r,
    int num_samples
) nothrow @nogc @trusted {
    if (state is null) return;

    if (!state.enabled) {
        foreach (i; 0 .. num_samples) {
            out_l[i] = (in_l !is null) ? in_l[i] : out_l[i];
            out_r[i] = (in_r !is null) ? in_r[i] : out_r[i];
        }
        return;
    }

    state.updateParams();

    float mix = clampVal(cast(float)state.params[ReverbParamId.PARAM_MIX], 0.0f, 1.0f);
    float width = clampVal(cast(float)state.params[ReverbParamId.PARAM_WIDTH], 0.0f, 1.0f);
    float wet1 = width * 0.5f + 0.5f;
    float wet2 = (1.0f - width) * 0.5f;

    float pre_delay_ms = d_reverb_norm_to_predelay_ms(state.params[ReverbParamId.PARAM_PRE_DELAY]);
    int pre_delay_samples = clampVal(
        cast(int)(pre_delay_ms * 0.001f * cast(float)state.sample_rate),
        0,
        state.predelay_size - 1
    );

    float hp_freq = 20.0f * pow(25.0f, cast(float)state.params[ReverbParamId.PARAM_HP_FREQ]);
    float lp_freq = 2000.0f * pow(10.0f, cast(float)state.params[ReverbParamId.PARAM_LP_FREQ]);
    float hp_coeff = 1.0f - exp(-2.0f * 3.14159f * hp_freq / cast(float)state.sample_rate);
    float lp_coeff = 1.0f - exp(-2.0f * 3.14159f * lp_freq / cast(float)state.sample_rate);

    int pd_size = state.predelay_size;
    float in_sum_sq = 0.0f, out_sum_sq = 0.0f;

    foreach (i; 0 .. num_samples) {
        float inl = (in_l !is null) ? in_l[i] : out_l[i];
        float inr = (in_r !is null) ? in_r[i] : out_r[i];

        if (state.predelay_l !is null && state.predelay_r !is null && pd_size > 0) {
            state.predelay_l[state.predelay_pos] = inl;
            state.predelay_r[state.predelay_pos] = inr;
            int pd_read = (state.predelay_pos - pre_delay_samples + pd_size) % pd_size;
            float pd_l = state.predelay_l[pd_read];
            float pd_r = state.predelay_r[pd_read];
            state.predelay_pos = (state.predelay_pos + 1) % pd_size;

            float input_mixed = (pd_l + pd_r) * kFixedGain;

            float comb_out_l = 0.0f;
            float comb_out_r = 0.0f;
            foreach (j; 0 .. kNumCombs) {
                comb_out_l += state.combs_l[j].process(input_mixed);
                comb_out_r += state.combs_r[j].process(input_mixed);
            }

            foreach (j; 0 .. kNumAllpasses) {
                comb_out_l = state.allpasses_l[j].process(comb_out_l);
                comb_out_r = state.allpasses_r[j].process(comb_out_r);
            }

            float wet_l = comb_out_l * wet1 + comb_out_r * wet2;
            float wet_r = comb_out_r * wet1 + comb_out_l * wet2;

            state.hp_state_l += hp_coeff * (wet_l - state.hp_state_l);
            wet_l -= state.hp_state_l;
            state.hp_state_r += hp_coeff * (wet_r - state.hp_state_r);
            wet_r -= state.hp_state_r;

            state.lp_state_l += lp_coeff * (wet_l - state.lp_state_l);
            wet_l = state.lp_state_l;
            state.lp_state_r += lp_coeff * (wet_r - state.lp_state_r);
            wet_r = state.lp_state_r;

            float final_l = inl * (1.0f - mix) + wet_l * mix;
            float final_r = inr * (1.0f - mix) + wet_r * mix;
            out_l[i] = final_l;
            out_r[i] = final_r;

            in_sum_sq += inl * inl + inr * inr;
            out_sum_sq += final_l * final_l + final_r * final_r;
        } else {
            out_l[i] = inl;
            out_r[i] = inr;
        }
    }

    if (num_samples > 0) {
        float in_rms = sqrt(in_sum_sq / (2.0f * num_samples));
        float out_rms = sqrt(out_sum_sq / (2.0f * num_samples));
        enum float kSmooth = 0.15f;
        state.input_rms = state.input_rms * (1.0f - kSmooth) + in_rms * kSmooth;
        state.output_rms = state.output_rms * (1.0f - kSmooth) + out_rms * kSmooth;
    }
}

}
