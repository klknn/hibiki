// Tests for timeline clip playback, clip moves, and MIDI event timing.
// These tests reproduce specific bugs to prevent regression.

#include <gtest/gtest.h>

#include <cmath>
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

}  // namespace hibiki
