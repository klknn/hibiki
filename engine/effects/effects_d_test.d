module engine.effects.effects_d_test;

import core.math : fabs;
import engine.effects.builtin_bitcrusher;
import engine.effects.builtin_chorus;
import engine.effects.builtin_compressor;
import engine.effects.builtin_delay;
import engine.effects.builtin_limiter;
import engine.effects.builtin_stereo_width;

@nogc nothrow unittest {
    // 1. Bitcrusher
    BitcrusherDspState bc;
    bc.reset();
    float[8] in_l = [0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f];
    float[8] in_r = [0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f];
    float[8] out_l, out_r;

    // Disabled: pure bypass
    d_bitcrusher_process(bc, in_l.ptr, in_r.ptr, out_l.ptr, out_r.ptr, 8, 48000.0, 0.0, 0.0, 1.0, 0.0, false);
    assert(out_l[0] == in_l[0]);

    // Enabled with 4-bit crush
    d_bitcrusher_process(bc, in_l.ptr, in_r.ptr, out_l.ptr, out_r.ptr, 8, 48000.0, 0.0, 0.75, 1.0, 0.0, true);
    assert(out_l[7] > 0.0f);

    // 2. Stereo Width
    StereoWidthDspState sw;
    sw.reset();
    float[4] sin_l = [1.0f, 0.0f, -0.5f, 0.2f];
    float[4] sin_r = [0.0f, 1.0f, 0.5f, -0.2f];
    float[4] sout_l, sout_r;

    // Width = 0.0 -> mono: L and R should be identical (Mid only)
    d_stereo_width_process(sw, sin_l.ptr, sin_r.ptr, sout_l.ptr, sout_r.ptr, 4, 48000.0, 0.0, 0.0, 0.0, 0.0, true);
    assert(fabs(sout_l[0] - sout_r[0]) < 1e-5f);
    assert(fabs(sout_l[1] - sout_r[1]) < 1e-5f);

    // 3. Limiter
    LimiterDspState lim;
    lim.reset();
    assert(d_limiter_norm_to_ceiling_db(1.0) == 0.0f);
    assert(d_limiter_norm_to_ceiling_db(0.0) == -12.0f);

    float[128] loud_in_l = 2.0f; // Over 0 dB
    float[128] loud_in_r = 2.0f;
    float[128] lout_l, lout_r;
    d_limiter_process(lim, loud_in_l.ptr, loud_in_r.ptr, lout_l.ptr, lout_r.ptr, 128, 48000.0, 1.0, 0.3, 0.2, 0.0, true, true);
    // After lookahead, output must be clamped below ceiling (1.0 lin)
    foreach (s; lout_l[64 .. $]) {
        assert(s <= 1.05f);
    }
    assert(lim.gain_reduction_db < -0.1f);

    // 4. Compressor transfer curve
    float gr0 = d_compressor_compute_gain_reduction(-10.0f, -20.0f, 4.0f, 0.0f, -60.0f, 1.0f);
    // Above threshold -20 by 10dB, ratio 4:1 -> reduction is -10 * (1 - 1/4) = -7.5dB
    assert(fabs(gr0 - (-7.5f)) < 1e-4f);

    // Below threshold -> 0 dB reduction
    float gr1 = d_compressor_compute_gain_reduction(-30.0f, -20.0f, 4.0f, 0.0f, -60.0f, 1.0f);
    assert(gr1 == 0.0f);

    // 5. Delay sync lookup table
    assert(d_delay_norm_to_division_beats(0.0) == 0.08333f);
    assert(d_delay_norm_to_division_beats(1.0) == 4.0f);
}
