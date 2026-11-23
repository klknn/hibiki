module gui_d.e2e_ipc_test;

import core.sync.mutex : Mutex;
import core.thread : Thread, msecs;
import gui_d.ipc_client;
import gui_d.transport_model;
import pb.commands : PluginHostMode, TransportCmd;
import pb.core : PbAutoPoint = AutomationPoint, PbPlaybackMode = PlaybackMode, TrackType;
import pb.notifications : Notification;
import std.file : exists, getSize, mkdirRecurse, remove, rmdirRecurse, tempDir;
import std.path : buildPath, dirName;
import std.process : environment;
import std.stdio : stderr, writeln;

private string resolveHbkPlayPath() {
  string[] candidates = [
    "engine/hbk-play", "./engine/hbk-play", "../engine/hbk-play",
  ];
  const string srcdir = environment.get("TEST_SRCDIR", "");
  if (srcdir.length > 0) {
    candidates ~= buildPath(srcdir, "_main", "engine", "hbk-play");
    candidates ~= buildPath(srcdir, "engine", "hbk-play");
  }
  const string rdir = environment.get("RUNFILES_DIR", "");
  if (rdir.length > 0) {
    candidates ~= buildPath(rdir, "_main", "engine", "hbk-play");
  }
  foreach (c; candidates) {
    if (exists(c))
      return c;
  }
  return "";
}

private string resolveTestdataPath(string rel) {
  if (exists(rel))
    return rel;
  const string srcdir = environment.get("TEST_SRCDIR", "");
  if (srcdir.length > 0) {
    const p = buildPath(srcdir, "_main", rel);
    if (exists(p))
      return p;
  }
  return rel;
}

