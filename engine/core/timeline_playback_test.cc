// Tests for timeline clip playback, clip moves, and MIDI event timing.
// These tests reproduce specific bugs to prevent regression.

#include <gtest/gtest.h>

#include <cmath>
#include <set>
#include <vector>

#include "engine/core/commands.hpp"
#include "engine/core/track.hpp"
#include "engine/ipc/ipc.hpp"

namespace hibiki {

class TimelinePlaybackTest : public ::testing::Test {
 protected:
  hibiki::ProjectState state;
  hibiki::HistoryManager history;

  void SetUp() override {
    hibiki::g_ipc_enabled = false;
    state.bpm = 120.0;
    state.sample_rate = 44100.0;
  }

  // Helper: create a track with a MIDI timeline clip containing a note-on at
  // the given beat position.
  Track* createTrackWithMidiClip(int track_idx, double clip_start_sec,
                                 double note_beat,
                                 double duration_beats = 4.0) {
    auto* track = GetOrCreateTrack(state, track_idx);
    auto clip = std::make_unique<Clip>();
    clip->type = Clip::Type::MIDI;
    clip->duration_beats = duration_beats;
    // Add a note-on event at the specified beat
    MidiEvent me;
    me.beats = note_beat;
    me.type = 0x90;
    me.channel = 0;
    me.note = 60;
    me.velocity = 100;
    clip->midi_events.push_back(me);

    auto tc = std::make_unique<TimelineClip>();
    tc->start_time_sec = clip_start_sec;
    tc->duration_beats = duration_beats;
    tc->duration_sec = duration_beats * 60.0 / state.bpm;
    tc->clip = std::move(clip);
    track->timeline_clips.push_back(std::move(tc));
    return track;
  }

  // Helper: create a track with an audio timeline clip at the given position.
  Track* createTrackWithAudioClip(int track_idx, double clip_start_sec,
                                  double duration_sec) {
    auto* track = GetOrCreateTrack(state, track_idx);
    auto clip = std::make_unique<Clip>();
    clip->type = Clip::Type::AUDIO;
    clip->duration_sec = duration_sec;
    clip->num_channels = 2;
    clip->sample_rate = (int)state.sample_rate;
    // Fill with some audio data
    int total_samples = (int)(duration_sec * state.sample_rate) * 2;
    clip->audio_data.resize(total_samples, 0.5f);

    auto tc = std::make_unique<TimelineClip>();
    tc->start_time_sec = clip_start_sec;
    tc->duration_sec = duration_sec;
    tc->clip = std::move(clip);
    track->timeline_clips.push_back(std::move(tc));
    return track;
  }

  // Simulate the timeline MIDI event collection from main.cpp step 1b.
  // Returns collected MidiNoteEvent list for a single block.
  struct MidiNoteEvent {
    int sampleOffset;
    int channel;
    int pitch;
    bool isNoteOn;
    float velocity;
  };

