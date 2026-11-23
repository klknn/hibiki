module engine.core.core_d_test;

import core.math : fabs;
import engine.core.audio_file;
import engine.core.biquad_filter;
import engine.core.history;
import engine.core.math;
import engine.core.midi;
import engine.core.modulator;
import engine.core.project;
import engine.core.resample;
import engine.instruments.adsr;
import engine.instruments.multi_point_envelope;
import pb.core : AutomationPoint;

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

    // 7. ProjectStateD & automation tests
    assert(beatsToSec(4.0, 120.0) == 2.0);
    assert(secToBeats(2.0, 120.0) == 4.0);

    // Linear automation interpolation
    assert(interpolateAutomation(0.0f, 1.0f, 0.5f, 0.0f) == 0.5f);

    auto proj = new ProjectStateD();
    proj.bpm = 120.0;
    proj.playheadPosSec = 4.0;
    proj.loopStartSec = 1.0;
    proj.loopEndSec = 5.0;

    auto trk = proj.getOrCreateTrack(0);
    trk.name = "Lead";
    trk.addTimelineClip("audio.wav", 2.0, proj.bpm, 4.0);
    trk.addAutomationLane(0, 1);

    // Add automation clip with points
    auto autoClip = new ClipD();
    autoClip.type = ClipD.Type.AUTOMATION;
    autoClip.durationBeats = 4.0;
    AutomationPoint p0; p0.time_beats = 0.0f; p0.value = 0.2f;
    AutomationPoint p1; p1.time_beats = 4.0f; p1.value = 0.8f;
    autoClip.automationPoints ~= p0;
    autoClip.automationPoints ~= p1;

    auto tcAuto = new TimelineClipD();
    tcAuto.clip = autoClip;
    tcAuto.startTimeSec = 3.0;
    tcAuto.durationBeats = 4.0;
    trk.automationLanes[0].clips ~= tcAuto;

    // Test getAutomationValue
    float val = getAutomationValue(trk.automationLanes[0], 6.0, proj.bpm);
    // At start (3.0s * 120/60 = 6.0 beats), localBeats = 0.0 -> value = 0.2
    assert(fabs(val - 0.2f) < 1e-5f);

    // Test setBpm: 120 -> 60 (factor of 2.0)
    proj.setBpm(60.0);
    assert(proj.bpm == 60.0);
    assert(proj.playheadPosSec == 8.0);
    assert(proj.loopStartSec == 2.0);
    assert(proj.loopEndSec == 10.0);
    assert(trk.timelineClips[0].startTimeSec == 4.0);
    assert(trk.automationLanes[0].clips[0].startTimeSec == 6.0);

    // Test captureState / applyState serialization roundtrip
    ubyte[] snapshot = proj.captureState();
    assert(snapshot.length > 0);

    auto proj2 = new ProjectStateD();
    assert(proj2.applyState(snapshot));
    assert(proj2.bpm == 60.0);
    assert(proj2.playheadPosSec == 8.0);
    assert(0 in proj2.tracks);
    assert(proj2.tracks[0].name == "Lead");
    assert(proj2.tracks[0].timelineClips.length == 1);
    assert(proj2.tracks[0].timelineClips[0].startTimeSec == 4.0);
    assert(proj2.tracks[0].automationLanes.length == 1);
    assert(proj2.tracks[0].automationLanes[0].clips.length == 1);
    assert(proj2.tracks[0].automationLanes[0].clips[0].startTimeSec == 6.0);
}