unittest {
  const string engineBin = resolveHbkPlayPath();
  assert(engineBin.length > 0, "Could not locate hbk-play binary in Bazel runfiles");

  auto qMutex = new Mutex();
  Notification[] rxQueue;

  auto client = new EngineProcessClient((Notification notif) {
    synchronized (qMutex) {
      rxQueue ~= notif;
    }
  });
  assert(client.start(engineBin), "Failed to spawn hbk-play subprocess: " ~ engineBin);
  scope (exit)
    client.stop();

  TransportModel model;
  model.initDefaultTemplate();

  void drainNotifications(int waitMs = 15) {
    if (waitMs > 0)
      Thread.sleep(waitMs.msecs);
    Notification[] batch;
    synchronized (qMutex) {
      batch = rxQueue.dup;
      rxQueue.length = 0;
    }
    foreach (n; batch) {
      model.applyNotification(n);
    }
  }

  bool waitUntil(bool delegate() pred, int timeoutMs = 4000) {
    int elapsed = 0;
    while (elapsed < timeoutMs) {
      drainNotifications(15);
      if (pred())
        return true;
      elapsed += 15;
    }
    drainNotifications(0);
    return pred();
  }

  // Wait for initial ConfigInfo from runIpcLoop()
  assert(waitUntil(() => model.bufferLatencyMs > 0),
      "Timed out waiting for initial ConfigInfo from hbk-play");

  // 1. Sync default FL template -> engine (including track 4 automation + channels 0..3)
  model.saveCurrentPatternToStore();
  client.sendRequest(makeSetBpmRequest(cast(float)model.bpm));
  foreach (ref trk; model.tracks) {
    client.sendRequest(makeSetTrackTypeRequest(trk.index, trk.index == 0 ?
        TrackType.TRACK_MASTER : TrackType.TRACK_GROUP));
    if (trk.index > 0) {
      client.sendRequest(makeSetOutputRoutingRequest(trk.index, trk.routedTo));
    }
    client.sendRequest(makeTrackVolumeRequest(trk.index, trk.volume));
    client.sendRequest(makeTrackPanRequest(trk.index, trk.pan));
    foreach (ref plug; trk.plugins) {
      if (plug.uri.length > 0) {
        client.sendRequest(makePluginLoadRequest(trk.index, plug.uri, plug
            .pluginIndex));
      }
    }
  }
  foreach (ref ch; model.channels) {
    client.sendRequest(makeSequencerAddChannelRequest(ch.index, ch.name,
        ch.uri, ch.category, ch.color, ch.mixerInsert));
    client.sendRequest(makeSequencerVolumeChannelRequest(ch.index, ch.volume));
  }
  // Route Channel 0 -> Master (0) explicitly to verify mixer_insert=0 preservation across save/load
  model.channels[0].mixerInsert = 0;
  client.sendRequest(makeSequencerRouteChannelRequest(0, 0));

  foreach (ref pat; model.patterns) {
    client.sendRequest(makeSequencerAddPatternRequest(pat.index, pat.name,
        pat.color, pat.lengthSteps));
    foreach (ref cp; pat.channelSteps) {
      foreach (stepIdx; 0 .. 16) {
        if (cp.steps[stepIdx]) {
          client.sendRequest(makeSequencerStepRequest(pat.index, cp.channelIndex, stepIdx,
              true));
        }
      }
      if (cp.notes.length > 0) {
        client.sendRequest(makeSequencerChannelNotesRequest(pat.index, cp
            .channelIndex, cp.notes));
      }
    }
  }

  const string loopWav = resolveTestdataPath("testdata/loop140.wav");
  assert(exists(loopWav), "Missing testdata/loop140.wav in runfiles");

  foreach (ref trk; model.tracks) {
    bool addedAuto = false;
    uint autoIdx = 0;
    foreach (ref cl; trk.clips) {
      const float durBeats = cast(float)(cl.durationSec * (model.bpm / 60.0));
      if (cl.kind == PlaylistClipKind.PATTERN) {
        client.sendRequest(makeAddPatternTimelineClipRequest(trk.index,
            cl.patternIndex, cast(float)cl.startTimeSec, durBeats));
      } else if (cl.kind == PlaylistClipKind.AUDIO) {
        client.sendRequest(makeAddTimelineClipRequest(trk.index, loopWav,
            cast(float)cl.startTimeSec, durBeats));
      } else if (cl.kind == PlaylistClipKind.AUTOMATION) {
        if (!addedAuto) {
          client.sendRequest(makeAutomationAddLaneRequest(trk.index, 0, 0));
          addedAuto = true;
        }
        client.sendRequest(makeAutomationAddClipRequest(trk.index, 0,
            cast(float)cl.startTimeSec, durBeats));
        PbAutoPoint[] pts;
        foreach (const ref ap; cl.autoPoints) {
          pts ~= PbAutoPoint(ap.normTime * durBeats, ap.value, 0.0f);
        }
        client.sendRequest(makeAutomationUpdatePointsRequest(trk.index, 0, autoIdx,
            pts));
        ++autoIdx;
      }
    }
  }

  // Verify generator ParamList arrived with real parameters
  const bool syncedInitial = waitUntil(() => model.channels.length >= 4 &&
      model.channels[3].generatorPlugin.params.length > 0 &&
      model.channels[3].generatorPlugin.name.length > 0);
  if (!syncedInitial) {
    stderr.writefln("E2E initial sync failed: ch=%d ch3_params=%d ch0_ins=%d",
        model.channels.length, model.channels.length > 3 ?
        model.channels[3].generatorPlugin.params.length : 0,
        model.channels.length > 0 ? model.channels[0].mixerInsert : -1);
  }
  assert(syncedInitial, "Expected hbk-play to sync 4 channels and generator ParamList");

  // 2. Verify SET_BPM updates BPM cleanly without clearing channels or clips
  client.sendRequest(makeSetBpmRequest(128.0f));
  assert(waitUntil(() => model.bpm > 127.0 && model.bpm < 129.0),
      "Expected BPM update to reach TransportModel");
  assert(model.channels.length >= 4, "SET_BPM must not wipe channels via ClearProject");

  // 3. Verify Remove FX slot leaves no zombie 'Plugin'
  const size_t t1Arr = model.ensureTrackIndex(1);
  const size_t origFxCount = model.tracks[t1Arr].plugins.length;
  if (origFxCount > 0) {
    client.sendRequest(makePluginRemoveRequest(1, 0));
    assert(waitUntil(() => model.tracks[t1Arr].plugins.length == origFxCount - 1),
        "Expected FX removal to update Track 1 plugins cleanly");
    foreach (ref p; model.tracks[t1Arr].plugins) {
      assert(p.name != "Plugin", "FX removal left a zombie 'Plugin' slot");
    }
  }

  // 4. Verify Save (nested parent dir), Collect Files, Bounce WAV, and Load round-trip
  const string testDir = buildPath(tempDir(), "hibiki_gui_e2e_test");
  if (exists(testDir))
    rmdirRecurse(testDir);
  mkdirRecurse(testDir);
  scope (exit) {
    if (exists(testDir))
      rmdirRecurse(testDir);
  }

  const string projPath = buildPath(testDir, "nested", "dir", "song.hbk");
  const string collectDir = buildPath(testDir, "collected");
  const string bounceWav = buildPath(testDir, "renders", "mix.wav");

  client.sendRequest(makeProjectSaveRequest(projPath));
  assert(waitUntil(() => exists(projPath) && getSize(projPath) > 0, 5000),
      "Expected ACTION_SAVE to create parent directories and write non-empty .hbk");

  client.sendRequest(makeProjectCollectFilesRequest(collectDir));
  const string collectedAudio = buildPath(collectDir, "audio", "loop140.wav");
  assert(waitUntil(() => exists(collectedAudio) && getSize(collectedAudio) > 0,
      5000),
      "Expected ACTION_COLLECT_FILES to copy loop140.wav into collectDir/audio");

  client.sendRequest(makeProjectBounceRequest(bounceWav));
  assert(waitUntil(() => model.lastBouncePath == bounceWav && exists(bounceWav) &&
      getSize(bounceWav) > 44, 15000),
      "Expected ACTION_BOUNCE to create parent dirs and render non-empty WAV");

  // Verify bounced mix.wav contains non-silent audio samples
  {
    import std.file : read;

    const ubyte[] wavBytes = cast(const(ubyte)[])read(bounceWav);
    assert(wavBytes.length > 44 && wavBytes[0 .. 4] == cast(const(ubyte)[])"RIFF",
        "Expected valid RIFF WAV header from ACTION_BOUNCE");
    const ushort audioFmt = cast(ushort)(wavBytes[20] | (wavBytes[21] << 8));
    const ushort bitsPerSample = cast(ushort)(wavBytes[34] | (wavBytes[35] << 8));
    // Scan 'data' chunk payload after finding "data" marker
    size_t dataOff = 36;
    uint dataSize = 0;
    while (dataOff + 8 <= wavBytes.length) {
      const uint chunkSz = cast(uint)wavBytes[dataOff + 4] | (cast(
          uint)wavBytes[dataOff + 5] << 8) | (
          cast(uint)wavBytes[dataOff + 6] << 16) | (
          cast(uint)wavBytes[dataOff + 7] << 24);
      if (wavBytes[dataOff .. dataOff + 4] == cast(const(ubyte)[])"data") {
        dataOff += 8;
        dataSize = chunkSz;
        break;
      }
      dataOff += 8 + chunkSz;
    }
    assert(dataSize > 0 && dataOff + dataSize <= wavBytes.length,
        "Expected non-empty WAV data chunk");
    double peakAmp = 0.0;
    if (audioFmt == 1 && bitsPerSample == 16) {
      for (size_t i = dataOff; i + 1 < dataOff + dataSize; i += 2) {
        const short s = cast(short)(wavBytes[i] | (wavBytes[i + 1] << 8));
        const double a = (s < 0 ? -cast(double)s : cast(double)s) / 32768.0;
        if (a > peakAmp)
          peakAmp = a;
      }
    } else if (audioFmt == 1 && bitsPerSample == 24) {
      for (size_t i = dataOff; i + 2 < dataOff + dataSize; i += 3) {
        int v = wavBytes[i] | (wavBytes[i + 1] << 8) | (wavBytes[i + 2] << 16);
        if ((v & 0x800000) != 0)
          v |= 0xFF000000;
        const double a = (v < 0 ? -cast(double)v : cast(double)v) / 8388608.0;
        if (a > peakAmp)
          peakAmp = a;
      }
    } else {
      // Fallback non-zero byte check for IEEE float or other valid PCM formats
      foreach (b; wavBytes[dataOff .. dataOff + dataSize]) {
        if (b != 0) {
          peakAmp = 0.5;
          break;
        }
      }
    }
    assert(peakAmp > 0.001, "Expected bounced mix.wav to contain non-silent audio waveform");
  }

  // 5. Verify PAT/SONG switch, 32-step toggle, Undo/Redo, and ACTION_CLEAR_HISTORY
  client.sendRequest(makeSequencerModeRequest(PbPlaybackMode.PLAYBACK_MODE_PAT));
  assert(waitUntil(() => model.playbackMode == PlaybackMode.PAT, 4000),
      "Expected PLAYBACK_MODE_PAT sync");
  client.sendRequest(makeSequencerModeRequest(PbPlaybackMode.PLAYBACK_MODE_SONG));
  assert(waitUntil(() => model.playbackMode == PlaybackMode.SONG, 4000),
      "Expected PLAYBACK_MODE_SONG sync");

  client.sendRequest(makeSequencerPatternLengthRequest(0, 32));
  client.sendRequest(makeSequencerStepRequest(0, 0, 20, true));

  // Corrupt local flags so assertions prove notifications came back from engine SyncProjectToGui
  model.patterns[0].lengthSteps = 16;
  model.channels[0].steps[20] = true;
  client.sendRequest(makeUndoRedoRequest(false));
  assert(waitUntil(() => model.patterns.length > 0 &&
      model.patterns[0].lengthSteps == 32 && model.channels.length > 0 &&
      !model.channels[0].steps[20], 4000), "Expected Undo to revert sequencer step 20 while retaining 32-step pattern length");

  client.sendRequest(makeUndoRedoRequest(true));
  assert(waitUntil(() => model.channels.length > 0 && model.channels[0].steps[20],
      4000), "Expected Redo to restore sequencer step 20");

  client.sendRequest(makeClearHistoryRequest());
  client.sendRequest(makeUndoRedoRequest(false));
  const string proj32Path = buildPath(testDir, "nested", "dir", "song32.hbk");
  client.sendRequest(makeProjectSaveRequest(proj32Path));
  assert(waitUntil(() => exists(proj32Path) && getSize(proj32Path) > 0, 4000),
      "Expected Save after ClearHistory to succeed");
  model.patterns[0].lengthSteps = 16;
  model.channels[0].steps[20] = false;
  client.sendRequest(makeProjectLoadRequest(proj32Path));
  assert(waitUntil(() => model.patterns.length > 0 &&
      model.patterns[0].lengthSteps == 32 && model.channels.length > 0 &&
      model.channels[0].steps[20], 4000), "Expected ACTION_CLEAR_HISTORY + Save/Load to prove Undo left step 20 active in 32-step pattern");

  // 6. Wipe model, load saved .hbk back, and verify channels + automation clips come back from engine
  model.channels.length = 0;
  model.patterns.length = 0;
  client.sendRequest(makeProjectLoadRequest(projPath));
  const size_t t4Arr = model.ensureTrackIndex(4);
  assert(waitUntil(() {
      if (model.channels.length < 4 || model.patterns.length < 1)
        return false;
      const size_t idx4 = model.ensureTrackIndex(4);
      foreach (const ref cl; model.tracks[idx4].clips) {
        if (cl.kind == PlaylistClipKind.AUTOMATION && cl.autoPoints.length >= 2)
          return true;
      }
      return false;
    }, 5000), "Expected ACTION_LOAD round-trip to restore channels, patterns, and Track 4 automation clips");
  assert(model.channels[0].mixerInsert == 0, "Expected mixerInsert=0 preserved after Save/Load");

  // 7. Verify EngineProcessClient.onError surfaces process spawn failure
  string capturedErr;
  auto errClient = new EngineProcessClient((Notification n) {}, (string err) {
    capturedErr = err;
  });
  assert(!errClient.start("/nonexistent_hibiki_engine_binary_path_xyz"));
  assert(capturedErr.length > 0, "Expected failed start() to invoke EngineProcessClient.onError");
}
