#include "engine/commands/commands.hpp"

#include <gtest/gtest.h>

#include "engine/core/clip.hpp"
#include "engine/core/track.hpp"
#include "engine/ipc/ipc.hpp"
#include "engine/test_utils.hpp"

namespace hibiki {

class CommandsTest : public ::testing::Test {
 protected:
  hibiki::ProjectState state;
  hibiki::HistoryManager history;

  void SetUp() override {
    hibiki::g_ipc_enabled = false;
    state.bpm = 120.0;
    state.sample_rate = 44100.0;
  }
};

// ─── Project commands ───────────────────────────────────────────────

TEST_F(CommandsTest, SetBpm) {
  // Create a track with timeline clip and automation clip
  auto track = hibiki::GetOrCreateTrack(state, 0);
  track->AddTimelineClip("", 2.0, state.bpm, 4.0, state.sample_rate);

  // Add an automation lane and clip
  hibiki::pb::commands::AutomationCmd add_lane;
  add_lane.set_action(hibiki::pb::commands::AutomationCmd::ACTION_ADD_LANE);
  add_lane.mutable_target()->set_track_index(0);
  add_lane.mutable_target()->set_plugin_index(0);
  add_lane.set_param_id(1);
  hibiki::handleAutomationCmd(add_lane, state, history);

  hibiki::pb::commands::AutomationCmd add_clip;
  add_clip.set_action(hibiki::pb::commands::AutomationCmd::ACTION_ADD_CLIP);
  add_clip.mutable_target()->set_track_index(0);
  add_clip.mutable_target()->set_lane_index(0);
  add_clip.set_start_time_sec(3.0);
  add_clip.set_duration_beats(2.0);
  hibiki::handleAutomationCmd(add_clip, state, history);

  // Set transport parameters
  state.playhead_pos_sec = 4.0;
  state.loop_start_sec = 1.0;
  state.loop_end_sec = 5.0;

  hibiki::pb::commands::ProjectCmd cmd;
  cmd.set_action(hibiki::pb::commands::ProjectCmd::ACTION_SET_BPM);
  cmd.set_bpm(60.0f);  // 120 -> 60 (factor of 2.0)

  hibiki::handleProjectCmd(cmd, state, history);
  EXPECT_FLOAT_EQ(state.bpm, 60.0f);

  // Verify that all positions/times are scaled by old_bpm/new_bpm = 120/60
  // = 2.0
  EXPECT_DOUBLE_EQ(track->timeline_clips[0]->start_time_sec, 4.0);
  ASSERT_EQ(track->automation_lanes.size(), 1u);
  ASSERT_EQ(track->automation_lanes[0].clips.size(), 1u);
  EXPECT_DOUBLE_EQ(track->automation_lanes[0].clips[0]->start_time_sec, 6.0);

  EXPECT_DOUBLE_EQ(state.playhead_pos_sec, 8.0);
  EXPECT_DOUBLE_EQ(state.loop_start_sec, 2.0);
  EXPECT_DOUBLE_EQ(state.loop_end_sec, 10.0);
}

TEST_F(CommandsTest, Quit) {
  hibiki::pb::commands::ProjectCmd cmd;
  cmd.set_action(hibiki::pb::commands::ProjectCmd::ACTION_QUIT);

  EXPECT_FALSE(state.quit);
  hibiki::handleProjectCmd(cmd, state, history);
  EXPECT_TRUE(state.quit);
}

// ─── Transport commands ─────────────────────────────────────────────

TEST_F(CommandsTest, PlayAndStop) {
  hibiki::pb::commands::TransportCmd play;
  play.set_action(hibiki::pb::commands::TransportCmd::ACTION_PLAY);

  EXPECT_FALSE(state.is_timeline_playing);
  hibiki::handleTransportCmd(play, state);
  EXPECT_TRUE(state.is_timeline_playing);

  hibiki::pb::commands::TransportCmd stop;
  stop.set_action(hibiki::pb::commands::TransportCmd::ACTION_STOP);
  hibiki::handleTransportCmd(stop, state);
  EXPECT_FALSE(state.is_timeline_playing);
}

TEST_F(CommandsTest, Seek) {
  hibiki::pb::commands::TransportCmd cmd;
  cmd.set_action(hibiki::pb::commands::TransportCmd::ACTION_SEEK);
  cmd.set_seek_pos(5.0f);

  hibiki::handleTransportCmd(cmd, state);
  EXPECT_FLOAT_EQ(state.playhead_pos_sec, 5.0f);
}

// ─── Automation commands ────────────────────────────────────────────

TEST_F(CommandsTest, AddAndRemoveAutomationLane) {
  // Create a track first
  hibiki::GetOrCreateTrack(state, 0);

  // Add lane
  hibiki::pb::commands::AutomationCmd add;
  add.set_action(hibiki::pb::commands::AutomationCmd::ACTION_ADD_LANE);
  add.mutable_target()->set_track_index(0);
  add.mutable_target()->set_plugin_index(0);
  add.set_param_id(1);

  hibiki::handleAutomationCmd(add, state, history);
  ASSERT_EQ(state.tracks[0]->automation_lanes.size(), 1);
  EXPECT_EQ(state.tracks[0]->automation_lanes[0].param_id, 1u);

  // Remove lane
  hibiki::pb::commands::AutomationCmd rm;
  rm.set_action(hibiki::pb::commands::AutomationCmd::ACTION_REMOVE_LANE);
  rm.mutable_target()->set_track_index(0);
  rm.mutable_target()->set_lane_index(0);

  hibiki::handleAutomationCmd(rm, state, history);
  EXPECT_EQ(state.tracks[0]->automation_lanes.size(), 0);
}

TEST_F(CommandsTest, AddAutomationClip) {
  auto track = hibiki::GetOrCreateTrack(state, 0);
  track->AddAutomationLane(0, 1);

  hibiki::pb::commands::AutomationCmd cmd;
  cmd.set_action(hibiki::pb::commands::AutomationCmd::ACTION_ADD_CLIP);
  cmd.mutable_target()->set_track_index(0);
  cmd.mutable_target()->set_lane_index(0);
  cmd.set_start_time_sec(1.0f);
  cmd.set_duration_beats(4.0f);

  hibiki::handleAutomationCmd(cmd, state, history);

  auto& lane = track->automation_lanes[0];
  ASSERT_EQ(lane.clips.size(), 1);
  EXPECT_FLOAT_EQ(lane.clips[0]->start_time_sec, 1.0f);
  EXPECT_FLOAT_EQ(lane.clips[0]->duration_beats, 4.0f);
  EXPECT_EQ(lane.clips[0]->clip->type, hibiki::Clip::Type::AUTOMATION);
}

TEST_F(CommandsTest, RemoveAutomationClip) {
  auto track = hibiki::GetOrCreateTrack(state, 0);
  track->AddAutomationLane(0, 1);
  // Add a clip directly
  auto tc = std::make_unique<hibiki::TimelineClip>();
  tc->clip = std::make_unique<hibiki::Clip>();
  tc->clip->type = hibiki::Clip::Type::AUTOMATION;
  tc->start_time_sec = 2.0;
  tc->duration_beats = 4.0;
  track->automation_lanes[0].clips.push_back(std::move(tc));

  ASSERT_EQ(track->automation_lanes[0].clips.size(), 1);

  hibiki::pb::commands::AutomationCmd cmd;
  cmd.set_action(hibiki::pb::commands::AutomationCmd::ACTION_REMOVE_CLIP);
  cmd.mutable_target()->set_track_index(0);
  cmd.mutable_target()->set_lane_index(0);
  cmd.set_clip_index(0);

  hibiki::handleAutomationCmd(cmd, state, history);
  EXPECT_EQ(track->automation_lanes[0].clips.size(), 0);
}

TEST_F(CommandsTest, MoveAutomationClip) {
  auto track = hibiki::GetOrCreateTrack(state, 0);
  track->AddAutomationLane(0, 1);
  auto tc = std::make_unique<hibiki::TimelineClip>();
  tc->clip = std::make_unique<hibiki::Clip>();
  tc->clip->type = hibiki::Clip::Type::AUTOMATION;
  tc->start_time_sec = 1.0;
  tc->duration_beats = 4.0;
  track->automation_lanes[0].clips.push_back(std::move(tc));

  hibiki::pb::commands::AutomationCmd cmd;
  cmd.set_action(hibiki::pb::commands::AutomationCmd::ACTION_MOVE_CLIP);
  cmd.mutable_target()->set_track_index(0);
  cmd.mutable_target()->set_lane_index(0);
  cmd.set_clip_index(0);
  cmd.set_start_time_sec(5.0f);

  hibiki::handleAutomationCmd(cmd, state, history);
  EXPECT_FLOAT_EQ(track->automation_lanes[0].clips[0]->start_time_sec, 5.0f);
}

TEST_F(CommandsTest, ResizeAutomationClip) {
  auto track = hibiki::GetOrCreateTrack(state, 0);
  track->AddAutomationLane(0, 1);
  auto tc = std::make_unique<hibiki::TimelineClip>();
  tc->clip = std::make_unique<hibiki::Clip>();
  tc->clip->type = hibiki::Clip::Type::AUTOMATION;
  tc->start_time_sec = 0.0;
  tc->duration_beats = 4.0;
  track->automation_lanes[0].clips.push_back(std::move(tc));

  hibiki::pb::commands::AutomationCmd cmd;
  cmd.set_action(hibiki::pb::commands::AutomationCmd::ACTION_RESIZE_CLIP);
  cmd.mutable_target()->set_track_index(0);
  cmd.mutable_target()->set_lane_index(0);
  cmd.set_clip_index(0);
  cmd.set_duration_beats(8.0f);

  hibiki::handleAutomationCmd(cmd, state, history);
  EXPECT_FLOAT_EQ(track->automation_lanes[0].clips[0]->duration_beats, 8.0f);
  EXPECT_FLOAT_EQ(track->automation_lanes[0].clips[0]->clip->duration_beats,
                  8.0f);
}

TEST_F(CommandsTest, RenameAutomationClip) {
  auto track = hibiki::GetOrCreateTrack(state, 0);
  track->AddAutomationLane(0, 1);
  auto tc = std::make_unique<hibiki::TimelineClip>();
  tc->clip = std::make_unique<hibiki::Clip>();
  tc->clip->type = hibiki::Clip::Type::AUTOMATION;
  tc->clip->name = "Old Name";
  track->automation_lanes[0].clips.push_back(std::move(tc));

  hibiki::pb::commands::AutomationCmd cmd;
  cmd.set_action(hibiki::pb::commands::AutomationCmd::ACTION_RENAME_CLIP);
  cmd.mutable_target()->set_track_index(0);
  cmd.mutable_target()->set_lane_index(0);
  cmd.set_clip_index(0);
  cmd.set_clip_name("Volume Swell");

  hibiki::handleAutomationCmd(cmd, state, history);
  EXPECT_EQ(track->automation_lanes[0].clips[0]->clip->name, "Volume Swell");
}

TEST_F(CommandsTest, UpdateAutomationPoints) {
  auto track = hibiki::GetOrCreateTrack(state, 0);
  track->AddAutomationLane(0, 1);
  auto tc = std::make_unique<hibiki::TimelineClip>();
  tc->clip = std::make_unique<hibiki::Clip>();
  tc->clip->type = hibiki::Clip::Type::AUTOMATION;
  track->automation_lanes[0].clips.push_back(std::move(tc));

  hibiki::pb::commands::AutomationCmd cmd;
  cmd.set_action(hibiki::pb::commands::AutomationCmd::ACTION_UPDATE_POINTS);
  cmd.mutable_target()->set_track_index(0);
  cmd.mutable_target()->set_lane_index(0);
  cmd.set_clip_index(0);

  auto* pt1 = cmd.add_points();
  pt1->set_time_beats(0.0f);
  pt1->set_value(0.0f);
  pt1->set_tension(0.0f);

  auto* pt2 = cmd.add_points();
  pt2->set_time_beats(4.0f);
  pt2->set_value(1.0f);
  pt2->set_tension(0.5f);

  // Add out of order to test sorting
  auto* pt3 = cmd.add_points();
  pt3->set_time_beats(2.0f);
  pt3->set_value(0.5f);
  pt3->set_tension(-0.3f);

  hibiki::handleAutomationCmd(cmd, state, history);

  auto& points = track->automation_lanes[0].clips[0]->clip->automation_points;
  ASSERT_EQ(points.size(), 3);
  // Points should be sorted by time_beats
  EXPECT_FLOAT_EQ(points[0].time_beats(), 0.0f);
  EXPECT_FLOAT_EQ(points[1].time_beats(), 2.0f);
  EXPECT_FLOAT_EQ(points[2].time_beats(), 4.0f);
  // Check values
  EXPECT_FLOAT_EQ(points[1].value(), 0.5f);
  EXPECT_FLOAT_EQ(points[1].tension(), -0.3f);
}

TEST_F(CommandsTest, UpdateAutomationPointsClampsValues) {
  auto track = hibiki::GetOrCreateTrack(state, 0);
  track->AddAutomationLane(0, 1);
  auto tc = std::make_unique<hibiki::TimelineClip>();
  tc->clip = std::make_unique<hibiki::Clip>();
  tc->clip->type = hibiki::Clip::Type::AUTOMATION;
  track->automation_lanes[0].clips.push_back(std::move(tc));

  hibiki::pb::commands::AutomationCmd cmd;
  cmd.set_action(hibiki::pb::commands::AutomationCmd::ACTION_UPDATE_POINTS);
  cmd.mutable_target()->set_track_index(0);
  cmd.mutable_target()->set_lane_index(0);
  cmd.set_clip_index(0);

  auto* pt = cmd.add_points();
  pt->set_time_beats(0.0f);
  pt->set_value(2.0f);     // Over max (should clamp to 1.0)
  pt->set_tension(-5.0f);  // Under min (should clamp to -1.0)

  hibiki::handleAutomationCmd(cmd, state, history);

  auto& points = track->automation_lanes[0].clips[0]->clip->automation_points;
  ASSERT_EQ(points.size(), 1);
  EXPECT_FLOAT_EQ(points[0].value(), 1.0f);
  EXPECT_FLOAT_EQ(points[0].tension(), -1.0f);
}

// ─── Automation edge cases ──────────────────────────────────────────

TEST_F(CommandsTest, AutomationCmdInvalidTrack) {
  // Should not crash when targeting a non-existent track
  hibiki::pb::commands::AutomationCmd cmd;
  cmd.set_action(hibiki::pb::commands::AutomationCmd::ACTION_ADD_CLIP);
  cmd.mutable_target()->set_track_index(99);
  cmd.mutable_target()->set_lane_index(0);

  EXPECT_NO_FATAL_FAILURE(hibiki::handleAutomationCmd(cmd, state, history));
}

TEST_F(CommandsTest, AutomationCmdInvalidLane) {
  hibiki::GetOrCreateTrack(state, 0);

  hibiki::pb::commands::AutomationCmd cmd;
  cmd.set_action(hibiki::pb::commands::AutomationCmd::ACTION_ADD_CLIP);
  cmd.mutable_target()->set_track_index(0);
  cmd.mutable_target()->set_lane_index(99);

  EXPECT_NO_FATAL_FAILURE(hibiki::handleAutomationCmd(cmd, state, history));
}

TEST_F(CommandsTest, AutomationCmdInvalidClipIndex) {
  auto track = hibiki::GetOrCreateTrack(state, 0);
  track->AddAutomationLane(0, 1);

  hibiki::pb::commands::AutomationCmd cmd;
  cmd.set_action(hibiki::pb::commands::AutomationCmd::ACTION_MOVE_CLIP);
  cmd.mutable_target()->set_track_index(0);
  cmd.mutable_target()->set_lane_index(0);
  cmd.set_clip_index(99);
  cmd.set_start_time_sec(5.0f);

  EXPECT_NO_FATAL_FAILURE(hibiki::handleAutomationCmd(cmd, state, history));
}

// ─── Track commands ─────────────────────────────────────────────────

TEST_F(CommandsTest, StopTrack) {
  hibiki::GetOrCreateTrack(state, 0);

  hibiki::pb::commands::TrackCmd cmd;
  cmd.set_action(hibiki::pb::commands::TrackCmd::ACTION_STOP);
  cmd.mutable_target()->set_track_index(0);

  EXPECT_NO_FATAL_FAILURE(hibiki::handleTrackCmd(cmd, state, history));
}

// ─── Plugin editor commands ─────────────────────────────────────────

TEST_F(CommandsTest, ShowPluginGuiInvalidTrack) {
  // No tracks created  Eshould not crash
  hibiki::pb::commands::PluginCmd cmd;
  cmd.set_action(hibiki::pb::commands::PluginCmd::ACTION_SHOW_GUI);
  cmd.mutable_target()->set_track_index(99);
  cmd.mutable_target()->set_plugin_index(0);

  EXPECT_NO_FATAL_FAILURE(hibiki::handlePluginCmd(cmd, state, history));
}

TEST_F(CommandsTest, StopPluginGuiInvalidTrack) {
  hibiki::pb::commands::PluginCmd cmd;
  cmd.set_action(hibiki::pb::commands::PluginCmd::ACTION_STOP_GUI);
  cmd.mutable_target()->set_track_index(99);
  cmd.mutable_target()->set_plugin_index(0);

  EXPECT_NO_FATAL_FAILURE(hibiki::handlePluginCmd(cmd, state, history));
}

TEST_F(CommandsTest, GetEditorFrameNoPlugin) {
  // Create a track but no plugin  Eshould not crash
  hibiki::GetOrCreateTrack(state, 0);

  hibiki::pb::commands::PluginCmd cmd;
  cmd.set_action(hibiki::pb::commands::PluginCmd::ACTION_GET_EDITOR_FRAME);
  cmd.mutable_target()->set_track_index(0);
  cmd.mutable_target()->set_plugin_index(0);

  EXPECT_NO_FATAL_FAILURE(hibiki::handlePluginCmd(cmd, state, history));
}

TEST_F(CommandsTest, GetEditorFrameInvalidTrack) {
  hibiki::pb::commands::PluginCmd cmd;
  cmd.set_action(hibiki::pb::commands::PluginCmd::ACTION_GET_EDITOR_FRAME);
  cmd.mutable_target()->set_track_index(99);
  cmd.mutable_target()->set_plugin_index(0);

  EXPECT_NO_FATAL_FAILURE(hibiki::handlePluginCmd(cmd, state, history));
}

TEST_F(CommandsTest, SendEditorInputNoPlugin) {
  hibiki::GetOrCreateTrack(state, 0);

  hibiki::pb::commands::PluginCmd cmd;
  cmd.set_action(hibiki::pb::commands::PluginCmd::ACTION_SEND_EDITOR_INPUT);
  cmd.mutable_target()->set_track_index(0);
  cmd.mutable_target()->set_plugin_index(0);
  cmd.set_input_type(0);  // MOUSE_MOVE
  cmd.set_input_x(100);
  cmd.set_input_y(200);

  EXPECT_NO_FATAL_FAILURE(hibiki::handlePluginCmd(cmd, state, history));
}

TEST_F(CommandsTest, SendEditorInputInvalidTrack) {
  hibiki::pb::commands::PluginCmd cmd;
  cmd.set_action(hibiki::pb::commands::PluginCmd::ACTION_SEND_EDITOR_INPUT);
  cmd.mutable_target()->set_track_index(99);
  cmd.mutable_target()->set_plugin_index(0);
  cmd.set_input_type(1);  // MOUSE_DOWN

  EXPECT_NO_FATAL_FAILURE(hibiki::handlePluginCmd(cmd, state, history));
}

TEST_F(CommandsTest, MidiClipDurationUpdate) {
  auto track = hibiki::GetOrCreateTrack(state, 1);
  track->LoadClip(1, hibiki::find_test_file("testdata/test.mid"));
  ASSERT_EQ(track->clips.count(1), 1);
  auto* clip = track->clips[1].get();
  double initial_dur = clip->duration_beats;
  EXPECT_GT(initial_dur, 100.0);

  hibiki::pb::commands::MidiCmd cmd;
  cmd.set_action(hibiki::pb::commands::MidiCmd::ACTION_UPDATE);
  cmd.mutable_target()->set_track_index(1);
  cmd.mutable_target()->set_session_slot(1);
  cmd.set_resolution(480);

  auto* ev = cmd.add_events();
  ev->set_tick(0);
  ev->set_pitch(36);
  ev->set_duration_ticks(120);
  ev->set_velocity(90);

  hibiki::handleMidiCmd(cmd, state, history);

  EXPECT_DOUBLE_EQ(clip->duration_beats, 4.0);
}

TEST_F(CommandsTest, TransportMetronomePanicMasterVolPitch) {
  auto trk = hibiki::GetOrCreateTrack(state, 1);
  ASSERT_NE(trk, nullptr);

  hibiki::pb::commands::TransportCmd metro;
  metro.set_action(hibiki::pb::commands::TransportCmd::ACTION_SET_METRONOME);
  metro.set_enabled(true);
  hibiki::handleTransportCmd(metro, state);
  EXPECT_TRUE(state.metronome_enabled);

  hibiki::pb::commands::TransportCmd mv;
  mv.set_action(hibiki::pb::commands::TransportCmd::ACTION_SET_MASTER_VOLUME);
  mv.set_value(0.55f);
  hibiki::handleTransportCmd(mv, state);
  EXPECT_FLOAT_EQ(state.master_volume, 0.55f);

  hibiki::pb::commands::TransportCmd mp;
  mp.set_action(hibiki::pb::commands::TransportCmd::ACTION_SET_MASTER_PITCH);
  mp.set_value(-5.0f);
  hibiki::handleTransportCmd(mp, state);
  EXPECT_FLOAT_EQ(state.master_pitch, -5.0f);

  hibiki::pb::commands::TransportCmd panic;
  panic.set_action(hibiki::pb::commands::TransportCmd::ACTION_PANIC);
  hibiki::handleTransportCmd(panic, state);
  EXPECT_TRUE(trk->panic_requested_.load());
}

TEST_F(CommandsTest, SyncProjectToGuiIncludesMixBypassAndMidi) {
  auto track = hibiki::GetOrCreateTrack(state, 1);
  track->name = "Acid Lead";
  track->track_type = hibiki::Track::TrackType::GROUP;
  track->output_track_index = 0;
  track->volume = 0.72f;
  track->pan = -0.35f;
  track->muted = true;
  track->LoadPlugin("builtin://eq", 0, state.sample_rate);
  track->plugin_bypass[0] = true;

  std::vector<hibiki::pb::notifications::Notification> captured;
  hibiki::g_ipc_enabled = true;
  hibiki::setNotificationHandler([&](const uint8_t* buf, size_t size) {
    hibiki::pb::notifications::Notification n;
    if (n.ParseFromArray(buf, static_cast<int>(size))) {
      captured.push_back(std::move(n));
    }
  });

  hibiki::SyncProjectToGui(state);

  hibiki::setNotificationHandler(nullptr);
  hibiki::g_ipc_enabled = false;

  bool saw_playhead = false;
  bool saw_track_mix = false;
  bool saw_param_bypass = false;
  for (const auto& n : captured) {
    if (n.has_playhead_info()) {
      saw_playhead = true;
      EXPECT_FLOAT_EQ(n.playhead_info().bpm(), 120.0f);
    }
    if (n.has_track_info() && n.track_info().track_index() == 1) {
      saw_track_mix = true;
      EXPECT_FLOAT_EQ(n.track_info().volume(), 0.72f);
      EXPECT_FLOAT_EQ(n.track_info().pan(), -0.35f);
      EXPECT_TRUE(n.track_info().muted());
    }
    if (n.has_param_list() && n.param_list().track_index() == 1) {
      saw_param_bypass = n.param_list().bypassed();
    }
  }
  EXPECT_TRUE(saw_playhead);
  EXPECT_TRUE(saw_track_mix);
  EXPECT_TRUE(saw_param_bypass);
}

TEST_F(CommandsTest, SequencerCmdChannelRoutingStepsNotesAndSync) {
  state.sample_rate = 44100.0;
  state.bpm = 140.0;

  // 1. Add Channel 0 -> creates generator track 100 routed to Insert 4
  {
    hibiki::pb::commands::SequencerCmd seq;
    seq.set_action(hibiki::pb::commands::SequencerCmd::ACTION_ADD_CHANNEL);
    seq.set_channel_index(0);
    seq.set_name("DR-8 Kick");
    seq.set_uri("builtin://dr8_kick");
    seq.set_category("Drums");
    seq.set_mixer_insert(4);
    hibiki::handleSequencerCmd(seq, state, history);
  }
  ASSERT_EQ(state.sequencer_channels.size(), 1u);
  ASSERT_NE(state.tracks.find(100), state.tracks.end());
  EXPECT_EQ(state.tracks[100]->output_track_index, 4);
  EXPECT_FALSE(state.tracks[100]->plugins.empty());

  // 2. Add Pattern 0 + Pattern 1, toggle steps, set graph param & Piano Roll
  // notes
  {
    hibiki::pb::commands::SequencerCmd seq;
    seq.set_action(hibiki::pb::commands::SequencerCmd::ACTION_ADD_PATTERN);
    seq.set_pattern_index(0);
    seq.set_name("Pattern 1");
    seq.set_length_steps(16);
    hibiki::handleSequencerCmd(seq, state, history);
  }
  {
    hibiki::pb::commands::SequencerCmd seq;
    seq.set_action(hibiki::pb::commands::SequencerCmd::ACTION_SET_STEP);
    seq.set_pattern_index(0);
    seq.set_channel_index(0);
    seq.set_step_index(4);
    seq.set_enabled(true);
    hibiki::handleSequencerCmd(seq, state, history);
  }
  {
    hibiki::pb::commands::SequencerCmd seq;
    seq.set_action(hibiki::pb::commands::SequencerCmd::ACTION_SET_STEP_GRAPH);
    seq.set_pattern_index(0);
    seq.set_channel_index(0);
    seq.set_step_index(4);
    seq.set_graph_param(1);  // pitch
    seq.set_value(0.25f);
    hibiki::handleSequencerCmd(seq, state, history);
  }
  {
    hibiki::pb::commands::SequencerCmd seq;
    seq.set_action(
        hibiki::pb::commands::SequencerCmd::ACTION_SET_CHANNEL_NOTES);
    seq.set_pattern_index(0);
    seq.set_channel_index(0);
    auto* ev = seq.add_notes();
    ev->set_tick(240);
    ev->set_pitch(64);
    ev->set_duration_ticks(180);
    ev->set_velocity(110);
    hibiki::handleSequencerCmd(seq, state, history);
  }
  {
    hibiki::pb::commands::SequencerCmd seq;
    seq.set_action(hibiki::pb::commands::SequencerCmd::ACTION_SET_SWING);
    seq.set_value(0.45f);
    hibiki::handleSequencerCmd(seq, state, history);
  }
  {
    hibiki::pb::commands::SequencerCmd seq;
    seq.set_action(
        hibiki::pb::commands::SequencerCmd::ACTION_SET_PLAYBACK_MODE);
    seq.set_playback_mode(hibiki::pb::core::PLAYBACK_MODE_PAT);
    hibiki::handleSequencerCmd(seq, state, history);
  }

  ASSERT_EQ(state.sequencer_patterns.size(), 1u);
  ASSERT_EQ(state.sequencer_patterns[0].channels.size(), 1u);
  EXPECT_TRUE(state.sequencer_patterns[0].channels[0].steps[4]);
  EXPECT_FLOAT_EQ(state.sequencer_patterns[0].channels[0].step_pitches[4],
                  0.25f);
  ASSERT_EQ(state.sequencer_patterns[0].channels[0].notes.size(), 1u);
  EXPECT_EQ(state.sequencer_patterns[0].channels[0].notes[0].pitch(), 64u);
  EXPECT_FLOAT_EQ(state.swing_amount, 0.45f);
  EXPECT_EQ(state.playback_mode, hibiki::SequencerPlaybackMode::PAT);

  // 3. Re-route channel to Insert 7 & add pattern://0 Playlist clip
  {
    hibiki::pb::commands::SequencerCmd seq;
    seq.set_action(
        hibiki::pb::commands::SequencerCmd::ACTION_SET_CHANNEL_ROUTE);
    seq.set_channel_index(0);
    seq.set_mixer_insert(7);
    hibiki::handleSequencerCmd(seq, state, history);
  }
  EXPECT_EQ(state.tracks[100]->output_track_index, 7);

  auto* lane1 = hibiki::GetOrCreateTrack(state, 1);
  lane1->AddTimelineClip("pattern://0", 0.0, state.bpm, 8.0);
  ASSERT_EQ(lane1->timeline_clips.size(), 1u);
  EXPECT_EQ(lane1->timeline_clips[0]->pattern_index, 0);

  // 4. SyncProjectToGui emits SequencerSync & pattern_index
  std::vector<hibiki::pb::notifications::Notification> captured;
  hibiki::g_ipc_enabled = true;
  hibiki::setNotificationHandler([&](const uint8_t* buf, size_t size) {
    hibiki::pb::notifications::Notification n;
    if (n.ParseFromArray(buf, static_cast<int>(size))) {
      captured.push_back(std::move(n));
    }
  });
  hibiki::SyncProjectToGui(state);
  hibiki::setNotificationHandler(nullptr);
  hibiki::g_ipc_enabled = false;

  bool saw_seq_sync = false;
  bool saw_pat_clip = false;
  for (const auto& n : captured) {
    if (n.has_sequencer_sync()) {
      saw_seq_sync = true;
      EXPECT_EQ(n.sequencer_sync().playback_mode(),
                hibiki::pb::core::PLAYBACK_MODE_PAT);
      EXPECT_FLOAT_EQ(n.sequencer_sync().swing_amount(), 0.45f);
      EXPECT_EQ(n.sequencer_sync().channels_size(), 1);
      EXPECT_EQ(n.sequencer_sync().patterns_size(), 1);
    }
    if (n.has_timeline_clip_info() &&
        n.timeline_clip_info().has_pattern_index()) {
      saw_pat_clip = (n.timeline_clip_info().pattern_index() == 0);
    }
  }
  EXPECT_TRUE(saw_seq_sync);
  EXPECT_TRUE(saw_pat_clip);
}

TEST_F(CommandsTest, TrackEqStereoSepAndPluginMixCommandsAndSync) {
  auto* t1 = hibiki::GetOrCreateTrack(state, 1);
  t1->track_type = hibiki::Track::TrackType::GROUP;
  ASSERT_GE(t1->LoadPlugin("builtin://eq", 0, 44100.0).index, 0);

  // 1. Set 3-band EQ
  {
    hibiki::pb::commands::TrackCmd cmd;
    cmd.set_action(hibiki::pb::commands::TrackCmd::ACTION_SET_EQ);
    cmd.mutable_target()->set_track_index(1);
    cmd.set_eq_low(0.8f);
    cmd.set_eq_mid(0.35f);
    cmd.set_eq_high(0.9f);
    hibiki::handleTrackCmd(cmd, state, history);
  }
  EXPECT_FLOAT_EQ(t1->eq_low, 0.8f);
  EXPECT_FLOAT_EQ(t1->eq_mid, 0.35f);
  EXPECT_FLOAT_EQ(t1->eq_high, 0.9f);

  // 2. Set Stereo Separation
  {
    hibiki::pb::commands::TrackCmd cmd;
    cmd.set_action(hibiki::pb::commands::TrackCmd::ACTION_SET_STEREO_SEP);
    cmd.mutable_target()->set_track_index(1);
    cmd.set_value(-0.6f);
    hibiki::handleTrackCmd(cmd, state, history);
  }
  EXPECT_FLOAT_EQ(t1->stereo_sep, -0.6f);

  // 3. Set Plugin Wet/Dry Mix
  {
    hibiki::pb::commands::PluginCmd cmd;
    cmd.set_action(hibiki::pb::commands::PluginCmd::ACTION_SET_MIX);
    cmd.mutable_target()->set_track_index(1);
    cmd.mutable_target()->set_plugin_index(0);
    cmd.set_param_value(0.42f);
    hibiki::handlePluginCmd(cmd, state, history);
  }
  EXPECT_FLOAT_EQ(t1->GetPluginMix(0), 0.42f);

  // 4. Setting routing preserves eq/sep and volume instead of resetting them
  {
    hibiki::pb::commands::TrackCmd cmd;
    cmd.set_action(hibiki::pb::commands::TrackCmd::ACTION_SET_OUTPUT_ROUTING);
    cmd.mutable_target()->set_track_index(1);
    cmd.set_target_track_index(0);
    hibiki::handleTrackCmd(cmd, state, history);
  }
  EXPECT_FLOAT_EQ(t1->eq_low, 0.8f);
  EXPECT_FLOAT_EQ(t1->stereo_sep, -0.6f);

  // 5. SyncProjectToGui emits TrackInfo EQ/StereoSep and ParamList Mix
  std::vector<hibiki::pb::notifications::Notification> captured;
  hibiki::g_ipc_enabled = true;
  hibiki::setNotificationHandler([&](const uint8_t* buf, size_t size) {
    hibiki::pb::notifications::Notification n;
    if (n.ParseFromArray(buf, static_cast<int>(size))) {
      captured.push_back(std::move(n));
    }
  });
  hibiki::SyncProjectToGui(state);
  hibiki::setNotificationHandler(nullptr);
  hibiki::g_ipc_enabled = false;

  bool saw_track_eq = false;
  bool saw_plugin_mix = false;
  for (const auto& n : captured) {
    if (n.has_track_info() && n.track_info().track_index() == 1) {
      saw_track_eq = true;
      EXPECT_FLOAT_EQ(n.track_info().eq_low(), 0.8f);
      EXPECT_FLOAT_EQ(n.track_info().eq_mid(), 0.35f);
      EXPECT_FLOAT_EQ(n.track_info().eq_high(), 0.9f);
      EXPECT_FLOAT_EQ(n.track_info().stereo_sep(), -0.6f);
    }
    if (n.has_param_list() && n.param_list().track_index() == 1 &&
        n.param_list().plugin_index() == 0) {
      saw_plugin_mix = true;
      EXPECT_FLOAT_EQ(n.param_list().mix(), 0.42f);
    }
  }
  EXPECT_TRUE(saw_track_eq);
  EXPECT_TRUE(saw_plugin_mix);
}

TEST_F(CommandsTest, MidiClipTimelineTargetNotShadowedByUnsetSessionSlot) {
  auto* t0 = hibiki::GetOrCreateTrack(state, 0);
  // Populate both session slot 0 and timeline clip 0; target timeline_clip(0)
  // explicitly without setting session_slot.
  t0->clips[0] = std::make_unique<hibiki::Clip>();
  t0->clips[0]->type = hibiki::Clip::Type::MIDI;
  {
    hibiki::MidiEvent slotEv{};
    slotEv.beats = 0.0;
    slotEv.type = 0x90;
    slotEv.channel = 0;
    slotEv.note = 36;
    slotEv.velocity = 100;
    t0->clips[0]->midi_events.push_back(slotEv);
  }
  auto tc = std::make_unique<hibiki::TimelineClip>();
  tc->clip = std::make_unique<hibiki::Clip>();
  tc->clip->type = hibiki::Clip::Type::MIDI;
  t0->timeline_clips.push_back(std::move(tc));

  hibiki::pb::commands::MidiCmd cmd;
  cmd.set_action(hibiki::pb::commands::MidiCmd::ACTION_UPDATE);
  cmd.mutable_target()->set_track_index(0);
  cmd.mutable_target()->set_timeline_clip(0);
  cmd.set_resolution(480);
  auto* ev = cmd.add_events();
  ev->set_tick(0);
  ev->set_pitch(72);
  ev->set_duration_ticks(240);
  ev->set_velocity(100);
  hibiki::handleMidiCmd(cmd, state, history);

  // Session slot 0 must stay untouched (note 36), while timeline clip 0 gets
  // note 72
  ASSERT_EQ(t0->clips[0]->midi_events.size(), 1u);
  EXPECT_EQ(t0->clips[0]->midi_events[0].note, 36);
  ASSERT_GE(t0->timeline_clips[0]->clip->midi_events.size(), 1u);
  EXPECT_EQ(t0->timeline_clips[0]->clip->midi_events[0].note, 72);
}

}  // namespace hibiki
