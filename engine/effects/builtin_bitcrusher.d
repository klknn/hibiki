module engine.effects.builtin_bitcrusher;

import core.stdc.math : fmod, pow, round;

extern (C++, "hibiki") {

/// Core DSP state for the BuiltinBitcrusher effect.
struct BitcrusherDspState {
    double phase_accumulator = 0.0;
    float held_l = 0.0f;
    float held_r = 0.0f;

    void reset() pure nothrow @nogc @safe {
        phase_accumulator = 0.0;
        held_l = 0.0f;
        held_r = 0.0f;
    }
}

pure nothrow @nogc @safe float d_bitcrusher_norm_to_bit_depth(double norm) {
    return cast(float)(1.0 + norm * 23.0);
}

nothrow @nogc @safe float d_bitcrusher_norm_to_sample_rate(double norm, double host_sample_rate) {
    return (norm >= 1.0)
        ? cast(float)host_sample_rate
        : cast(float)(20.0 * pow(host_sample_rate / 20.0, norm));
}

pure nothrow @nogc @safe float d_bitcrusher_norm_to_drive_db(double norm) {
    return cast(float)(norm * 24.0);
}

void d_bitcrusher_process(
    ref BitcrusherDspState state,
    const(float)* in_l,
    const(float)* in_r,
    float* out_l,
    float* out_r,
    int num_samples,
    double sample_rate,
    double bit_depth_norm,
    double sample_rate_norm,
    double drive_norm,
    double wet_dry_norm,
    bool enabled
) nothrow @nogc @trusted {
    if (!enabled) {
        foreach (i; 0 .. num_samples) {
            out_l[i] = in_l[i];
            out_r[i] = in_r[i];
        }
        return;
    }

    float bit_depth = d_bitcrusher_norm_to_bit_depth(bit_depth_norm);
    float target_sr = d_bitcrusher_norm_to_sample_rate(sample_rate_norm, sample_rate);
    float drive_db = d_bitcrusher_norm_to_drive_db(drive_norm);
    float drive_lin = cast(float)pow(10.0, drive_db / 20.0);
    float wet = cast(float)wet_dry_norm;
    float dry = 1.0f - wet;
    float step = cast(float)pow(2.0, bit_depth - 1.0f);

    foreach (i; 0 .. num_samples) {
        float in_sample_l = in_l[i];
        float in_sample_r = in_r[i];

        // 1. Pre-amp Drive (Input Amplification) & Hard Clipping
        float driven_l = in_sample_l * drive_lin;
        float driven_r = in_sample_r * drive_lin;
        if (driven_l > 1.0f) driven_l = 1.0f;
        else if (driven_l < -1.0f) driven_l = -1.0f;
        if (driven_r > 1.0f) driven_r = 1.0f;
        else if (driven_r < -1.0f) driven_r = -1.0f;

        // 2. Downsampling (Sample and Hold)
        state.phase_accumulator += cast(double)target_sr / sample_rate;
        if (state.phase_accumulator >= 1.0) {
            state.phase_accumulator = fmod(state.phase_accumulator, 1.0);
            state.held_l = driven_l;
            state.held_r = driven_r;
        }

        // 3. Bit Crushing (Quantization)
        float crushed_l = cast(float)(round(state.held_l * step) / step);
        float crushed_r = cast(float)(round(state.held_r * step) / step);

        // 4. Dry/Wet Mix
        out_l[i] = crushed_l * wet + in_sample_l * dry;
        out_r[i] = crushed_r * wet + in_sample_r * dry;
    }
}

} // extern (C++, "hibiki")
