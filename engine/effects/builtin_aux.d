module engine.effects.builtin_aux;

import core.stdc.math : cos, log10, pow, sin;
import engine.core.math : clampVal;

extern (C++, "hibiki") {

enum kTotalAuxParams = 2;

enum AuxParamId {
    PARAM_GAIN = 0,
    PARAM_PAN = 1,
}

__gshared immutable double[kTotalAuxParams] kDefaultAuxParams = [
    0.7, // GAIN (~0 dB)
    0.5, // PAN (center)
];

struct AuxDspState {
    double[kTotalAuxParams] params = 0.0;
    double sample_rate = 44100.0;

    void reset() nothrow @nogc @safe {
        foreach (i; 0 .. kTotalAuxParams) {
            params[i] = kDefaultAuxParams[i];
        }
    }
}

AuxDspState* d_aux_create() nothrow @nogc @trusted {
    import core.stdc.stdlib : malloc;
    auto s = cast(AuxDspState*)malloc(AuxDspState.sizeof);
    if (s !is null) {
        *s = AuxDspState.init;
        s.reset();
    }
    return s;
}

void d_aux_destroy(AuxDspState* state) nothrow @nogc @trusted {
    import core.stdc.stdlib : free;
    if (state !is null) free(state);
}

void d_aux_reset(AuxDspState* state) nothrow @nogc @safe {
    if (state !is null) state.reset();
}

void d_aux_set_parameter(AuxDspState* state, uint id, double value) nothrow @nogc @safe {
    if (state !is null && id < kTotalAuxParams) {
        state.params[id] = clampVal(value, 0.0, 1.0);
    }
}

double d_aux_get_parameter(const AuxDspState* state, uint id) nothrow @nogc @safe {
    if (state !is null && id < kTotalAuxParams) {
        return state.params[id];
    }
    return 0.0;
}

void d_aux_process(
    AuxDspState* state,
    const(float)* in_l,
    const(float)* in_r,
    float* out_l,
    float* out_r,
    int num_samples
) nothrow @nogc @trusted {
    if (state is null) return;

    float gain_norm = clampVal(cast(float)state.params[AuxParamId.PARAM_GAIN], 0.0f, 1.0f);
    float gain_db;
    if (gain_norm <= 0.001f) {
        gain_db = -100.0f;
    } else {
        gain_db = 20.0f * log10(gain_norm / 0.7f) * 2.0f;
        gain_db = clampVal(gain_db, -60.0f, 6.0f);
    }
    float gain_linear = pow(10.0f, gain_db / 20.0f);

    float pan = clampVal(cast(float)state.params[AuxParamId.PARAM_PAN], 0.0f, 1.0f);
    float pan_angle = pan * 1.5707963f;
    float pan_l = cos(pan_angle);
    float pan_r = sin(pan_angle);

    float coeff_l = gain_linear * pan_l;
    float coeff_r = gain_linear * pan_r;

    foreach (i; 0 .. num_samples) {
        out_l[i] = (in_l !is null ? in_l[i] : out_l[i]) * coeff_l;
        out_r[i] = (in_r !is null ? in_r[i] : out_r[i]) * coeff_r;
    }
}

}
