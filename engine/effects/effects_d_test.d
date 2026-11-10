module engine.effects.effects_d_test;

import core.math : fabs;
import engine.effects.builtin_bitcrusher;
import engine.effects.builtin_chorus;
import engine.effects.builtin_compressor;
import engine.effects.builtin_delay;
import engine.effects.builtin_envelope_shaper;
import engine.effects.builtin_eq;
import engine.effects.builtin_limiter;
import engine.effects.builtin_phaser;
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

    // 6. EQ Eight
    assert(fabs(d_eq_norm_to_freq(0.0) - 20.0f) < 0.1f);
    assert(fabs(d_eq_norm_to_freq(1.0) - 20000.0f) < 10.0f);
    float test_freq = 440.0f;
    assert(fabs(d_eq_norm_to_freq(d_eq_freq_to_norm(test_freq)) - test_freq) < 0.1f);

    EqDspState eq;
    eq.reset();
    assert(eq.enabled);
    assert(fabs(d_eq_get_magnitude_db(&eq, 1000.0f)) < 0.01f);

    // Boost band 1 with Bell (type=5, norm=5/5.0=1.0) at 1kHz
    d_eq_set_parameter(&eq, 0, 1.0); // Bell
    d_eq_set_parameter(&eq, 8, d_eq_freq_to_norm(1000.0f));
    d_eq_set_parameter(&eq, 16, 0.75); // +12dB
    d_eq_set_parameter(&eq, 24, d_eq_q_to_norm(1.0f));
    assert(d_eq_get_magnitude_db(&eq, 1000.0f) > 6.0f);

    float[32] eq_in_l = 0.5f;
    float[32] eq_in_r = 0.5f;
    d_eq_process(&eq, eq_in_l.ptr, eq_in_r.ptr, 32, 48000.0);

    // 7. Envelope Shaper
    assert(d_env_shaper_norm_to_rate_beats(0.0) == 0.25f);
    assert(d_env_shaper_norm_to_rate_beats(1.0) == 16.0f);

    EnvShaperDspState env_s;
    env_s.reset();
    assert(env_s.enabled);
    assert(env_s.getNumPoints() >= 2);

    // Test ducking curve evaluation: start at full (1.0), quick dip at 0.1
    float c0 = d_env_shaper_evaluate_curve(&env_s, 0.0f);
    assert(fabs(c0 - 1.0f) < 0.05f);

    float[64] es_in_l = 1.0f;
    float[64] es_in_r = 1.0f;
    float[64] es_out_l, es_out_r;
    d_env_shaper_process(&env_s, es_in_l.ptr, es_in_r.ptr, es_out_l.ptr, es_out_r.ptr, 64, 48000.0, 120.0, 0.0);
    assert(es_out_l[0] > 0.5f);

    // 8. Phaser
    assert(d_phaser_norm_to_stages(0.0) == 2);
    assert(d_phaser_norm_to_stages(1.0) == 12);
    assert(d_phaser_norm_to_mode(0.0) == 0); // PHASER

    PhaserDspState phaser;
    phaser.reset();
    assert(phaser.enabled);

    float[64] ph_in_l = 0.5f;
    float[64] ph_in_r = 0.5f;
    float[64] ph_out_l, ph_out_r;
    d_phaser_process(&phaser, ph_in_l.ptr, ph_in_r.ptr, ph_out_l.ptr, ph_out_r.ptr, 64, 48000.0, 120.0);
    assert(ph_out_l[0] != 0.0f);
}
