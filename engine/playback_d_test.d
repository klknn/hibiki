module engine.playback_d_test;

import core.math : fabs;
import engine.playback_mixer;

@nogc nothrow unittest {
    // 1. Test playhead advancement without looping
    double dt = 512.0 / 44100.0;
    double p1 = advancePlayhead(1.0, dt, false, 0.0, 0.0);
    assert(fabs(p1 - (1.0 + dt)) < 1e-9);

    // 2. Test playhead advancement with loop wrapping
    // When nextPos exceeds loopEnd (5.0s), wraps back relative to loopStart (1.0s)
    double p2 = advancePlayhead(4.99, 0.02, true, 1.0, 5.0);
    assert(fabs(p2 - 1.01) < 1e-6);

    // 3. Test applyTrackGainAndPan
    double[4] bufL = [1.0, -0.5, 0.25, 0.0];
    double[4] bufR = [0.5, 0.5, -0.5, 0.0];
    TrackMixStrip strip;
    strip.volume = 1.0f;
    strip.pan = -1.0f; // Hard left
    strip.audible = true;

    applyTrackGainAndPan(bufL[], bufR[], strip);
    // Hard left: Left = 1.0 * inL, Right = 0.0
    assert(fabs(bufL[0] - 1.0) < 1e-5);
    assert(fabs(bufR[0] - 0.0) < 1e-5);
    assert(fabs(strip.peakL - 1.0f) < 1e-5f);
    assert(fabs(strip.peakR - 0.0f) < 1e-5f);

    // Muted / silenced track
    bufL[] = 1.0;
    bufR[] = 1.0;
    strip.audible = false;
    applyTrackGainAndPan(bufL[], bufR[], strip);
    assert(bufL[0] == 0.0);
    assert(bufR[0] == 0.0);
    assert(strip.peakL == 0.0f);
    assert(strip.peakR == 0.0f);

    // 4. Test accumulateMix
    double[4] masterL = [0.1, 0.2, 0.3, 0.4];
    double[4] masterR = [0.1, 0.2, 0.3, 0.4];
    double[4] trkL = [0.5, 0.5, 0.5, 0.5];
    double[4] trkR = [0.2, 0.2, 0.2, 0.2];

    accumulateMix(trkL[], trkR[], masterL[], masterR[]);
    assert(fabs(masterL[0] - 0.6) < 1e-9);
    assert(fabs(masterR[0] - 0.3) < 1e-9);

    // 5. Test processMasterOutput
    float[8] interleaved;
    float peakL = 0.0f, peakR = 0.0f;
    processMasterOutput(masterL[], masterR[], interleaved[], peakL, peakR);

    assert(fabs(interleaved[0] - 0.6f) < 1e-6f);
    assert(fabs(interleaved[1] - 0.3f) < 1e-6f);
    assert(fabs(peakL - 0.9f) < 1e-6f);
    assert(fabs(peakR - 0.6f) < 1e-6f);

    // 6. Test renderAudioClipBlock (stereo + loop + fade-in/out)
    float[4] stereoClip = [0.5f, -0.5f, 1.0f, -1.0f];
    double[4] clipL = [0.0, 0.0, 0.0, 0.0];
    double[4] clipR = [0.0, 0.0, 0.0, 0.0];
    renderAudioClipBlock(stereoClip[], 2, true, 0, 0, 2, 0.0, 1.0, 0.0, 0.0, 100.0, clipL[], clipR[]);
    assert(fabs(clipL[0] - 0.5) < 1e-6 && fabs(clipR[0] + 0.5) < 1e-6);
    assert(fabs(clipL[1] - 1.0) < 1e-6 && fabs(clipR[1] + 1.0) < 1e-6);
    assert(fabs(clipL[2] - 0.5) < 1e-6 && fabs(clipR[2] + 0.5) < 1e-6);
    assert(fabs(clipL[3] - 1.0) < 1e-6 && fabs(clipR[3] + 1.0) < 1e-6);

    // 7. Test renderAudioClipBlock mono with fade-in
    float[2] monoClip = [1.0f, 1.0f];
    double[2] monoL = [0.0, 0.0];
    double[2] monoR = [0.0, 0.0];
    renderAudioClipBlock(monoClip[], 1, false, 0, 0, 2, 0.0, 1.0, 1.0, 0.0, 100.0, monoL[], monoR[]);
    // At sample 0, t=0 => -30dB => ~0.03162; at sample 1, t=0.01 => slightly louder
    assert(monoL[0] > 0.03 && monoL[0] < 0.04);
    assert(monoL[1] > monoL[0]);
    assert(fabs(monoL[0] - monoR[0]) < 1e-9);

    // 8. Test accumulateAuxSend
    double[2] auxDstL = [0.1, 0.2];
    double[2] auxDstR = [0.3, 0.4];
    double[2] auxSrcL = [1.0, 2.0];
    double[2] auxSrcR = [0.5, 1.0];
    accumulateAuxSend(auxSrcL[], auxSrcR[], auxDstL[], auxDstR[], 0.5f);
    assert(fabs(auxDstL[0] - 0.6) < 1e-9);
    assert(fabs(auxDstL[1] - 1.2) < 1e-9);
    assert(fabs(auxDstR[0] - 0.55) < 1e-9);
    assert(fabs(auxDstR[1] - 0.9) < 1e-9);
}
