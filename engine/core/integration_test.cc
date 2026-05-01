// Integration tests for timeline playback with real Dexed.vst3.
// Tests audio rendering timing, clip load/move/delete for audio + MIDI,
// and virtual MIDI recording.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <numeric>
#include <vector>

#include "engine/core/clip.hpp"
#include "engine/commands/commands.hpp"
#include "engine/core/midi.hpp"
#include "engine/core/track.hpp"
#include "engine/ipc/ipc.hpp"
#include "engine/plugin/iplugin.hpp"
#include "engine/test_utils.hpp"
#include "engine/vst3/vst3_host.hpp"

namespace hibiki {

// ── Helpers ─────────────────────────────────────────────────────────

std::string GetDexedPath() {
  std::string path = "testdata/Dexed.vst3";
#ifdef _WIN32
  std::string win_path = "testdata/Dexed.vst3/Contents/x86_64-win/Dexed.vst3";
  std::string resolved = hibiki::find_test_file(win_path);
  if (resolved != win_path) return resolved;
#endif
  return hibiki::find_test_file(path);
}

// Compute RMS amplitude of a buffer
float computeRMS(const float* buf, int n) {
  if (n == 0) return 0.0f;
  double sum = 0.0;
  for (int i = 0; i < n; i++) sum += buf[i] * buf[i];
  return (float)std::sqrt(sum / n);
}

// Compute peak amplitude of a buffer
float computePeak(const float* buf, int n) {
  float peak = 0.0f;
  for (int i = 0; i < n; i++) peak = std::max(peak, std::abs(buf[i]));
  return peak;
}

// Find the first sample index where |buf[i]| > threshold
int findFirstNonSilent(const float* buf, int n, float threshold = 1e-6f) {
  for (int i = 0; i < n; i++) {
    if (std::abs(buf[i]) > threshold) return i;
  }
  return -1;  // all silent
}

// Simulate timeline MIDI event collection for one block
// (mirrors main.cpp lines 140-172)
std::vector<MidiNoteEvent> collectTimelineMidi(const Track& track,
                                               double playhead_sec,
                                               double time_per_block,
                                               double bpm, double sample_rate) {
  std::vector<MidiNoteEvent> events;
  int block_size = (int)(time_per_block * sample_rate);
  double beats_per_sec = bpm / 60.0;

  for (const auto& tc : track.timeline_clips) {
    if (!tc->clip || tc->clip->type != Clip::Type::MIDI) continue;
    double clip_duration = (tc->duration_beats > 0)
                               ? tc->duration_beats * 60.0 / bpm
                               : tc->duration_sec;
    if (playhead_sec + time_per_block > tc->start_time_sec &&
        playhead_sec < tc->start_time_sec + clip_duration) {
      double clip_local_time = playhead_sec - tc->start_time_sec;
      double window_start_beats = clip_local_time * beats_per_sec;
      double window_end_beats =
          (clip_local_time + time_per_block) * beats_per_sec;
      for (const auto& me : tc->clip->midi_events) {
        if (me.beats >= window_start_beats && me.beats < window_end_beats) {
          MidiNoteEvent e;
          double event_local_sec = me.beats / beats_per_sec - clip_local_time;
          e.sampleOffset = std::max(0, (int)(event_local_sec * sample_rate));
          if (e.sampleOffset >= block_size) e.sampleOffset = block_size - 1;
          e.channel = me.channel;
          e.pitch = me.note;
          e.isNoteOn = hibiki::isNoteOn(me);
          e.velocity = e.isNoteOn ? me.velocity / 127.0f : 0.0f;
          events.push_back(e);
        }
      }
    }
  }
  return events;
}

// Simulate timeline audio clip mixing for one block
// (mirrors main.cpp audio clip playback)
void mixTimelineAudio(const Track& track, float* outL, float* outR,
                      int block_size, double playhead_sec, double sample_rate) {
  for (const auto& tc : track.timeline_clips) {
    if (!tc->clip || tc->clip->type != Clip::Type::AUDIO) continue;
    double clip_duration = tc->duration_sec;
    if (playhead_sec + (double)block_size / sample_rate > tc->start_time_sec &&
        playhead_sec < tc->start_time_sec + clip_duration) {
      double clip_local_time = playhead_sec - tc->start_time_sec;
      int start_sample = (int)(clip_local_time * sample_rate);
      int num_ch = tc->clip->num_channels;
      for (int i = 0; i < block_size; i++) {
        int src = start_sample + i;
        if (src >= 0 && src * num_ch < (int)tc->clip->audio_data.size()) {
          outL[i] += tc->clip->audio_data[src * num_ch];
          if (num_ch > 1)
            outR[i] += tc->clip->audio_data[src * num_ch + 1];
          else
            outR[i] += tc->clip->audio_data[src * num_ch];
        }
      }
    }
  }
}

// ── Test Fixture ────────────────────────────────────────────────────

class IntegrationTest : public ::testing::Test {
 protected:
  static constexpr int kSampleRate = 44100;
  static constexpr int kBlockSize = 512;
  static constexpr double kBpm = 120.0;

