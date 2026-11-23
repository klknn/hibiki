module engine.instruments.builtin_dr8_snare;

import core.stdc.math : exp, pow, sin;
import engine.core.math : clampVal, pi;

extern (C++, "hibiki") {

enum kTotalDr8SnareParams = 7;

enum Dr8SnareParamId {
    PARAM_PITCH = 0,
    PARAM_DECAY = 1,
    PARAM_NOISE_LEVEL = 2,
    PARAM_NOISE_DECAY = 3,
    PARAM_NOISE_HPF = 4,
    PARAM_TONE_NOISE_MIX = 5,
    PARAM_VOLUME = 6,
}

__gshared immutable double[kTotalDr8SnareParams] kDefaultDr8SnareParams = [
    0.333333, // PITCH
    0.3,      // DECAY
    0.5,      // NOISE_LEVEL
    0.4,      // NOISE_DECAY
    0.4,      // NOISE_HPF
    0.5,      // TONE_NOISE_MIX
    0.7,      // VOLUME
];

struct Dr8SnareMidiEvent {
    int pitch;
    float velocity;
    bool is_note_on;
}

struct Dr8SnareDspState {
    double[kTotalDr8SnareParams] params = 0.0;
    double sample_rate = 44100.0;

    bool active = false;
    double phase1 = 0.0;
    double phase2 = 0.0;
    double env_tone = 0.0;
    double env_noise = 0.0;
    float velocity = 0.0f;
    double noise_lp_state = 0.0;
    uint rng_state = 0x12345678;

    void reset() nothrow @nogc @safe {
        active = false;
        phase1 = 0.0;
        phase2 = 0.0;
        env_tone = 0.0;
        env_noise = 0.0;
        velocity = 0.0f;
        noise_lp_state = 0.0;
        rng_state = 0x12345678;
        foreach (i; 0 .. kTotalDr8SnareParams) {
            params[i] = kDefaultDr8SnareParams[i];
        }
    }

    float nextRandomFloat() nothrow @nogc @safe {
        rng_state ^= (rng_state << 13);
        rng_state ^= (rng_state >> 17);
        rng_state ^= (rng_state << 5);
        return (cast(float)(rng_state & 0x7FFFFFFF) / 1073741824.0f) - 1.0f;
    }
}

nothrow @nogc @safe float d_dr8_snare_norm_to_pitch_hz(double norm) {
    return cast(float)(100.0 + norm * 150.0);
}

nothrow @nogc @trusted float d_dr8_snare_norm_to_decay_s(double norm) {
    return cast(float)(0.05 * pow(10.0, norm));
}

nothrow @nogc @trusted float d_dr8_snare_norm_to_noise_decay_s(double norm) {
    return cast(float)(0.05 * pow(20.0, norm));
}

nothrow @nogc @trusted float d_dr8_snare_norm_to_noise_hpf_hz(double norm) {
    return cast(float)(800.0 * pow(10.0, norm));
}

Dr8SnareDspState* d_dr8_snare_create() nothrow @nogc @trusted {
    import core.stdc.stdlib : malloc;
    auto s = cast(Dr8SnareDspState*)malloc(Dr8SnareDspState.sizeof);
    if (s !is null) {
        *s = Dr8SnareDspState.init;
        s.reset();
    }
    return s;
}

void d_dr8_snare_destroy(Dr8SnareDspState* state) nothrow @nogc @trusted {
    import core.stdc.stdlib : free;
    if (state !is null) free(state);
}

void d_dr8_snare_reset(Dr8SnareDspState* state) nothrow @nogc @safe {
    if (state !is null) state.reset();
}

void d_dr8_snare_set_parameter(Dr8SnareDspState* state, uint id, double value) nothrow @nogc @safe {
    if (state !is null && id < kTotalDr8SnareParams) {
        state.params[id] = value;
    }
}

double d_dr8_snare_get_parameter(const(Dr8SnareDspState)* state, uint id) nothrow @nogc @safe {
    if (state !is null && id < kTotalDr8SnareParams) {
        return state.params[id];
    }
    return 0.0;
}

void d_dr8_snare_process(
    Dr8SnareDspState* state,
    float* out_l,
    float* out_r,
    int num_samples,
    double sample_rate,
    const(Dr8SnareMidiEvent)* events,
    int num_events
) nothrow @nogc @trusted {
    if (state is null) return;
    state.sample_rate = sample_rate;

    if (events !is null) {
        foreach (i; 0 .. num_events) {
            if (events[i].is_note_on && events[i].velocity > 0) {
                state.active = true;
                state.phase1 = 0.0;
                state.phase2 = 0.0;
                state.env_tone = 1.0;
                state.env_noise = 1.0;
                state.velocity = events[i].velocity;
            }
        }
    }

    foreach (i; 0 .. num_samples) {
        out_l[i] = 0.0f;
        out_r[i] = 0.0f;
    }

    if (!state.active) return;

    double f1 = d_dr8_snare_norm_to_pitch_hz(state.params[Dr8SnareParamId.PARAM_PITCH]);
    double f2 = f1 * 1.6;
    double decay_s = d_dr8_snare_norm_to_decay_s(state.params[Dr8SnareParamId.PARAM_DECAY]);
    double noise_level = state.params[Dr8SnareParamId.PARAM_NOISE_LEVEL];
    double noise_decay_s = d_dr8_snare_norm_to_noise_decay_s(state.params[Dr8SnareParamId.PARAM_NOISE_DECAY]);
    double noise_hpf_fc = d_dr8_snare_norm_to_noise_hpf_hz(state.params[Dr8SnareParamId.PARAM_NOISE_HPF]);
    double mix = state.params[Dr8SnareParamId.PARAM_TONE_NOISE_MIX];
    double volume = state.params[Dr8SnareParamId.PARAM_VOLUME];

    double tone_decay_coeff = exp(-1.0 / (decay_s * state.sample_rate));
    double noise_decay_coeff = exp(-1.0 / (noise_decay_s * state.sample_rate));

    double filter_theta = 2.0 * pi * noise_hpf_fc / state.sample_rate;
    double filter_alpha = 1.0 - exp(-filter_theta);
    filter_alpha = clampVal(filter_alpha, 0.0, 1.0);

    double dt1 = f1 / state.sample_rate;
    double dt2 = f2 / state.sample_rate;

    foreach (i; 0 .. num_samples) {
        if (state.env_tone < 0.0001 && state.env_noise < 0.0001) {
            state.active = false;
            state.reset();
            break;
        }

        state.phase1 += dt1;
        if (state.phase1 >= 1.0) state.phase1 -= 1.0;
        state.phase2 += dt2;
        if (state.phase2 >= 1.0) state.phase2 -= 1.0;

        double tone_out = (sin(2.0 * pi * state.phase1) + 0.4 * sin(2.0 * pi * state.phase2)) * state.env_tone;
        double noise = state.nextRandomFloat();
        state.noise_lp_state += filter_alpha * (noise - state.noise_lp_state);
        double filtered_noise = noise - state.noise_lp_state;
        double noise_out = filtered_noise * state.env_noise * noise_level;

        double mix_val = (1.0 - mix) * tone_out + mix * noise_out;
        float sample = cast(float)(mix_val * state.velocity * volume);

        out_l[i] = sample;
        out_r[i] = sample;

        state.env_tone *= tone_decay_coeff;
        state.env_noise *= noise_decay_coeff;
    }
}

}
