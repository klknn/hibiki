module engine.instruments.instruments_d_test;

import core.math : fabs;
import engine.instruments.builtin_3xosc;
import engine.instruments.builtin_acid_bass;
import engine.instruments.builtin_dr8_clap;
import engine.instruments.builtin_dr8_hat;
import engine.instruments.builtin_dr8_kick;
import engine.instruments.builtin_dr8_snare;

@nogc nothrow unittest {
    // 0. 3xOsc
    ThreeXOscDspState osc;
    osc.reset();
    assert(osc.enabled);
    assert(d_3xosc_get_default_value(ThreeXOscParamId.P_VOL) == 1.0);

    float[128] osc_out_l, osc_out_r;
    ThreeXOscMidiEvent[1] osc_ev = [ThreeXOscMidiEvent(69, 1.0f, true)];
    d_3xosc_process(osc, osc_out_l.ptr, osc_out_r.ptr, 128, 48000.0, osc_ev.ptr, 1);
    assert(osc.voices[0].active);
    float osc_max = 0.0f;
    foreach (s; osc_out_l) {
        float a = s >= 0 ? s : -s;
        if (a > osc_max) osc_max = a;
    }
    assert(osc_max > 0.01f);

    // 1. Acid Bass
    AcidBassDspState acid;
    acid.reset();
    assert(d_acid_norm_to_cutoff_hz(0.0) == 100.0f);
    assert(d_acid_norm_to_decay_s(0.0) == 0.05f);

    float[128] out_l, out_r;

    // Trigger note 60 (C4)
    AcidMidiEvent[1] ev_on = [AcidMidiEvent(60, 0.9f, true)];
    d_acid_bass_process(
        acid, out_l.ptr, out_r.ptr, 128, 48000.0, ev_on.ptr, 1,
        0.0, 0.5, 0.6, 0.5, 0.2, 0.0, 0.1, 0.7, 0.5, 0.0, 0.0
    );

    assert(acid.note_active);
    assert(acid.target_freq > 260.0 && acid.target_freq < 263.0);
    // Sound produced
    float max_val = 0.0f;
    foreach (s; out_l) {
        float a = s >= 0 ? s : -s;
        if (a > max_val) max_val = a;
    }
    assert(max_val > 0.001f);

    // Note off
    AcidMidiEvent[1] ev_off = [AcidMidiEvent(60, 0.0f, false)];
    d_acid_bass_process(
        acid, out_l.ptr, out_r.ptr, 128, 48000.0, ev_off.ptr, 1,
        0.0, 0.5, 0.6, 0.5, 0.2, 0.0, 0.1, 0.7, 0.5, 0.0, 0.0
    );
    assert(!acid.note_active);

    // 2. DR8 Kick
    Dr8KickDspState kick;
    kick.reset();
    assert(d_dr8_kick_norm_to_pitch_hz(0.0) == 40.0f);
    assert(d_dr8_kick_norm_to_pitch_hz(1.0) == 80.0f);

    float[256] kout_l, kout_r;
    Dr8KickMidiEvent[1] kev_on = [Dr8KickMidiEvent(1.0f, true)];
    d_dr8_kick_process(
        kick, kout_l.ptr, kout_r.ptr, 256, 48000.0, kev_on.ptr, 1,
        0.25, 0.4, 0.3, 0.5, 0.2, 0.1, 0.7
    );
    assert(kick.active);
    float k_max = 0.0f;
    foreach (s; kout_l) {
        float a = s >= 0 ? s : -s;
        if (a > k_max) k_max = a;
    }
    assert(k_max > 0.01f);

    // 3. DR8 Snare
    Dr8SnareDspState snare;
    snare.reset();
    assert(d_dr8_snare_norm_to_pitch_hz(0.0) == 100.0f);
    assert(d_dr8_snare_norm_to_pitch_hz(1.0) == 250.0f);

    float[256] sout_l, sout_r;
    Dr8SnareMidiEvent[1] sev_on = [Dr8SnareMidiEvent(38, 0.9f, true)];
    d_dr8_snare_process(&snare, sout_l.ptr, sout_r.ptr, 256, 48000.0, sev_on.ptr, 1);
    assert(snare.active);
    float s_max = 0.0f;
    foreach (s; sout_l) {
        float a = s >= 0 ? s : -s;
        if (a > s_max) s_max = a;
    }
    assert(s_max > 0.01f);

    // 4. DR8 Hat
    Dr8HatDspState hat;
    hat.reset();
    assert(fabs(d_dr8_hat_norm_to_hpf_hz(0.0) - 3000.0f) < 0.1f);
    assert(fabs(d_dr8_hat_norm_to_bpf_hz(0.0) - 6000.0f) < 0.1f);

    float[256] hout_l, hout_r;
    Dr8HatMidiEvent[1] hev_on = [Dr8HatMidiEvent(42, 0.9f, true)];
    d_dr8_hat_process(&hat, hout_l.ptr, hout_r.ptr, 256, 48000.0, hev_on.ptr, 1);
    assert(hat.active);
    float h_max = 0.0f;
    foreach (s; hout_l) {
        float a = s >= 0 ? s : -s;
        if (a > h_max) h_max = a;
    }
    assert(h_max > 0.001f);

    // 5. DR8 Clap
    Dr8ClapDspState clap;
    clap.reset();
    assert(fabs(d_dr8_clap_norm_to_decay_s(0.0) - 0.05f) < 0.001f);
    assert(fabs(d_dr8_clap_norm_to_cutoff_hz(0.0) - 500.0f) < 0.1f);

    float[256] cout_l, cout_r;
    Dr8ClapMidiEvent[1] cev_on = [Dr8ClapMidiEvent(39, 0.9f, true)];
    d_dr8_clap_process(&clap, cout_l.ptr, cout_r.ptr, 256, 48000.0, cev_on.ptr, 1);
    assert(clap.active);
    float c_max = 0.0f;
    foreach (s; cout_l) {
        float a = s >= 0 ? s : -s;
        if (a > c_max) c_max = a;
    }
    assert(c_max > 0.001f);
}
