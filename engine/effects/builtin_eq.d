module engine.effects.builtin_eq;

import core.stdc.math : cos, log, log10, pow, sin;
import engine.core.biquad_filter : BiquadFilter;
import engine.core.math : pi;

extern (C++, "hibiki") {

enum kNumEqBands = 8;
enum kParamsPerEqBand = 4;
enum kTotalEqParams = kNumEqBands * kParamsPerEqBand + 1;

__gshared immutable float[kNumEqBands] kDefaultEqFreqs = [
    30.0f, 80.0f, 250.0f, 700.0f, 2000.0f, 5000.0f, 10000.0f, 16000.0f
];

struct EqBandParams {
    BiquadFilter.Type type = BiquadFilter.Type.OFF;
    float freq = 1000.0f;
    float gain_db = 0.0f;
    float q = 0.707f;
}

struct EqDspState {
    EqBandParams[kNumEqBands] bands;
    BiquadFilter[2][kNumEqBands] filters;
    double[kTotalEqParams] params = 0.0;
    double sample_rate = 44100.0;
    bool enabled = true;

    void reset() nothrow @nogc @safe {
        foreach (b; 0 .. kNumEqBands) {
            bands[b].type = BiquadFilter.Type.OFF;
            bands[b].freq = kDefaultEqFreqs[b];
            bands[b].gain_db = 0.0f;
            bands[b].q = 0.707f;
            filters[b][0].reset();
            filters[b][1].reset();
        }
        foreach (b; 0 .. kNumEqBands) {
            params[b] = 0.0; // OFF
            params[b + kNumEqBands] = d_eq_freq_to_norm(kDefaultEqFreqs[b]);
            params[b + kNumEqBands * 2] = 0.5; // 0 dB
            params[b + kNumEqBands * 3] = d_eq_q_to_norm(0.707f);
        }
        params[kNumEqBands * kParamsPerEqBand] = 1.0;
        enabled = true;
    }

    void updateBand(int b) nothrow @nogc @safe {
        if (b < 0 || b >= kNumEqBands) return;
        bands[b].type = BiquadFilter.normToTypeV2(cast(float)params[b]);
        bands[b].freq = d_eq_norm_to_freq(params[b + kNumEqBands]);
        bands[b].gain_db = cast(float)(params[b + kNumEqBands * 2] - 0.5) * 48.0f;
        bands[b].q = d_eq_norm_to_q(params[b + kNumEqBands * 3]);

        float sr_f = cast(float)sample_rate;
        filters[b][0].setParams(bands[b].type, bands[b].freq, bands[b].q, bands[b].gain_db, sr_f);
        filters[b][1].setParams(bands[b].type, bands[b].freq, bands[b].q, bands[b].gain_db, sr_f);
    }

    void recalcAllCoeffs() nothrow @nogc @safe {
        foreach (b; 0 .. kNumEqBands) {
            updateBand(cast(int)b);
        }
    }
}

nothrow @nogc @trusted float d_eq_norm_to_freq(double norm) {
    return cast(float)(20.0 * pow(1000.0, norm));
}

nothrow @nogc @trusted double d_eq_freq_to_norm(float freq) {
    return log(freq / 20.0) / log(1000.0);
}

nothrow @nogc @trusted float d_eq_norm_to_q(double norm) {
    return cast(float)(0.1 * pow(180.0, norm));
}

nothrow @nogc @trusted double d_eq_q_to_norm(float q) {
    return log(q / 0.1) / log(180.0);
}

EqDspState* d_eq_create() nothrow @nogc @trusted {
    import core.stdc.stdlib : malloc;
    auto s = cast(EqDspState*)malloc(EqDspState.sizeof);
    if (s !is null) {
        *s = EqDspState.init;
        s.reset();
    }
    return s;
}

void d_eq_destroy(EqDspState* state) nothrow @nogc @trusted {
    import core.stdc.stdlib : free;
    if (state !is null) {
        free(state);
    }
}

void d_eq_reset(EqDspState* state) nothrow @nogc @safe {
    if (state !is null) {
        state.reset();
    }
}

void d_eq_set_parameter(EqDspState* state, uint id, double value) nothrow @nogc @safe {
    if (state is null || id >= kTotalEqParams) return;
    state.params[id] = value;
    if (id == kNumEqBands * kParamsPerEqBand) {
        state.enabled = value >= 0.5;
        return;
    }
    int band = id % kNumEqBands;
    state.updateBand(band);
}

double d_eq_get_parameter(const(EqDspState)* state, uint id) nothrow @nogc @safe {
    if (state is null || id >= kTotalEqParams) return 0.0;
    return state.params[id];
}

bool d_eq_is_enabled(const(EqDspState)* state) nothrow @nogc @safe {
    return state !is null && state.enabled;
}

float d_eq_get_magnitude_db(const(EqDspState)* state, float freq) nothrow @nogc @trusted {
    if (state is null || !state.enabled) return 0.0f;
    float total_db = 0.0f;
    double w = 2.0 * pi * freq / state.sample_rate;
    double cos_w = cos(w);
    double cos_2w = cos(2.0 * w);
    double sin_w = sin(w);
    double sin_2w = sin(2.0 * w);

    foreach (b; 0 .. kNumEqBands) {
        if (state.bands[b].type == BiquadFilter.Type.OFF) continue;
        double mag_sq = state.filters[b][0].getMagnitudeSq(cos_w, cos_2w, sin_w, sin_2w);
        if (mag_sq > 0.0) {
            total_db += 10.0f * log10(mag_sq);
        }
    }
    return total_db;
}

void d_eq_process(
    EqDspState* state,
    float* out_l,
    float* out_r,
    int num_samples,
    double sample_rate
) nothrow @nogc @trusted {
    if (state is null) return;
    if (state.sample_rate != sample_rate) {
        state.sample_rate = sample_rate;
        state.recalcAllCoeffs();
    }

    if (!state.enabled) return;

    foreach (b; 0 .. kNumEqBands) {
        if (state.bands[b].type == BiquadFilter.Type.OFF) continue;

        foreach (i; 0 .. num_samples) {
            out_l[i] = state.filters[b][0].process(out_l[i]);
            out_r[i] = state.filters[b][1].process(out_r[i]);
        }
    }
}

}