  void SetUp() override { hibiki::g_ipc_enabled = false; }

  // Render N blocks through Dexed with given MIDI events per block
  // Returns concatenated stereo audio [L, R]
  struct RenderResult {
    std::vector<float> left;
    std::vector<float> right;
  };

  RenderResult renderBlocks(
      IPlugin* plugin, int num_blocks,
      const std::vector<std::vector<MidiNoteEvent>>& events_per_block) {
    RenderResult result;
    result.left.resize(num_blocks * kBlockSize, 0.0f);
    result.right.resize(num_blocks * kBlockSize, 0.0f);

    HostProcessContext ctx;
    ctx.sampleRate = kSampleRate;
    ctx.tempo = kBpm;
    ctx.timeSigNumerator = 4;
    ctx.timeSigDenominator = 4;
    ctx.continuousTimeSamples = 0;
    ctx.projectTimeMusic = 0.0;

    for (int b = 0; b < num_blocks; b++) {
      float* outL = result.left.data() + b * kBlockSize;
      float* outR = result.right.data() + b * kBlockSize;
      float* outs[2] = {outL, outR};

      const auto& events = (b < (int)events_per_block.size())
                               ? events_per_block[b]
                               : std::vector<MidiNoteEvent>{};

      plugin->process(nullptr, outs, kBlockSize, ctx, events);
      ctx.continuousTimeSamples += kBlockSize;
      ctx.projectTimeMusic += (double)kBlockSize / kSampleRate * (kBpm / 60.0);
    }
    return result;
  }
};

// ── Dexed Rendering Tests ───────────────────────────────────────────

TEST_F(IntegrationTest, DexedRendersSilenceWithNoEvents) {
  auto plugin = std::make_unique<Vst3Plugin>();
  ASSERT_TRUE(plugin->load(GetDexedPath(), 0, kSampleRate));
  ASSERT_TRUE(plugin->isInstrument());

  auto result = renderBlocks(plugin.get(), 4, {});
  float peak = computePeak(result.left.data(), result.left.size());
  EXPECT_LT(peak, 1e-6f) << "No MIDI → should be silence";
}

TEST_F(IntegrationTest, DexedRendersAudioOnNoteOn) {
  auto plugin = std::make_unique<Vst3Plugin>();
  ASSERT_TRUE(plugin->load(GetDexedPath(), 0, kSampleRate));

  // Send note-on at sample offset 0 in first block
  MidiNoteEvent noteOn;
  noteOn.sampleOffset = 0;
  noteOn.channel = 0;
  noteOn.pitch = 60;  // C4
  noteOn.velocity = 0.8f;
  noteOn.isNoteOn = true;

  // 8 blocks = ~93ms of audio
  auto result = renderBlocks(plugin.get(), 8, {{noteOn}});

  float peak = computePeak(result.left.data(), result.left.size());
  EXPECT_GT(peak, 0.001f) << "Expected audible output after note-on";
}

TEST_F(IntegrationTest, DexedNoteTimingMatchesSampleOffset) {
  auto plugin = std::make_unique<Vst3Plugin>();
  ASSERT_TRUE(plugin->load(GetDexedPath(), 0, kSampleRate));

  // Send note-on at sample offset 256 (middle of block)
  MidiNoteEvent noteOn;
  noteOn.sampleOffset = 256;
  noteOn.channel = 0;
  noteOn.pitch = 60;
  noteOn.velocity = 0.8f;
  noteOn.isNoteOn = true;

  auto result = renderBlocks(plugin.get(), 4, {{noteOn}});

  // First 256 samples should be silent
  float first_half_peak = computePeak(result.left.data(), 256);
  EXPECT_LT(first_half_peak, 1e-6f) << "Before sampleOffset should be silent";

  // After sample 256 should have audio
  float second_half_peak =
      computePeak(result.left.data() + 256, kBlockSize - 256);
  EXPECT_GT(second_half_peak, 0.001f) << "Audio should start at sampleOffset";
}

TEST_F(IntegrationTest, TimelineMidiClipPlaybackTiming) {
  auto plugin = std::make_unique<Vst3Plugin>();
  ASSERT_TRUE(plugin->load(GetDexedPath(), 0, kSampleRate));

  // Load test.mid as timeline clip
  Track track(0);
  std::string mid_path = find_test_file("testdata/test.mid");
  track.AddTimelineClip(mid_path, 0.0, kBpm);
  ASSERT_EQ(track.timeline_clips.size(), 1);
  ASSERT_FALSE(track.timeline_clips[0]->clip->midi_events.empty());

  // Render first 8 blocks starting at playhead = 0
  HostProcessContext ctx;
  ctx.sampleRate = kSampleRate;
  ctx.tempo = kBpm;
  ctx.timeSigNumerator = 4;
  ctx.timeSigDenominator = 4;
  ctx.continuousTimeSamples = 0;
  ctx.projectTimeMusic = 0.0;

  double playhead = 0.0;
  double time_per_block = (double)kBlockSize / kSampleRate;
  std::vector<float> allAudioL;

  for (int b = 0; b < 8; b++) {
    auto events =
        collectTimelineMidi(track, playhead, time_per_block, kBpm, kSampleRate);
    std::vector<float> outL(kBlockSize, 0.0f);
    std::vector<float> outR(kBlockSize, 0.0f);
    float* outs[2] = {outL.data(), outR.data()};
    plugin->process(nullptr, outs, kBlockSize, ctx, events);
    allAudioL.insert(allAudioL.end(), outL.begin(), outL.end());
    playhead += time_per_block;
    ctx.continuousTimeSamples += kBlockSize;
    ctx.projectTimeMusic += time_per_block * (kBpm / 60.0);
  }

  // test.mid first event is at beat 0.0, so audio should appear in the first
  // block
  float first_block_peak = computePeak(allAudioL.data(), kBlockSize);
  EXPECT_GT(first_block_peak, 0.001f)
      << "First block should have audio (MIDI file starts at beat 0)";
}

// ── Clip Load Tests ─────────────────────────────────────────────────

TEST_F(IntegrationTest, LoadMidiClipAndVerifyPlayback) {
  Track track(0);
  std::string mid_path = find_test_file("testdata/test.mid");
  track.AddTimelineClip(mid_path, 2.0, kBpm);  // Start at 2.0 seconds
  ASSERT_EQ(track.timeline_clips.size(), 1);

  double time_per_block = (double)kBlockSize / kSampleRate;

  // Before clip start (playhead = 0.0) → no events
  auto events =
      collectTimelineMidi(track, 0.0, time_per_block, kBpm, kSampleRate);
  EXPECT_TRUE(events.empty()) << "No events before clip start";

  // At clip start (playhead = 2.0) → should have events
  events = collectTimelineMidi(track, 2.0, time_per_block, kBpm, kSampleRate);
  EXPECT_FALSE(events.empty())
      << "Should have events at clip start (beat 0 is at t=2.0)";
}

TEST_F(IntegrationTest, LoadAudioClipAndVerifyPlayback) {
  Track track(0);
  std::string wav_path = find_test_file("testdata/loop140.wav");
  track.AddTimelineClip(wav_path, 1.0, kBpm);  // Start at 1.0 seconds
  ASSERT_EQ(track.timeline_clips.size(), 1);

  double time_per_block = (double)kBlockSize / kSampleRate;

  // Before clip start → silence
  std::vector<float> outL(kBlockSize, 0.0f), outR(kBlockSize, 0.0f);
  mixTimelineAudio(track, outL.data(), outR.data(), kBlockSize, 0.0,
                   kSampleRate);
  EXPECT_LT(computePeak(outL.data(), kBlockSize), 1e-6f)
      << "Before clip start should be silence";

  // At clip start → audio
  std::fill(outL.begin(), outL.end(), 0.0f);
  std::fill(outR.begin(), outR.end(), 0.0f);
  mixTimelineAudio(track, outL.data(), outR.data(), kBlockSize, 1.0,
                   kSampleRate);
  EXPECT_GT(computePeak(outL.data(), kBlockSize), 0.001f)
      << "Audio clip should produce output at its start time";
}

// ── Clip Delete Tests ───────────────────────────────────────────────

TEST_F(IntegrationTest, DeleteMidiClipStopsPlayback) {
  Track track(0);
  std::string mid_path = find_test_file("testdata/test.mid");
  track.AddTimelineClip(mid_path, 0.0, kBpm);
  ASSERT_EQ(track.timeline_clips.size(), 1);

  double time_per_block = (double)kBlockSize / kSampleRate;

  // Before delete → events available
  auto events =
      collectTimelineMidi(track, 0.0, time_per_block, kBpm, kSampleRate);
  EXPECT_FALSE(events.empty()) << "Events should exist before delete";

  // Delete the clip
  track.RemoveTimelineClip(0);
  EXPECT_EQ(track.timeline_clips.size(), 0);

  // After delete → no events
  events = collectTimelineMidi(track, 0.0, time_per_block, kBpm, kSampleRate);
  EXPECT_TRUE(events.empty()) << "No events after clip deletion";
}

TEST_F(IntegrationTest, DeleteAudioClipStopsPlayback) {
  Track track(0);
  std::string wav_path = find_test_file("testdata/loop140.wav");
  track.AddTimelineClip(wav_path, 0.0, kBpm);
  ASSERT_EQ(track.timeline_clips.size(), 1);

  // Before delete → audio
  std::vector<float> outL(kBlockSize, 0.0f), outR(kBlockSize, 0.0f);
  mixTimelineAudio(track, outL.data(), outR.data(), kBlockSize, 0.0,
                   kSampleRate);
  EXPECT_GT(computePeak(outL.data(), kBlockSize), 0.001f);

  // Delete
  track.RemoveTimelineClip(0);
  EXPECT_EQ(track.timeline_clips.size(), 0);

  // After delete → silence
  std::fill(outL.begin(), outL.end(), 0.0f);
  std::fill(outR.begin(), outR.end(), 0.0f);
  mixTimelineAudio(track, outL.data(), outR.data(), kBlockSize, 0.0,
                   kSampleRate);
  EXPECT_LT(computePeak(outL.data(), kBlockSize), 1e-6f);
}

// ── Clip Move Tests ─────────────────────────────────────────────────

TEST_F(IntegrationTest, MoveMidiClipUpdatesPlayback) {
  Track track(0);
  std::string mid_path = find_test_file("testdata/test.mid");
  track.AddTimelineClip(mid_path, 0.0, kBpm);
  ASSERT_EQ(track.timeline_clips.size(), 1);

  double time_per_block = (double)kBlockSize / kSampleRate;

  // Events at original position (t=0)
  auto events =
      collectTimelineMidi(track, 0.0, time_per_block, kBpm, kSampleRate);
  EXPECT_FALSE(events.empty()) << "Events at original position";

  // Move clip to t=5.0
  track.timeline_clips[0]->start_time_sec = 5.0;

  // No events at old position
  events = collectTimelineMidi(track, 0.0, time_per_block, kBpm, kSampleRate);
  EXPECT_TRUE(events.empty()) << "No events at old position after move";

  // Events at new position
  events = collectTimelineMidi(track, 5.0, time_per_block, kBpm, kSampleRate);
  EXPECT_FALSE(events.empty()) << "Events at new position";
}

TEST_F(IntegrationTest, MoveAudioClipUpdatesPlayback) {
  Track track(0);
  std::string wav_path = find_test_file("testdata/loop140.wav");
  track.AddTimelineClip(wav_path, 0.0, kBpm);
  ASSERT_EQ(track.timeline_clips.size(), 1);

  // Audio at original position
  std::vector<float> outL(kBlockSize, 0.0f), outR(kBlockSize, 0.0f);
  mixTimelineAudio(track, outL.data(), outR.data(), kBlockSize, 0.0,
                   kSampleRate);
  EXPECT_GT(computePeak(outL.data(), kBlockSize), 0.001f);

  // Move clip to t=5.0
  track.timeline_clips[0]->start_time_sec = 5.0;

  // No audio at old position
  std::fill(outL.begin(), outL.end(), 0.0f);
  mixTimelineAudio(track, outL.data(), outR.data(), kBlockSize, 0.0,
                   kSampleRate);
  EXPECT_LT(computePeak(outL.data(), kBlockSize), 1e-6f);

  // Audio at new position
  std::fill(outL.begin(), outL.end(), 0.0f);
  mixTimelineAudio(track, outL.data(), outR.data(), kBlockSize, 5.0,
                   kSampleRate);
  EXPECT_GT(computePeak(outL.data(), kBlockSize), 0.001f);
}

// ── Virtual MIDI Recording Test ─────────────────────────────────────

TEST_F(IntegrationTest, VirtualMidiEventsAreRecorded) {
  ProjectState state;
  state.bpm = 120.0;
  state.sample_rate = kSampleRate;
  state.is_timeline_playing = true;
  state.is_recording = true;
  state.playhead_pos_sec = 0.0;
  state.record_start_sec = 0.0;

  auto track = GetOrCreateTrack(state, 0);
  int pidx = track->LoadPlugin(GetDexedPath(), 0, kSampleRate).index;
  ASSERT_GE(pidx, 0) << "Failed to load Dexed";
  track->record_armed = true;
  track->record_mode = Track::RecordMode::RECORD_MIDI;

  // Simulate virtual MIDI events being pushed to the queue
  // (same as handleSendVirtualMidi does)
  {
    MidiNoteEvent noteOn;
    noteOn.sampleOffset = 0;
    noteOn.channel = 0;
    noteOn.pitch = 60;
    noteOn.velocity = 0.8f;
    noteOn.isNoteOn = true;

    std::lock_guard<std::mutex> mlock(track->virtual_midi_mutex);
    track->virtual_midi_queue.push_back(noteOn);
  }

  // Simulate the audio thread's processing loop (one block)
  // This mirrors main.cpp lines 177-220
  ASSERT_TRUE(!track->plugins.empty() && track->plugins[0]->isInstrument());

  std::vector<MidiNoteEvent> allEvents;

  // Drain virtual MIDI queue
  {
    std::lock_guard<std::mutex> mlock(track->virtual_midi_mutex);
    allEvents.insert(allEvents.end(), track->virtual_midi_queue.begin(),
                     track->virtual_midi_queue.end());
    track->virtual_midi_queue.clear();
  }

  ASSERT_EQ(allEvents.size(), 1) << "Should have 1 virtual MIDI event";

  // Capture MIDI events for recording
  if (state.is_recording && track->record_armed &&
      track->record_mode == Track::RecordMode::RECORD_MIDI) {
    for (const auto& ev : allEvents) {
      Track::TimestampedMidiEvent tev;
      tev.time_sec =
          state.playhead_pos_sec + ev.sampleOffset / state.sample_rate;
      tev.event = ev;
      track->midi_record_buffer.push_back(tev);
    }
  }

  ASSERT_EQ(track->midi_record_buffer.size(), 1)
      << "Virtual MIDI event should be captured in record buffer";
  EXPECT_EQ(track->midi_record_buffer[0].event.pitch, 60);
  EXPECT_TRUE(track->midi_record_buffer[0].event.isNoteOn);
  EXPECT_NEAR(track->midi_record_buffer[0].time_sec, 0.0, 0.001);

  // Now simulate a note-off + advance playhead
  state.playhead_pos_sec = 0.5;  // 1 beat later at 120 BPM
  {
    MidiNoteEvent noteOff;
    noteOff.sampleOffset = 0;
    noteOff.channel = 0;
    noteOff.pitch = 60;
    noteOff.velocity = 0.0f;
    noteOff.isNoteOn = false;

    std::lock_guard<std::mutex> mlock(track->virtual_midi_mutex);
    track->virtual_midi_queue.push_back(noteOff);
  }

  allEvents.clear();
  {
    std::lock_guard<std::mutex> mlock(track->virtual_midi_mutex);
    allEvents.insert(allEvents.end(), track->virtual_midi_queue.begin(),
                     track->virtual_midi_queue.end());
    track->virtual_midi_queue.clear();
  }

  if (state.is_recording && track->record_armed &&
      track->record_mode == Track::RecordMode::RECORD_MIDI) {
    for (const auto& ev : allEvents) {
      Track::TimestampedMidiEvent tev;
      tev.time_sec =
          state.playhead_pos_sec + ev.sampleOffset / state.sample_rate;
      tev.event = ev;
      track->midi_record_buffer.push_back(tev);
    }
  }

  ASSERT_EQ(track->midi_record_buffer.size(), 2)
      << "Should have note-on + note-off";
  EXPECT_EQ(track->midi_record_buffer[1].event.pitch, 60);
  EXPECT_FALSE(track->midi_record_buffer[1].event.isNoteOn);
  EXPECT_NEAR(track->midi_record_buffer[1].time_sec, 0.5, 0.001);

  // Verify the recording would produce correct beat positions
  double beats_per_sec = state.bpm / 60.0;
  double note_on_beat =
      (track->midi_record_buffer[0].time_sec - state.record_start_sec) *
      beats_per_sec;
  double note_off_beat =
      (track->midi_record_buffer[1].time_sec - state.record_start_sec) *
      beats_per_sec;

  EXPECT_NEAR(note_on_beat, 0.0, 0.01) << "Note-on should be at beat 0";
  EXPECT_NEAR(note_off_beat, 1.0, 0.01) << "Note-off should be at beat 1";

  // Clean up
  state.tracks.clear();
}

}  // namespace hibiki
