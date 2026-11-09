module engine.effects.builtin_delay;

import core.stdc.math : exp, log10, pow, sqrt;
import engine.core.math : pi;

extern (C++, "hibiki") {

enum kMaxDelaySamples = 384000; // 2s @ 192kHz

struct DelayDivision {
    float beats;
    const(char)* label;
}

__gshared immutable DelayDivision[16] kDelayDivisions = [
    DelayDivision(0.08333f, "1/32 T"),
    DelayDivision(0.125f,   "1/32"),
    DelayDivision(0.16667f, "1/16 T"),
    DelayDivision(0.1875f,  "1/32 D"),
    DelayDivision(0.25f,    "1/16"),
    DelayDivision(0.33333f, "1/8 T"),
    DelayDivision(0.375f,   "1/16 D"),
    DelayDivision(0.5f,     "1/8"),
    DelayDivision(0.66667f, "1/4 T"),
    DelayDivision(0.75f,    "1/8 D"),
    DelayDivision(1.0f,     "1/4"),
    DelayDivision(1.33333f, "1/2 T"),
    DelayDivision(1.5f,     "1/4 D"),
    DelayDivision(2.0f,     "1/2"),
    DelayDivision(3.0f,     "1/2 D"),
    DelayDivision(4.0f,     "1 bar")
];

struct DelayDspState {
    int write_pos = 0;
    float hp_state_l = 0.0f;
    float hp_state_r = 0.0f;
    float lp_state_l = 0.0f;
    float lp_state_r = 0.0f;
    float input_rms = 0.0f;
    float output_rms = 0.0f;

    void reset() pure nothrow @nogc @safe {
        write_pos = 0;
        hp_state_l = 0.0f;
        hp_state_r = 0.0f;
        lp_state_l = 0.0f;
        lp_state_r = 0.0f;
        input_rms = 0.0f;
        output_rms = 0.0f;
    }
}

nothrow @nogc @trusted float d_delay_norm_to_time_ms(double norm) {
    return 1.0f * pow(2000.0f, cast(float)norm);
}

nothrow @nogc @trusted float d_delay_norm_to_hp_freq(double norm) {
    return 20.0f * pow(100.0f, cast(float)norm);
}

nothrow @nogc @trusted float d_delay_norm_to_lp_freq(double norm) {
    return 1000.0f * pow(20.0f, cast(float)norm);
}

nothrow @nogc @safe float d_delay_norm_to_division_beats(double norm) {
    int idx = cast(int)(norm * 15.0 + 0.5);
    if (idx < 0) idx = 0;
    if (idx > 15) idx = 15;
    return kDelayDivisions[idx].beats;
}

nothrow @nogc @safe const(char)* d_delay_norm_to_division_label(double norm) {
    int idx = cast(int)(norm * 15.0 + 0.5);
    if (idx < 0) idx = 0;
    if (idx > 15) idx = 15;
    return kDelayDivisions[idx].label;
}

void d_delay_process(
    ref DelayDspState state,
    float* buf_l,
    float* buf_r,
    int buf_size,
    const(float)* in_l,
    const(float)* in_r,
    float* out_l,
    float* out_r,
    int num_samples,
    double sample_rate,
    double tempo,
    double time_l_norm,
    double time_r_norm,
    double feedback_norm,
    double mix_norm,
    double hp_norm,
    double lp_norm,
    double ping_pong_norm,
    bool sync,
    bool enabled
) nothrow @nogc @trusted {
    if (!enabled) {
        foreach (i; 0 .. num_samples) {
            out_l[i] = in_l[i];
            out_r[i] = in_r[i];
        }
        return;
    }

    float time_l_ms, time_r_ms;
    if (sync && tempo > 0) {
        float div_l = d_delay_norm_to_division_beats(time_l_norm);
        float div_r = d_delay_norm_to_division_beats(time_r_norm);
        time_l_ms = cast(float)(div_l * 60000.0 / tempo);
        time_r_ms = cast(float)(div_r * 60000.0 / tempo);
    } else {
        time_l_ms = d_delay_norm_to_time_ms(time_l_norm);
        time_r_ms = d_delay_norm_to_time_ms(time_r_norm);
    }

    float sample_rate_f = cast(float)sample_rate;
    int delay_l = cast(int)(time_l_ms * 0.001f * sample_rate_f);
    if (delay_l < 1) delay_l = 1;
    if (delay_l > buf_size - 1) delay_l = buf_size - 1;

    int delay_r = cast(int)(time_r_ms * 0.001f * sample_rate_f);
    if (delay_r < 1) delay_r = 1;
    if (delay_r > buf_size - 1) delay_r = buf_size - 1;

    float feedback = cast(float)feedback_norm;
    if (feedback < 0.0f) feedback = 0.0f;
    if (feedback > 0.95f) feedback = 0.95f;

    float mix = cast(float)mix_norm;
    if (mix < 0.0f) mix = 0.0f;
    if (mix > 1.0f) mix = 1.0f;

    float cross_talk = cast(float)ping_pong_norm;
    if (cross_talk < 0.0f) cross_talk = 0.0f;
    if (cross_talk > 1.0f) cross_talk = 1.0f;

    float hp_freq = d_delay_norm_to_hp_freq(hp_norm);
    float lp_freq = d_delay_norm_to_lp_freq(lp_norm);
    float hp_coeff = 1.0f - exp(-2.0f * cast(float)pi * hp_freq / sample_rate_f);
    float lp_coeff = 1.0f - exp(-2.0f * cast(float)pi * lp_freq / sample_rate_f);

    float in_sum_sq = 0.0f;
    float out_sum_sq = 0.0f;

    foreach (i; 0 .. num_samples) {
        float sample_in_l = in_l[i];
        float sample_in_r = in_r[i];

        int read_l = (state.write_pos - delay_l + buf_size) % buf_size;
        int read_r = (state.write_pos - delay_r + buf_size) % buf_size;
        float del_l = buf_l[read_l];
        float del_r = buf_r[read_r];

        state.hp_state_l += hp_coeff * (del_l - state.hp_state_l);
        float filtered_l = del_l - state.hp_state_l;
        state.hp_state_r += hp_coeff * (del_r - state.hp_state_r);
        float filtered_r = del_r - state.hp_state_r;

        state.lp_state_l += lp_coeff * (filtered_l - state.lp_state_l);
        filtered_l = state.lp_state_l;
        state.lp_state_r += lp_coeff * (filtered_r - state.lp_state_r);
        filtered_r = state.lp_state_r;

        float fb_l = filtered_l * (1.0f - cross_talk) + filtered_r * cross_talk;
        float fb_r = filtered_r * (1.0f - cross_talk) + filtered_l * cross_talk;
        buf_l[state.write_pos] = sample_in_l + fb_l * feedback;
        buf_r[state.write_pos] = sample_in_r + fb_r * feedback;

        float sample_out_l = sample_in_l * (1.0f - mix) + del_l * mix;
        float sample_out_r = sample_in_r * (1.0f - mix) + del_r * mix;
        out_l[i] = sample_out_l;
        out_r[i] = sample_out_r;

        in_sum_sq += sample_in_l * sample_in_l + sample_in_r * sample_in_r;
        out_sum_sq += sample_out_l * sample_out_l + sample_out_r * sample_out_r;

        state.write_pos = (state.write_pos + 1) % buf_size;
    }

    float alpha = 0.1f;
    float cur_in_rms = sqrt(in_sum_sq / (num_samples * 2 + 1e-10f));
    float cur_out_rms = sqrt(out_sum_sq / (num_samples * 2 + 1e-10f));
    state.input_rms = state.input_rms * (1.0f - alpha) + cur_in_rms * alpha;
    state.output_rms = state.output_rms * (1.0f - alpha) + cur_out_rms * alpha;
}

}
