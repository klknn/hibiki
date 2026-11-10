module engine.effects.effects_d_test;

import core.math : fabs;
import engine.effects.builtin_aux;
import engine.effects.builtin_bitcrusher;
import engine.effects.builtin_chorus;
import engine.effects.builtin_compressor;
import engine.effects.builtin_delay;
import engine.effects.builtin_envelope_shaper;
import engine.effects.builtin_eq;
import engine.effects.builtin_hott;
import engine.effects.builtin_limiter;
import engine.effects.builtin_maxim;
import engine.effects.builtin_phaser;
import engine.effects.builtin_reverb;
import engine.effects.builtin_stereo_width;
import engine.effects.builtin_vocodey;

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

    // 9. Aux
    AuxDspState aux;
    aux.reset();
    assert(d_aux_get_parameter(&aux, AuxParamId.PARAM_GAIN) == 0.7);
    assert(d_aux_get_parameter(&aux, AuxParamId.PARAM_PAN) == 0.5);

    float[32] aux_in_l = 1.0f;
    float[32] aux_in_r = 1.0f;
    float[32] aux_out_l, aux_out_r;
    d_aux_process(&aux, aux_in_l.ptr, aux_in_r.ptr, aux_out_l.ptr, aux_out_r.ptr, 32);
    assert(aux_out_l[0] > 0.5f);
    assert(aux_out_r[0] > 0.5f);

    // 10. Hott (OTT)
    assert(d_hott_norm_to_freq(0.0, 20.0f, 500.0f) == 20.0f);
    assert(fabs(d_hott_norm_to_freq(1.0, 20.0f, 500.0f) - 500.0f) < 0.1f);
    assert(d_hott_norm_to_db24(0.5) == 0.0f);

    HottDspState hott;
    hott.reset();
    assert(hott.enabled);
    assert(d_hott_get_parameter(&hott, HottParamId.PARAM_AMOUNT) == 1.0);
    assert(d_hott_get_parameter(&hott, HottParamId.PARAM_TIME) == 1.0);

    float[128] hott_in_l = 0.3f;
    float[128] hott_in_r = 0.3f;
    float[128] hott_out_l, hott_out_r;
    d_hott_process(&hott, hott_in_l.ptr, hott_in_r.ptr, hott_out_l.ptr, hott_out_r.ptr, 128, 44100.0, 0);
    assert(hott_out_l[0] != 0.0f);

    // 11. Reverb
    assert(d_reverb_norm_to_predelay_ms(0.5) == 50.0f);
    auto rev = d_reverb_create(44100.0);
    assert(rev !is null);
    assert(d_reverb_get_parameter(rev, ReverbParamId.PARAM_ROOM_SIZE) == 0.5);

    float[128] rev_in_l = 0.5f;
    float[128] rev_in_r = 0.5f;
    float[128] rev_out_l, rev_out_r;
    d_reverb_process(rev, rev_in_l.ptr, rev_in_r.ptr, rev_out_l.ptr, rev_out_r.ptr, 128);
    assert(rev_out_l[0] != 0.0f);
    d_reverb_destroy(rev);

    // 12. Maxim
    assert(d_maxim_norm_to_freq(0.0, 20.0f, 500.0f) == 20.0f);
    assert(d_maxim_norm_to_gain_db(0.5) == 0.0f);
    auto maxim = d_maxim_create(44100.0);
    assert(maxim !is null);
    assert(d_maxim_get_parameter(maxim, MaximParamId.PARAM_LOOKAHEAD) == 0.2);

    float[256] max_in_l = 0.5f;
    float[256] max_in_r = 0.5f;
    float[256] max_out_l, max_out_r;
    d_maxim_process(maxim, max_in_l.ptr, max_in_r.ptr, max_out_l.ptr, max_out_r.ptr, 256, 44100.0, 0);
    float max_peak = 0.0f;
    foreach (s; max_out_l) {
        float a = s >= 0 ? s : -s;
        if (a > max_peak) max_peak = a;
    }
    assert(max_peak > 0.01f);
    d_maxim_destroy(maxim);

    // 13. Vocodey
    assert(d_vocodey_norm_to_attack_s(0.0) == 0.001f);
    assert(d_vocodey_norm_to_bandwidth_q(0.0) == 0.5f);
    auto voc = d_vocodey_create();
    assert(voc !is null);
    assert(d_vocodey_get_parameter(voc, VocodeyParamId.PARAM_DRY) == 0.0);
    assert(d_vocodey_get_parameter(voc, VocodeyParamId.PARAM_WET) == 1.0);

    float[128] voc_in_l = 0.5f;
    float[128] voc_in_r = 0.5f;
    float[128] voc_out_l, voc_out_r;
    VocodeyMidiEvent[1] voc_ev = [VocodeyMidiEvent(60, 0.9f, true)];
    d_vocodey_process(voc, voc_in_l.ptr, voc_in_r.ptr, voc_out_l.ptr, voc_out_r.ptr, 128, 44100.0, voc_ev.ptr, 1, null, null);
    float voc_peak = 0.0f;
    foreach (s; voc_out_l) {
        float a = s >= 0 ? s : -s;
        if (a > voc_peak) voc_peak = a;
    }
    assert(voc_peak > 0.001f);
    d_vocodey_destroy(voc);
}