  std::vector<MidiNoteEvent> collectTimelineMidiEvents(const Track* track,
                                                       double playhead_sec,
                                                       double time_per_block) {
    std::vector<MidiNoteEvent> events;
    int block_size = (int)(time_per_block * state.sample_rate);
    for (const auto& tc : track->timeline_clips) {
      if (!tc->clip || tc->clip->type != Clip::Type::MIDI) continue;
      double clip_duration = (tc->duration_beats > 0)
                                 ? tc->duration_beats * 60.0 / state.bpm
                                 : tc->duration_sec;
      if (playhead_sec + time_per_block > tc->start_time_sec &&
          playhead_sec < tc->start_time_sec + clip_duration) {
        double clip_local_time = playhead_sec - tc->start_time_sec;
        double beats_per_sec = state.bpm / 60.0;
        double window_start_beats = clip_local_time * beats_per_sec;
        double window_end_beats =
            (clip_local_time + time_per_block) * beats_per_sec;
        for (const auto& me : tc->clip->midi_events) {
          if (me.beats >= window_start_beats && me.beats < window_end_beats) {
            MidiNoteEvent e;
            double event_local_sec = me.beats / beats_per_sec - clip_local_time;
            e.sampleOffset =
                std::max(0, (int)(event_local_sec * state.sample_rate));
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
};

// ─── Bug 1: Clip move must update engine start_time_sec ─────────────

TEST_F(TimelinePlaybackTest, MoveTimelineClipSameTrack) {
  createTrackWithMidiClip(0, /*clip_start*/ 0.0, /*note_beat*/ 0.0);
  ASSERT_EQ(state.tracks[0]->timeline_clips.size(), 1);
  EXPECT_DOUBLE_EQ(state.tracks[0]->timeline_clips[0]->start_time_sec, 0.0);

  // Send move command: move clip to 4.0 seconds on same track
  pb::commands::TrackCmd cmd;
  cmd.set_action(pb::commands::TrackCmd::ACTION_MOVE_TIMELINE_CLIP);
  cmd.mutable_target()->set_track_index(0);
  cmd.mutable_target()->set_timeline_clip(0);
  cmd.set_value(4.0f);            // new start time in seconds
  cmd.set_target_track_index(0);  // same track

  handleTrackCmd(cmd, state, history);

  EXPECT_DOUBLE_EQ(state.tracks[0]->timeline_clips[0]->start_time_sec, 4.0);
  EXPECT_EQ(state.tracks[0]->timeline_clips.size(), 1);
}

TEST_F(TimelinePlaybackTest, MoveTimelineClipCrossTrack) {
  createTrackWithMidiClip(0, /*clip_start*/ 0.0, /*note_beat*/ 0.0);
  GetOrCreateTrack(state, 1);  // Create target track

  ASSERT_EQ(state.tracks[0]->timeline_clips.size(), 1);
  ASSERT_EQ(state.tracks[1]->timeline_clips.size(), 0);

  // Move clip from track 0 to track 1 at 2.0s
  pb::commands::TrackCmd cmd;
  cmd.set_action(pb::commands::TrackCmd::ACTION_MOVE_TIMELINE_CLIP);
  cmd.mutable_target()->set_track_index(0);
  cmd.mutable_target()->set_timeline_clip(0);
  cmd.set_value(2.0f);
  cmd.set_target_track_index(1);

  handleTrackCmd(cmd, state, history);

  // Source track should be empty, target should have the clip
  EXPECT_EQ(state.tracks[0]->timeline_clips.size(), 0);
  ASSERT_EQ(state.tracks[1]->timeline_clips.size(), 1);
  EXPECT_DOUBLE_EQ(state.tracks[1]->timeline_clips[0]->start_time_sec, 2.0);
}

TEST_F(TimelinePlaybackTest, MoveTimelineClipDoesNotDuplicate) {
  // Create track with 2 clips at different positions
  auto* track =
      createTrackWithMidiClip(0, /*clip_start*/ 0.0, /*note_beat*/ 0.0);
  {
    auto clip = std::make_unique<Clip>();
    clip->type = Clip::Type::MIDI;
    clip->duration_beats = 4.0;
    auto tc = std::make_unique<TimelineClip>();
    tc->start_time_sec = 4.0;
    tc->duration_beats = 4.0;
    tc->duration_sec = 2.0;
    tc->clip = std::move(clip);
    track->timeline_clips.push_back(std::move(tc));
  }
  ASSERT_EQ(track->timeline_clips.size(), 2);

  // Move clip 0 to position 8.0 (same track)
  pb::commands::TrackCmd cmd;
  cmd.set_action(pb::commands::TrackCmd::ACTION_MOVE_TIMELINE_CLIP);
  cmd.mutable_target()->set_track_index(0);
  cmd.mutable_target()->set_timeline_clip(0);
  cmd.set_value(8.0f);
  cmd.set_target_track_index(0);

  handleTrackCmd(cmd, state, history);

  // Should still have exactly 2 clips (no duplication or overlap)
  EXPECT_EQ(track->timeline_clips.size(), 2);
  // Clip at index 0 should now be at 8.0
  EXPECT_DOUBLE_EQ(track->timeline_clips[0]->start_time_sec, 8.0);
  // Clip at index 1 should remain at 4.0
  EXPECT_DOUBLE_EQ(track->timeline_clips[1]->start_time_sec, 4.0);
}

// ─── Bug 2: MIDI events must play at correct time relative to playhead ──

TEST_F(TimelinePlaybackTest, MidiEventsPlayAtCorrectTime) {
  // Place a MIDI clip at 2.0 seconds with a note at beat 0
  createTrackWithMidiClip(0, /*clip_start*/ 2.0, /*note_beat*/ 0.0);

  double time_per_block = 512.0 / state.sample_rate;

  // Before clip: no events should be collected
  auto events = collectTimelineMidiEvents(state.tracks[0].get(),
                                          /*playhead*/ 0.0, time_per_block);
  EXPECT_EQ(events.size(), 0) << "No events should play before clip start";

  // At clip start: note-on at beat 0 should fire
  events = collectTimelineMidiEvents(state.tracks[0].get(), /*playhead*/ 2.0,
                                     time_per_block);
  ASSERT_EQ(events.size(), 1)
      << "Note at beat 0 should play when playhead reaches clip start";
  EXPECT_EQ(events[0].pitch, 60);
  EXPECT_TRUE(events[0].isNoteOn);
  EXPECT_EQ(events[0].sampleOffset, 0);  // Should be at the very start

  // After clip ends (past 4 beats at 120bpm = 2 seconds)
  events = collectTimelineMidiEvents(state.tracks[0].get(), /*playhead*/ 5.0,
                                     time_per_block);
  EXPECT_EQ(events.size(), 0) << "No events should play after clip ends";
}

TEST_F(TimelinePlaybackTest, MidiEventsAfterClipMove) {
  // Create clip at 0.0s, then move to 4.0s
  createTrackWithMidiClip(0, /*clip_start*/ 0.0, /*note_beat*/ 0.0);

  pb::commands::TrackCmd cmd;
  cmd.set_action(pb::commands::TrackCmd::ACTION_MOVE_TIMELINE_CLIP);
  cmd.mutable_target()->set_track_index(0);
  cmd.mutable_target()->set_timeline_clip(0);
  cmd.set_value(4.0f);
  cmd.set_target_track_index(0);
  handleTrackCmd(cmd, state, history);

  double time_per_block = 512.0 / state.sample_rate;

  // Old position (0.0s): should NOT have any events
  auto events = collectTimelineMidiEvents(state.tracks[0].get(),
                                          /*playhead*/ 0.0, time_per_block);
  EXPECT_EQ(events.size(), 0) << "Old position should be silent after move";

  // New position (4.0s): should have the note
  events = collectTimelineMidiEvents(state.tracks[0].get(), /*playhead*/ 4.0,
                                     time_per_block);
  ASSERT_EQ(events.size(), 1) << "Note should play at new position after move";
  EXPECT_EQ(events[0].pitch, 60);
}

TEST_F(TimelinePlaybackTest, MidiNotAtBeatZeroPlaysCorrectly) {
  // Place a MIDI clip at 0.0s with a note at beat 2 (= 1 second at 120bpm)
  createTrackWithMidiClip(0, /*clip_start*/ 0.0, /*note_beat*/ 2.0,
                          /*duration_beats*/ 4.0);

  double time_per_block = 512.0 / state.sample_rate;

  // At playhead 0.0: note at beat 2 should NOT play yet
  auto events = collectTimelineMidiEvents(state.tracks[0].get(),
                                          /*playhead*/ 0.0, time_per_block);
  EXPECT_EQ(events.size(), 0) << "Note at beat 2 should not play at beat 0";

  // At playhead 1.0s (= beat 2 at 120bpm): note should play
  events = collectTimelineMidiEvents(state.tracks[0].get(), /*playhead*/ 1.0,
                                     time_per_block);
  ASSERT_EQ(events.size(), 1) << "Note at beat 2 should play at 1.0s";
  EXPECT_EQ(events[0].pitch, 60);
}

// ─── Bug 3: Audio clip playback after move ──────────────────────────

TEST_F(TimelinePlaybackTest, AudioClipPlaybackAfterMove) {
  createTrackWithAudioClip(0, /*clip_start*/ 0.0, /*duration*/ 2.0);

  // Move clip to 4.0s
  pb::commands::TrackCmd cmd;
  cmd.set_action(pb::commands::TrackCmd::ACTION_MOVE_TIMELINE_CLIP);
  cmd.mutable_target()->set_track_index(0);
  cmd.mutable_target()->set_timeline_clip(0);
  cmd.set_value(4.0f);
  cmd.set_target_track_index(0);
  handleTrackCmd(cmd, state, history);

  // Verify engine state
  ASSERT_EQ(state.tracks[0]->timeline_clips.size(), 1);
  EXPECT_DOUBLE_EQ(state.tracks[0]->timeline_clips[0]->start_time_sec, 4.0);

  // Simulate playback: at 0.0s, clip should NOT be active
  double playhead = 0.0;
  double time_per_block = 512.0 / state.sample_rate;
  auto& tc = state.tracks[0]->timeline_clips[0];
  bool active_at_old_pos = (playhead + time_per_block > tc->start_time_sec &&
                            playhead < tc->start_time_sec + tc->duration_sec);
  EXPECT_FALSE(active_at_old_pos)
      << "Audio clip should not play at old position";

  // At 4.0s, clip SHOULD be active
  playhead = 4.0;
  bool active_at_new_pos = (playhead + time_per_block > tc->start_time_sec &&
                            playhead < tc->start_time_sec + tc->duration_sec);
  EXPECT_TRUE(active_at_new_pos) << "Audio clip should play at new position";
}

// ─── Edge cases ─────────────────────────────────────────────────────

TEST_F(TimelinePlaybackTest, MoveInvalidClipIndex) {
  createTrackWithMidiClip(0, 0.0, 0.0);

  pb::commands::TrackCmd cmd;
  cmd.set_action(pb::commands::TrackCmd::ACTION_MOVE_TIMELINE_CLIP);
  cmd.mutable_target()->set_track_index(0);
  cmd.mutable_target()->set_timeline_clip(99);  // invalid index
  cmd.set_value(4.0f);
  cmd.set_target_track_index(0);

  EXPECT_NO_FATAL_FAILURE(handleTrackCmd(cmd, state, history));
  // Original clip should be unchanged
  EXPECT_DOUBLE_EQ(state.tracks[0]->timeline_clips[0]->start_time_sec, 0.0);
}

TEST_F(TimelinePlaybackTest, MoveInvalidTrack) {
  createTrackWithMidiClip(0, 0.0, 0.0);

  pb::commands::TrackCmd cmd;
  cmd.set_action(pb::commands::TrackCmd::ACTION_MOVE_TIMELINE_CLIP);
  cmd.mutable_target()->set_track_index(99);  // invalid track
  cmd.mutable_target()->set_timeline_clip(0);
  cmd.set_value(4.0f);
  cmd.set_target_track_index(0);

  EXPECT_NO_FATAL_FAILURE(handleTrackCmd(cmd, state, history));
  // Original clip should be unchanged
  EXPECT_DOUBLE_EQ(state.tracks[0]->timeline_clips[0]->start_time_sec, 0.0);
}

// ─── Bug 4: MIDI recording timing — simulate record→stop→playback ──

TEST_F(TimelinePlaybackTest, MidiRecordingTimingAtBarStart) {
  // Simulate: start recording at beat 0, play a note immediately, stop
  auto* track = GetOrCreateTrack(state, 0);
  track->record_armed = true;
  track->record_mode = Track::RecordMode::RECORD_MIDI;

  // Start recording via transport command
  pb::commands::TransportCmd rec_cmd;
  rec_cmd.set_action(pb::commands::TransportCmd::ACTION_RECORD);
  handleTransportCmd(rec_cmd, state);

  // record_start_sec should be at playhead (0.0)
  EXPECT_DOUBLE_EQ(state.record_start_sec, 0.0);
  EXPECT_TRUE(state.is_recording);
  EXPECT_TRUE(state.is_timeline_playing);

  // Simulate: user plays a note 0.1 seconds after record start
  // In the real code, this gets captured as:
  //   tev.time_sec = playhead_pos_sec + sampleOffset/sr
  Track::TimestampedMidiEvent tev;
  tev.time_sec = 0.1;  // 0.1s after start
  tev.event.sampleOffset = 0;
  tev.event.channel = 0;
  tev.event.pitch = 60;
  tev.event.velocity = 0.8f;
  tev.event.isNoteOn = true;
  track->midi_record_buffer.push_back(tev);

  // Note-off at 0.5s
  Track::TimestampedMidiEvent tev_off;
  tev_off.time_sec = 0.5;
  tev_off.event.sampleOffset = 0;
  tev_off.event.channel = 0;
  tev_off.event.pitch = 60;
  tev_off.event.velocity = 0.0f;
  tev_off.event.isNoteOn = false;
  track->midi_record_buffer.push_back(tev_off);

  // Advance playhead past recording
  state.playhead_pos_sec = 2.0;

  // Stop recording via transport command
  pb::commands::TransportCmd stop_cmd;
  stop_cmd.set_action(pb::commands::TransportCmd::ACTION_STOP);
  handleTransportCmd(stop_cmd, state);

  // Verify the recorded clip was created
  ASSERT_EQ(track->timeline_clips.size(), 1);
  auto& tc = track->timeline_clips[0];

  // KEY CHECK: clip must start at record_start_sec (0.0)
  EXPECT_DOUBLE_EQ(tc->start_time_sec, 0.0)
      << "Recorded clip should start at the record start position";

  // Verify the MIDI events have correct beat positions
  ASSERT_GE(tc->clip->midi_events.size(), 1);
  double beats_per_sec = state.bpm / 60.0;
  double expected_note_beat = 0.1 * beats_per_sec;  // 0.2 beats at 120bpm
  EXPECT_NEAR(tc->clip->midi_events[0].beats, expected_note_beat, 0.001)
      << "First note should be at beat " << expected_note_beat
      << " (0.1s after record start)";

  // Now verify playback: the note should play at exactly 0.1s
  double time_per_block = 512.0 / state.sample_rate;
  auto events =
      collectTimelineMidiEvents(track, /*playhead*/ 0.1, time_per_block);
  ASSERT_EQ(events.size(), 1)
      << "Note should play back at 0.1s (time of original recording)";
  EXPECT_EQ(events[0].pitch, 60);

  // The note should NOT play at 0.0s (before the note was recorded)
  events = collectTimelineMidiEvents(track, /*playhead*/ 0.0, time_per_block);
  EXPECT_EQ(events.size(), 0)
      << "Note should not play at 0.0s (before it was recorded)";

  // The note should NOT play 1 bar late (at 2.1s = 0.1s + 1 bar)
  events = collectTimelineMidiEvents(track, /*playhead*/ 2.1, time_per_block);
  EXPECT_EQ(events.size(), 0)
      << "Note should NOT play 1 bar late — this reproduces the delay bug";
}

TEST_F(TimelinePlaybackTest, MidiRecordingTimingAtMidTimeline) {
  // Simulate: start recording at beat 8 (= 4.0s at 120bpm)
  state.playhead_pos_sec = 4.0;
  auto* track = GetOrCreateTrack(state, 0);
  track->record_armed = true;
  track->record_mode = Track::RecordMode::RECORD_MIDI;

  // Start recording
  pb::commands::TransportCmd rec_cmd;
  rec_cmd.set_action(pb::commands::TransportCmd::ACTION_RECORD);
  handleTransportCmd(rec_cmd, state);
  EXPECT_DOUBLE_EQ(state.record_start_sec, 4.0);

  // User plays a note immediately at record start
  Track::TimestampedMidiEvent tev;
  tev.time_sec = 4.0;
  tev.event.sampleOffset = 0;
  tev.event.channel = 0;
  tev.event.pitch = 64;
  tev.event.velocity = 0.7f;
  tev.event.isNoteOn = true;
  track->midi_record_buffer.push_back(tev);

  Track::TimestampedMidiEvent tev_off;
  tev_off.time_sec = 4.5;
  tev_off.event.sampleOffset = 0;
  tev_off.event.channel = 0;
  tev_off.event.pitch = 64;
  tev_off.event.velocity = 0.0f;
  tev_off.event.isNoteOn = false;
  track->midi_record_buffer.push_back(tev_off);

  state.playhead_pos_sec = 6.0;

  // Stop recording
  pb::commands::TransportCmd stop_cmd;
  stop_cmd.set_action(pb::commands::TransportCmd::ACTION_STOP);
  handleTransportCmd(stop_cmd, state);

  ASSERT_EQ(track->timeline_clips.size(), 1);
  auto& tc = track->timeline_clips[0];

  // Clip should start at 4.0s (where recording began)
  EXPECT_DOUBLE_EQ(tc->start_time_sec, 4.0)
      << "Clip should start at the record start position (4.0s)";

  // First note should be at beat 0 (immediately at record start)
  EXPECT_NEAR(tc->clip->midi_events[0].beats, 0.0, 0.001)
      << "Note played at record start should be at beat 0";

  // Playback: note should play at 4.0s
  double time_per_block = 512.0 / state.sample_rate;
  auto events =
      collectTimelineMidiEvents(track, /*playhead*/ 4.0, time_per_block);
  ASSERT_EQ(events.size(), 1)
      << "Note should play at 4.0s (start of recording)";
  EXPECT_EQ(events[0].pitch, 64);
}

// ─── Bug 5: Trim-aware MIDI loop wrapping ───────────────────────────

// Helper that matches the actual loop-aware MIDI collection logic in main.cpp
struct LoopMidiEvent {
  int sampleOffset;
  int pitch;
  bool isNoteOn;
};

std::vector<LoopMidiEvent> collectLoopMidiEvents(
    const Track* track, double playhead_sec, double time_per_block,
    double bpm, double sample_rate) {
  std::vector<LoopMidiEvent> events;
  int block_size = (int)(time_per_block * sample_rate);
  for (const auto& tc : track->timeline_clips) {
    if (!tc->clip || tc->clip->type != Clip::Type::MIDI) continue;
    double clip_duration = (tc->duration_beats > 0)
                               ? tc->duration_beats * 60.0 / bpm
                               : tc->duration_sec;
    if (playhead_sec + time_per_block > tc->start_time_sec &&
        playhead_sec < tc->start_time_sec + clip_duration) {
      double clip_local_time = playhead_sec - tc->start_time_sec;
      double beats_per_sec = bpm / 60.0;
      double content_dur_beats = tc->clip->duration_beats;
      double loop_length_beats =
          (tc->loop_interval_beats > 0)
              ? tc->loop_interval_beats
              : (content_dur_beats - tc->trim_start_beats);
      double window_start_beats =
          clip_local_time * beats_per_sec + tc->trim_start_beats;
      double window_end_beats =
          (clip_local_time + time_per_block) * beats_per_sec +
          tc->trim_start_beats;
      // Loop wrapping for MIDI within trimmed range
      if (tc->clip->is_loop && loop_length_beats > 0) {
        window_start_beats = tc->trim_start_beats +
            std::fmod(window_start_beats - tc->trim_start_beats,
                      loop_length_beats);
        window_end_beats =
            window_start_beats + time_per_block * beats_per_sec;
      }
      for (const auto& me : tc->clip->midi_events) {
        double me_beats = me.beats;
        // Skip events outside the trimmed content range
        if (me_beats < tc->trim_start_beats ||
            me_beats >= content_dur_beats)
          continue;
        double loop_end_beats = tc->trim_start_beats + loop_length_beats;
        if (tc->clip->is_loop && loop_length_beats > 0 &&
            window_end_beats > loop_end_beats) {
          bool in_range =
              (me_beats >= window_start_beats && me_beats < window_end_beats) ||
              (me_beats >= tc->trim_start_beats &&
               me_beats < tc->trim_start_beats +
                   std::fmod(window_end_beats - tc->trim_start_beats,
                             loop_length_beats));
          if (!in_range) continue;
        } else {
          if (me_beats < window_start_beats || me_beats >= window_end_beats)
            continue;
        }
        LoopMidiEvent e;
        double event_local_sec = me.beats / beats_per_sec - clip_local_time;
        if (tc->clip->is_loop && loop_length_beats > 0) {
          double rel_beat = me.beats - tc->trim_start_beats;
          double loop_length_sec = loop_length_beats / beats_per_sec;
          double local_in_content =
              std::fmod(clip_local_time, loop_length_sec);
          event_local_sec = rel_beat / beats_per_sec - local_in_content;
          if (event_local_sec < 0) event_local_sec += loop_length_sec;
        }
        e.sampleOffset = std::max(0, (int)(event_local_sec * sample_rate));
        if (e.sampleOffset >= block_size) e.sampleOffset = block_size - 1;
        e.pitch = me.note;
        e.isNoteOn = hibiki::isNoteOn(me);
        events.push_back(e);
      }
    }
  }
  return events;
}

TEST_F(TimelinePlaybackTest, MidiLoopWrapsWithinTrimmedRange) {
  // Create a 4-beat MIDI clip with notes at beat 0, 1, 2, 3
  // Trim first 2 beats: only beats 2,3 should play and loop (loop_length=2)
  auto* track = GetOrCreateTrack(state, 0);
  auto clip = std::make_unique<Clip>();
  clip->type = Clip::Type::MIDI;
  clip->duration_beats = 4.0;
  clip->is_loop = true;
  for (int i = 0; i < 4; i++) {
    MidiEvent me;
    me.beats = (double)i;
    me.type = 0x90;
    me.channel = 0;
    me.note = 60 + i;
    me.velocity = 100;
    clip->midi_events.push_back(me);
  }
  auto tc = std::make_unique<TimelineClip>();
  tc->start_time_sec = 0.0;
  tc->duration_beats = 8.0;  // looped: 2-beat content × 4 repeats
  tc->duration_sec = 8.0 * 60.0 / state.bpm;
  tc->trim_start_beats = 2.0;  // Trim first 2 beats
  tc->clip = std::move(clip);
  track->timeline_clips.push_back(std::move(tc));

  double time_per_block = 512.0 / state.sample_rate;

  // At playhead 0.0s: first block of trimmed loop starts at beat 2
  // Note at beat 2 (pitch 62) should play
  auto events = collectLoopMidiEvents(
      track, 0.0, time_per_block, state.bpm, state.sample_rate);
  ASSERT_EQ(events.size(), 1) << "Note at beat 2 should play at start";
  EXPECT_EQ(events[0].pitch, 62) << "Should be the note at beat 2";

  // At playhead corresponding to beat 3 (0.5s at 120bpm):
  // Note at beat 3 (pitch 63) should play
  events = collectLoopMidiEvents(
      track, 0.5, time_per_block, state.bpm, state.sample_rate);
  ASSERT_EQ(events.size(), 1) << "Note at beat 3 should play at 0.5s";
  EXPECT_EQ(events[0].pitch, 63);

  // Second repetition: loop length = 2 beats = 1.0s
  // At playhead 1.0s: should wrap back to beat 2 (note 62)
  events = collectLoopMidiEvents(
      track, 1.0, time_per_block, state.bpm, state.sample_rate);
  ASSERT_EQ(events.size(), 1) << "Loop should repeat note at beat 2";
  EXPECT_EQ(events[0].pitch, 62) << "Second repetition should be beat 2 note";
}

TEST_F(TimelinePlaybackTest, TrimmedMidiEventsBeforeTrimStartExcluded) {
  // Clip with notes at beats 0,1,2,3 trimmed at beat 2
  // Notes at 0 and 1 should NEVER play (even without loop)
  auto* track = GetOrCreateTrack(state, 0);
  auto clip = std::make_unique<Clip>();
  clip->type = Clip::Type::MIDI;
  clip->duration_beats = 4.0;
  clip->is_loop = false;
  for (int i = 0; i < 4; i++) {
    MidiEvent me;
    me.beats = (double)i;
    me.type = 0x90;
    me.channel = 0;
    me.note = 60 + i;
    me.velocity = 100;
    clip->midi_events.push_back(me);
  }
  auto tc = std::make_unique<TimelineClip>();
  tc->start_time_sec = 0.0;
  tc->duration_beats = 2.0;  // visible: beats 2-4 = 2 beats long
  tc->duration_sec = 2.0 * 60.0 / state.bpm;
  tc->trim_start_beats = 2.0;
  tc->clip = std::move(clip);
  track->timeline_clips.push_back(std::move(tc));

  double time_per_block = 512.0 / state.sample_rate;

  // Sweep through the clip: collect all events
  std::set<int> pitches_heard;
  for (double t = 0.0; t < 1.0; t += time_per_block) {
    auto events = collectLoopMidiEvents(
        track, t, time_per_block, state.bpm, state.sample_rate);
    for (auto& e : events) pitches_heard.insert(e.pitch);
  }

  EXPECT_EQ(pitches_heard.count(60), 0) << "Beat 0 note should be excluded";
  EXPECT_EQ(pitches_heard.count(61), 0) << "Beat 1 note should be excluded";
  EXPECT_EQ(pitches_heard.count(62), 1) << "Beat 2 note should play";
  EXPECT_EQ(pitches_heard.count(63), 1) << "Beat 3 note should play";
}

TEST_F(TimelinePlaybackTest, AudioLoopWrapsWithinTrimmedRange) {
  // Create a 2-second audio clip, trim 1s from start, loop
  // Loop should play samples [trim..end] repeatedly
  auto* track = GetOrCreateTrack(state, 0);
  auto clip = std::make_unique<Clip>();
  clip->type = Clip::Type::AUDIO;
  clip->duration_sec = 2.0;
  clip->num_channels = 1;
  clip->sample_rate = (int)state.sample_rate;
  int total_samples = (int)(2.0 * state.sample_rate);
  // Fill: first half = 0.0, second half = 1.0
  clip->audio_data.resize(total_samples, 0.0f);
  int half = total_samples / 2;
  for (int i = half; i < total_samples; i++) {
    clip->audio_data[i] = 1.0;
  }
  clip->is_loop = true;

  auto tc = std::make_unique<TimelineClip>();
  tc->start_time_sec = 0.0;
  tc->duration_sec = 4.0;  // looped: 1s content × 4
  tc->trim_start_beats = 2.0;  // At 120bpm: 2 beats = 1 second = half the clip
  tc->clip = std::move(clip);
  track->timeline_clips.push_back(std::move(tc));

  // Simulate audio playback at time 0 (should read from trimmed region = 1.0)
  auto& stored_tc = track->timeline_clips[0];
  auto& stored_clip = stored_tc->clip;
  int block_size = 256;
  double time_per_block = block_size / state.sample_rate;
  double clip_local_time = 0.0;
  double bps = state.bpm / 60.0;
  int trim_samples = (bps > 0) ? (int)(stored_tc->trim_start_beats / bps * state.sample_rate) : 0;
  int content_samples = total_samples;
  int loop_len = content_samples - trim_samples;
  int start_sample = trim_samples + (int)(clip_local_time * state.sample_rate);

  // Sample from the start of the trim — should be 1.0 (second half)
  int sample_pos = start_sample;
  if (stored_clip->is_loop && loop_len > 0) {
    sample_pos = trim_samples + ((sample_pos - trim_samples) % loop_len);
  }
  EXPECT_GE(sample_pos, half) << "Trimmed loop should start in second half";
  EXPECT_FLOAT_EQ(stored_clip->audio_data[sample_pos], 1.0)
      << "Audio at loop start should be from trimmed region (value 1.0)";
}

TEST_F(TimelinePlaybackTest, SetClipLoopViaCommand) {
  // Create a MIDI clip and verify SET_CLIP_LOOP sets is_loop correctly
  createTrackWithMidiClip(0, 0.0, 0.0, 4.0);
  ASSERT_FALSE(state.tracks[0]->timeline_clips[0]->clip->is_loop);

  pb::commands::TrackCmd cmd;
  cmd.set_action(pb::commands::TrackCmd::ACTION_SET_CLIP_LOOP);
  cmd.mutable_target()->set_track_index(0);
  cmd.mutable_target()->set_timeline_clip(0);
  cmd.set_flag(true);
  handleTrackCmd(cmd, state, history);

  EXPECT_TRUE(state.tracks[0]->timeline_clips[0]->clip->is_loop)
      << "is_loop should be true after SET_CLIP_LOOP";

  // Disable loop
  cmd.set_flag(false);
  handleTrackCmd(cmd, state, history);
  EXPECT_FALSE(state.tracks[0]->timeline_clips[0]->clip->is_loop)
      << "is_loop should be false after disabling loop";
}

// ─── Bug 6: Trimmed clip should produce dense repeats ───────────────
// Scenario: 4-beat MIDI clip trimmed to 1 beat, loop_interval=1 beat,
// looped to 4 beats → note repeats at 0s, 0.5s, 1.0s, 1.5s

TEST_F(TimelinePlaybackTest, TrimmedClipDenseRepeat) {
  auto* track = GetOrCreateTrack(state, 0);
  auto clip = std::make_unique<Clip>();
  clip->type = Clip::Type::MIDI;
  clip->duration_beats = 4.0;
  clip->is_loop = true;
  // Single note at beat 0
  MidiEvent me;
  me.beats = 0.0;
  me.type = 0x90;
  me.channel = 0;
  me.note = 60;
  me.velocity = 100;
  clip->midi_events.push_back(me);

  auto tc = std::make_unique<TimelineClip>();
  tc->start_time_sec = 0.0;
  tc->duration_beats = 4.0;  // Total looped duration: 4 beats (2.0s)
  tc->duration_sec = 4.0 * 60.0 / state.bpm;  // 2.0s
  tc->trim_start_beats = 0.0;
  tc->loop_interval_beats = 1.0;  // Repeat every 1 beat (0.5s at 120bpm)
  tc->clip = std::move(clip);
  track->timeline_clips.push_back(std::move(tc));

  double time_per_block = 512.0 / state.sample_rate;
  // At 120bpm: 1 beat = 0.5 seconds
  // Note should fire at playhead 0.0, 0.5, 1.0, 1.5 (four times total)
  std::vector<double> expected_times = {0.0, 0.5, 1.0, 1.5};
  int found = 0;
  for (double t : expected_times) {
    auto events = collectLoopMidiEvents(
        track, t, time_per_block, state.bpm, state.sample_rate);
    EXPECT_GE(events.size(), 1)
        << "Note should play at t=" << t << "s (dense repeat)";
    if (!events.empty()) {
      EXPECT_EQ(events[0].pitch, 60);
      found++;
    }
  }
  EXPECT_EQ(found, 4) << "Note should repeat exactly 4 times";

  // Between repeats (at 0.25s = beat 0.5): no note should play
  auto events = collectLoopMidiEvents(
      track, 0.25, time_per_block, state.bpm, state.sample_rate);
  EXPECT_EQ(events.size(), 0)
      << "No note should play between repeat boundaries";
}

// ─── Bug 7: Padded clip should produce sparse repeats ───────────────
// Scenario: 1-beat audio clip padded to 4 beats, loop_interval=4 beats,
// looped to 8 beats → audio plays in first 0.5s of each 2s period,
// silence fills the remaining 1.5s of each period.

TEST_F(TimelinePlaybackTest, PaddedClipSparseRepeat) {
  auto* track = GetOrCreateTrack(state, 0);
  auto clip = std::make_unique<Clip>();
  clip->type = Clip::Type::AUDIO;
  // 1-beat audio clip: 0.5s at 120bpm
  double content_sec = 0.5;
  clip->duration_sec = content_sec;
  clip->num_channels = 1;
  clip->sample_rate = (int)state.sample_rate;
  int content_samples = (int)(content_sec * state.sample_rate);
  // Fill content with 1.0
  clip->audio_data.resize(content_samples, 1.0f);
  clip->is_loop = true;

  auto tc = std::make_unique<TimelineClip>();
  tc->start_time_sec = 0.0;
  // Padded to 4 beats (2.0s), then looped to 8 beats (4.0s)
  tc->duration_sec = 4.0;
  tc->duration_beats = 8.0;
  tc->trim_start_beats = 0.0;
  tc->loop_interval_beats = 4.0;  // Repeat every 4 beats (2.0s) — includes padding
  tc->clip = std::move(clip);
  track->timeline_clips.push_back(std::move(tc));

  auto& stored_tc = track->timeline_clips[0];
  auto& stored_clip = stored_tc->clip;
  double bps = state.bpm / 60.0;

  // Helper: compute sample position for a given playhead time
  auto getSampleAt = [&](double playhead_sec) -> int {
    double clip_local = playhead_sec - stored_tc->start_time_sec;
    int trim_samp = (bps > 0) ? (int)(stored_tc->trim_start_beats / bps *
                                       state.sample_rate) : 0;
    int start_samp = trim_samp + (int)(clip_local * state.sample_rate);
    int loop_len = (stored_tc->loop_interval_beats > 0 && bps > 0)
        ? (int)(stored_tc->loop_interval_beats / bps * state.sample_rate)
        : content_samples - trim_samp;
    if (stored_clip->is_loop && loop_len > 0) {
      start_samp = trim_samp + ((start_samp - trim_samp) % loop_len);
    }
    return start_samp;
  };

  // First repetition: t=0.0s → sample 0 (audio region, value=1.0)
  int s0 = getSampleAt(0.0);
  EXPECT_EQ(s0, 0);
  EXPECT_FLOAT_EQ(stored_clip->audio_data[s0], 1.0f)
      << "First rep start should have audio";

  // First repetition: t=0.25s → sample in audio region (within 0.5s content)
  int s1 = getSampleAt(0.25);
  EXPECT_LT(s1, content_samples)
      << "t=0.25s should be in audio region";
  EXPECT_FLOAT_EQ(stored_clip->audio_data[s1], 1.0f)
      << "Audio region should have content";

  // First repetition: t=1.0s → sample at 1.0s within 2.0s loop period
  // This is past the 0.5s content → should be in the padding region
  // (sample index exceeds audio_data size → engine should output silence)
  int s2 = getSampleAt(1.0);
  EXPECT_GE(s2, content_samples)
      << "t=1.0s should be in padding region (past content)";

  // Second repetition: t=2.0s → wraps to start of loop interval
  // Should be back at sample 0 (audio region again)
  int s3 = getSampleAt(2.0);
  EXPECT_EQ(s3, 0)
      << "t=2.0s should wrap to start of loop (second repetition)";

  // Second repetition: t=3.0s → 1.0s into second period → padding
  int s4 = getSampleAt(3.0);
  EXPECT_GE(s4, content_samples)
      << "t=3.0s should be in padding region of second repetition";
}

}  // namespace hibiki

