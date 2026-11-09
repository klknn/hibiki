module engine.instruments.instruments_d_test;

import core.math : fabs;
import engine.instruments.builtin_acid_bass;
import engine.instruments.builtin_dr8_kick;

@nogc nothrow unittest {
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
}
