#include "engine/core/commands.hpp"

#include <gtest/gtest.h>

#include "engine/core/track.hpp"
#include "engine/ipc/ipc.hpp"

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
  hibiki::pb::commands::ProjectCmd cmd;
  cmd.set_action(hibiki::pb::commands::ProjectCmd::ACTION_SET_BPM);
  cmd.set_bpm(140.0f);

  hibiki::handleProjectCmd(cmd, state, history);
  EXPECT_FLOAT_EQ(state.bpm, 140.0f);
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
  // No tracks created — should not crash
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
  // Create a track but no plugin — should not crash
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

}  // namespace hibiki
