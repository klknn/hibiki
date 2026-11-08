module engine.core.core_d_test;

import core.math : fabs;
import engine.core.audio_file;
import engine.core.biquad_filter;
import engine.core.history;
import engine.core.math;
import engine.core.midi;
import engine.core.modulator;
import engine.core.resample;
import engine.instruments.adsr;
import engine.instruments.multi_point_envelope;

@nogc nothrow unittest {
    // 1. Math utilities
    assert(clampVal(5.0f, 0.0f, 1.0f) == 1.0f);
    assert(clampVal(-2.0f, 0.0f, 1.0f) == 0.0f);
    assert(fabs(dbToLinear(0.0f) - 1.0f) < 1e-6f);
    assert(fabs(midiNoteToHz(69.0f) - 440.0f) < 1e-4f);

    // 2. BiquadFilter in @nogc mode
    BiquadFilter lpf;
    lpf.setParams(BiquadFilter.Type.LOWPASS, 1000.0f, 0.707f, 0.0f, 48000.0f);
    float y0 = lpf.process(1.0f);
    assert(y0 > 0.0f && y0 < 1.0f);
    lpf.reset();

    // 3. Adsr in @nogc mode
    Adsr env;
    env.setParams(0.01f, 0.05f, 0.5f, 0.02f);
    assert(env.isIdle());
    env.noteOn();
    float v = 0.0f;
    foreach (_; 0 .. 480) {
        v = env.process(48000.0f);
    }
    assert(v > 0.9f);
    env.noteOff();
    foreach (_; 0 .. 2000) {
        v = env.process(48000.0f);
    }
    assert(env.isIdle());

    // 4. ModulatorD in @nogc mode
    ModulatorD mod;
    mod.assigned = true;
    mod.depth = 0.5f;
    mod.rate_hz = 2.0f;
    mod.waveform = ModulatorD.Waveform.SINE;
    float mval = mod.tick(512, 48000.0, 120.0);
    assert(mval >= -0.5f && mval <= 0.5f);

    // 5. Resampler rational approximation
    int a = 0, b = 0;
    d_rational_approx(44100.0 / 48000.0, 1000, &a, &b);
    assert(a == 147 && b == 160);
}

@safe unittest {
    // 6. HistoryManagerD undo/redo stack
    HistoryManagerD hist;
    hist.maxHistory = 3;
    hist.pushState([1]);
    hist.pushState([2]);
    hist.pushState([3]);
    hist.pushState([4]); // drops [1]
    assert(hist.undoCount() == 3);

    ubyte[] outState;
    assert(hist.undo([5], outState));
    assert(outState == [4]);
    assert(hist.redo([4], outState));
    assert(outState == [5]);
}
