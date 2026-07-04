#include "engine/core/project.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "engine/builtin_registry.hpp"
#include "engine/core/audio_file.hpp"
#include "engine/core/history.hpp"
#include "engine/ipc/ipc.hpp"
#include "engine/plugin/mock_plugin.hpp"
#include "engine/test_utils.hpp"
#include "engine/test_utils_state.hpp"

using ::absl_testing::IsOk;

namespace hibiki {

class ProjectTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Disable IPC to prevent blocking on stdout writes during tests
    hibiki::g_ipc_enabled = false;
  }
};

std::string GetDexedPath() {
  std::string path = "testdata/Dexed.vst3";
#ifdef _WIN32
  std::string win_path = "testdata/Dexed.vst3/Contents/x86_64-win/Dexed.vst3";
  std::string resolved = hibiki::find_test_file(win_path);
  if (resolved != win_path) {
    return resolved;
  }
#endif
  return hibiki::find_test_file(path);
}

TEST_F(ProjectTest, GetOrCreateTrack) {
  hibiki::ProjectState state;
  auto track0 = hibiki::GetOrCreateTrack(state, 0);
  ASSERT_NE(track0, nullptr);
  EXPECT_EQ(track0->index, 0);

  auto track1 = hibiki::GetOrCreateTrack(state, 1);
  ASSERT_NE(track1, nullptr);
  EXPECT_EQ(track1->index, 1);
  EXPECT_NE(track0, track1);

  auto track0_again = hibiki::GetOrCreateTrack(state, 0);
  EXPECT_EQ(track0, track0_again);
}

TEST_F(ProjectTest, SaveAndLoad) {
  hibiki::ProjectState state;
  state.bpm = 120.0;

  auto track = hibiki::GetOrCreateTrack(state, 0);
  track->LoadClip(0, hibiki::find_test_file("testdata/loop140.wav"));

  std::string tmp_file = std::tmpnam(nullptr);

  // Save
  ASSERT_THAT(hibiki::SaveProject(state, tmp_file), IsOk());

  // Modify state before load
  state.bpm = 140.0;
  state.tracks.clear();

  // Load
  ASSERT_THAT(hibiki::LoadProject(state, tmp_file), IsOk());

  EXPECT_DOUBLE_EQ(state.bpm, 120.0);
  auto loaded_track = hibiki::GetOrCreateTrack(state, 0);
  EXPECT_EQ(loaded_track->clips.count(0), 1);
  EXPECT_EQ(loaded_track->clips[0]->type, hibiki::Clip::Type::AUDIO);

  std::remove(tmp_file.c_str());
}

TEST_F(ProjectTest, SaveAndLoadTrackName) {
  hibiki::ProjectState state;
  state.bpm = 120.0;

  auto track = hibiki::GetOrCreateTrack(state, 0);
  track->name = "Drums";

  auto track1 = hibiki::GetOrCreateTrack(state, 1);
  track1->name = "Bass";

  std::string tmp_file = std::tmpnam(nullptr);

  // Save
  ASSERT_THAT(hibiki::SaveProject(state, tmp_file), IsOk());

  // Modify state before load
  state.tracks.clear();

  // Load
  ASSERT_THAT(hibiki::LoadProject(state, tmp_file), IsOk());

  auto loaded_track0 = hibiki::GetOrCreateTrack(state, 0);
  EXPECT_EQ(loaded_track0->name, "Drums");

  auto loaded_track1 = hibiki::GetOrCreateTrack(state, 1);
  EXPECT_EQ(loaded_track1->name, "Bass");

  std::remove(tmp_file.c_str());
}

TEST_F(ProjectTest, BounceProjectWithDexed) {
  hibiki::ProjectState state;
  state.bpm = 120.0;
  state.sample_rate = 44100.0;

  auto track = hibiki::GetOrCreateTrack(state, 0);
  std::string dexed_path = GetDexedPath();
  int pidx = track->LoadPlugin(dexed_path, 0, state.sample_rate).index;
  ASSERT_GE(pidx, 0) << "Failed to load Dexed plugin";

  std::string mid_path = hibiki::find_test_file("testdata/test.mid");
  track->AddTimelineClip(mid_path, 0.0, state.bpm);
  ASSERT_EQ(track->timeline_clips.size(), 1);

  std::string tmp_wav = "test_bounce_output.wav";

  // Run Bounce
  hibiki::BounceProject(state, tmp_wav);

  std::vector<float> audio_data;
  int channels = 0;
  double duration = 0.0;
  auto loaded_status = hibiki::LoadWav(tmp_wav, audio_data, channels, duration);

  EXPECT_THAT(loaded_status, IsOk());
  EXPECT_GT(audio_data.size(), 0) << "Should have written some audio frames";

  float max_amp = 0.0f;
  for (float f : audio_data) {
    if (std::abs(f) > max_amp) max_amp = std::abs(f);
  }

  EXPECT_GT(max_amp, 0.0f) << "Expected non-zero signal rendered from Dexed";

  // Explicitly clean up VST3 plugins before test exit to prevent hang
  state.tracks.clear();

  std::remove(tmp_wav.c_str());
}

TEST_F(ProjectTest, LoadProjectWithTimelineClips) {
  hibiki::ProjectState state;
  state.bpm = 140.0;
  state.sample_rate = 44100.0;

  // Build a project with a plugin and a timeline clip
  auto track = hibiki::GetOrCreateTrack(state, 0);
  std::string dexed_path = GetDexedPath();
  int pidx = track->LoadPlugin(dexed_path, 0, state.sample_rate).index;
  ASSERT_GE(pidx, 0) << "Failed to load Dexed plugin";

  std::string mid_path = hibiki::find_test_file("testdata/test.mid");
  track->AddTimelineClip(mid_path, 2.0, state.bpm);
  ASSERT_EQ(track->timeline_clips.size(), 1);

  // Save the project
  std::string tmp_file = std::tmpnam(nullptr);
  ASSERT_THAT(hibiki::SaveProject(state, tmp_file), IsOk());

  // Clear state and reload
  state.tracks.clear();
  auto load_result = hibiki::LoadProject(state, tmp_file);
  ASSERT_THAT(load_result, IsOk());

  // Verify BPM was loaded
  EXPECT_FLOAT_EQ(state.bpm, 140.0f) << "BPM should be 140";

  // Verify tracks were created
  EXPECT_EQ(state.tracks.size(), 1) << "Should have one track";

  // Check the track contents
  auto loaded_track = state.tracks.at(0).get();
  EXPECT_EQ(loaded_track->plugins.size(), 1) << "Should have 1 plugin";
  EXPECT_EQ(loaded_track->timeline_clips.size(), 1)
      << "Should have 1 timeline clip";

  if (!loaded_track->timeline_clips.empty()) {
    const auto& tc = loaded_track->timeline_clips[0];
    ASSERT_NE(tc->clip, nullptr) << "Timeline clip should be loaded";
    EXPECT_EQ(tc->clip->type, hibiki::Clip::Type::MIDI);
    EXPECT_FLOAT_EQ(tc->start_time_sec, 2.0f);
    EXPECT_GT(tc->duration_beats, 0.0)
        << "MIDI clip should have duration in beats";
  }

  // Clean up
  state.tracks.clear();
  std::remove(tmp_file.c_str());
}

// Test that a correctly structured project (plugin + MIDI on same track) plays
// back correctly
TEST_F(ProjectTest, SaveAndLoadCorrectProjectStructure) {
  hibiki::ProjectState state;
  state.bpm = 120.0;
  state.sample_rate = 44100.0;

  // Create a track with BOTH plugin AND timeline clip (correct structure)
  auto track = hibiki::GetOrCreateTrack(state, 1);
  std::string dexed_path = GetDexedPath();
  int pidx = track->LoadPlugin(dexed_path, 0, state.sample_rate).index;
  ASSERT_GE(pidx, 0) << "Failed to load Dexed plugin";

  std::string mid_path = hibiki::find_test_file("testdata/test.mid");
  track->AddTimelineClip(mid_path, 0.0, state.bpm);
  ASSERT_EQ(track->timeline_clips.size(), 1);

  // Save the project
  std::string tmp_file = "test_correct_project.hbk";
  ASSERT_THAT(hibiki::SaveProject(state, tmp_file), IsOk());

  // Clear and reload
  state.tracks.clear();
  ASSERT_THAT(hibiki::LoadProject(state, tmp_file), IsOk());

  // Verify structure is correct
  EXPECT_EQ(state.bpm, 120.0);
  EXPECT_EQ(state.tracks.size(), 1);

  auto loaded_track = state.tracks.at(1).get();
  EXPECT_EQ(loaded_track->plugins.size(), 1) << "Should have 1 plugin";
  EXPECT_EQ(loaded_track->timeline_clips.size(), 1)
      << "Should have 1 timeline clip";

  if (!loaded_track->plugins.empty() && !loaded_track->timeline_clips.empty()) {
    EXPECT_TRUE(loaded_track->plugins[0]->isInstrument())
        << "Plugin should be instrument";
    EXPECT_EQ(loaded_track->timeline_clips[0]->clip->type,
              hibiki::Clip::Type::MIDI);
    LOG(INFO) << "Correct structure: Track 1 has both Dexed plugin and MIDI "
                 "timeline clip";
  }

  // Clean up
  state.tracks.clear();
  std::remove(tmp_file.c_str());
}

// Test for bug: crash when deleting plugin then loading new plugin to empty
// track
TEST_F(ProjectTest, DeletePluginThenLoadNew) {
  hibiki::ProjectState state;
  state.bpm = 120.0;
  state.sample_rate = 44100.0;

  auto track = hibiki::GetOrCreateTrack(state, 0);
  std::string dexed_path = GetDexedPath();

  // Step 1: Load a plugin
  int pidx = track->LoadPlugin(dexed_path, 0, state.sample_rate).index;
  ASSERT_GE(pidx, 0) << "Failed to load Dexed plugin";
  EXPECT_EQ(track->plugins.size(), 1);

  // Step 2: Delete the plugin
  auto removed = track->RemovePlugin(0);
  EXPECT_TRUE(removed != nullptr) << "Should successfully remove plugin";
  EXPECT_EQ(track->plugins.size(), 0) << "Track should now be empty";

  // Step 3: Load a new plugin to the now-empty track (this was crashing)
  int pidx2 = track->LoadPlugin(dexed_path, 0, state.sample_rate).index;
  ASSERT_GE(pidx2, 0) << "Failed to load Dexed plugin to empty track";
  EXPECT_EQ(track->plugins.size(), 1);

  // Clean up
  state.tracks.clear();
}

// ── Error case tests ────────────────────────────────────────────────

TEST_F(ProjectTest, LoadProjectFileNotFound) {
  hibiki::ProjectState state;
  EXPECT_THAT(hibiki::LoadProject(state, "/nonexistent/path/to/project.hbk"),
              absl_testing::StatusIs(absl::StatusCode::kNotFound));
}

TEST_F(ProjectTest, SaveProjectUnwritablePath) {
  hibiki::ProjectState state;
  state.bpm = 120.0;
  EXPECT_THAT(hibiki::SaveProject(state, "/nonexistent/dir/project.hbk"),
              absl_testing::StatusIs(absl::StatusCode::kPermissionDenied));
}

TEST_F(ProjectTest, LoadProjectCorruptedFile) {
  // Create a file with garbage content
  std::string tmp = "test_corrupt_project.hbk";
  {
    std::ofstream out(tmp, std::ios::binary);
    out << "THIS_IS_NOT_A_VALID_PROJECT_FILE";
  }
  hibiki::ProjectState state;
  auto status = hibiki::LoadProject(state, tmp);
  EXPECT_THAT(status, ::testing::Not(IsOk()))
      << "LoadProject should fail on corrupted file";
  std::remove(tmp.c_str());
}

// ── Undo/Redo round-trip tests ──────────────────────────────────────

TEST_F(ProjectTest, CaptureAndApplyState) {
  hibiki::ProjectState state;
  state.bpm = 130.0;
  state.sample_rate = 44100.0;

  auto track = hibiki::GetOrCreateTrack(state, 0);
  track->name = "Lead";
  track->volume = 0.75f;
  track->pan = -0.5f;
  track->muted = true;
  track->soloed = false;
  track->record_armed = true;
  track->LoadClip(0, hibiki::find_test_file("testdata/loop140.wav"));

  // Capture
  auto snapshot = hibiki::CaptureProjectState(state);
  EXPECT_GT(snapshot.size(), 0u);

  // Apply to empty state
  hibiki::ProjectState restored;
  restored.sample_rate = 44100.0;
  ASSERT_THAT(hibiki::ApplyProjectState(restored, snapshot), IsOk());

  EXPECT_DOUBLE_EQ(restored.bpm, 130.0);
  EXPECT_EQ(restored.tracks.size(), 1u);
  auto* rt = restored.tracks.at(0).get();
  EXPECT_EQ(rt->name, "Lead");
  EXPECT_EQ(rt->clips.count(0), 1u);
}

TEST_F(ProjectTest, UndoRedoRoundTrip) {
  hibiki::HistoryManager history;
  hibiki::ProjectState state;
  state.bpm = 120.0;
  state.sample_rate = 44100.0;

  auto track = hibiki::GetOrCreateTrack(state, 0);
  track->LoadClip(0, hibiki::find_test_file("testdata/loop140.wav"));

  // Push initial state
  history.pushState(hibiki::CaptureProjectState(state));

  // Modify: change BPM and add a clip
  state.bpm = 180.0;

  // Undo: should restore BPM=120
  auto current = hibiki::CaptureProjectState(state);
  std::vector<uint8_t> prev;
  ASSERT_TRUE(history.undo(current, prev));
  ASSERT_THAT(hibiki::ApplyProjectState(state, prev), IsOk());
  EXPECT_DOUBLE_EQ(state.bpm, 120.0);

  // Redo: should restore BPM=180
  current = hibiki::CaptureProjectState(state);
  std::vector<uint8_t> next;
  ASSERT_TRUE(history.redo(current, next));
  ASSERT_THAT(hibiki::ApplyProjectState(state, next), IsOk());
  EXPECT_DOUBLE_EQ(state.bpm, 180.0);
}

TEST_F(ProjectTest, UndoEmptyHistory) {
  hibiki::HistoryManager history;
  std::vector<uint8_t> current = {1, 2, 3};
  std::vector<uint8_t> out;
  EXPECT_FALSE(history.undo(current, out));
}

TEST_F(ProjectTest, RedoEmptyHistory) {
  hibiki::HistoryManager history;
  std::vector<uint8_t> current = {1, 2, 3};
  std::vector<uint8_t> out;
  EXPECT_FALSE(history.redo(current, out));
}

TEST_F(ProjectTest, RedoClearedOnNewPush) {
  hibiki::HistoryManager history;
  hibiki::ProjectState state;
  state.bpm = 100.0;
  state.sample_rate = 44100.0;

  // State 1
  history.pushState(hibiki::CaptureProjectState(state));

  // State 2
  state.bpm = 200.0;
  auto current = hibiki::CaptureProjectState(state);

  // Undo to state 1
  std::vector<uint8_t> prev;
  ASSERT_TRUE(history.undo(current, prev));

  // Push new state 3 (should clear redo)
  state.bpm = 300.0;
  history.pushState(hibiki::CaptureProjectState(state));

  // Redo should fail
  current = hibiki::CaptureProjectState(state);
  std::vector<uint8_t> next;
  EXPECT_FALSE(history.redo(current, next));
}

TEST_F(ProjectTest, MultipleUndoSteps) {
  hibiki::HistoryManager history;
  hibiki::ProjectState state;
  state.bpm = 100.0;
  state.sample_rate = 44100.0;

  // Push 3 states
  history.pushState(hibiki::CaptureProjectState(state));  // BPM=100
  state.bpm = 110.0;
  history.pushState(hibiki::CaptureProjectState(state));  // BPM=110
  state.bpm = 120.0;
  history.pushState(hibiki::CaptureProjectState(state));  // BPM=120
  state.bpm = 130.0;                                      // Current

  // Undo 3 times
  std::vector<uint8_t> out;
  auto current = hibiki::CaptureProjectState(state);

  ASSERT_TRUE(history.undo(current, out));
  ASSERT_THAT(hibiki::ApplyProjectState(state, out), IsOk());
  EXPECT_DOUBLE_EQ(state.bpm, 120.0);

  current = hibiki::CaptureProjectState(state);
  ASSERT_TRUE(history.undo(current, out));
  ASSERT_THAT(hibiki::ApplyProjectState(state, out), IsOk());
  EXPECT_DOUBLE_EQ(state.bpm, 110.0);

  current = hibiki::CaptureProjectState(state);
  ASSERT_TRUE(history.undo(current, out));
  ASSERT_THAT(hibiki::ApplyProjectState(state, out), IsOk());
  EXPECT_DOUBLE_EQ(state.bpm, 100.0);

  // 4th undo should fail
  current = hibiki::CaptureProjectState(state);
  EXPECT_FALSE(history.undo(current, out));
}

TEST_F(ProjectTest, ApplyProjectState_EmptyData) {
  hibiki::ProjectState state;
  auto status = hibiki::ApplyProjectState(state, {});
  EXPECT_THAT(status, ::testing::Not(IsOk()));
}

TEST_F(ProjectTest, InMemoryMidiClipUndoRedo) {
  hibiki::ProjectState state;
  state.bpm = 120.0;
  state.sample_rate = 44100.0;

  // Create an in-memory MIDI clip
  auto* track = hibiki::GetOrCreateTrack(state, 0);
  track->AddTimelineClip("", 0.0, state.bpm, 4.0, state.sample_rate);
  ASSERT_EQ(track->timeline_clips.size(), 1);
  auto* clip = track->timeline_clips[0]->clip.get();
  ASSERT_NE(clip, nullptr);
  EXPECT_EQ(clip->type, hibiki::Clip::Type::MIDI);
  EXPECT_TRUE(clip->path.empty());

  // Add MIDI events to the in-memory clip
  hibiki::MidiEvent noteOn;
  noteOn.beats = 1.0;
  noteOn.note = 64;
  noteOn.velocity = 90;
  noteOn.type = 0x90;
  noteOn.channel = 0;
  clip->midi_events.push_back(noteOn);

  hibiki::MidiEvent noteOff;
  noteOff.beats = 2.0;
  noteOff.note = 64;
  noteOff.velocity = 0;
  noteOff.type = 0x80;
  noteOff.channel = 0;
  clip->midi_events.push_back(noteOff);

  // Capture the project state
  auto snapshot = hibiki::CaptureProjectState(state);

  // Apply to a new project state
  hibiki::ProjectState restored_state;
  restored_state.sample_rate = state.sample_rate;
  auto status = hibiki::ApplyProjectState(restored_state, snapshot);
  ASSERT_THAT(status, IsOk());

  // Verify the timeline clip and its notes are restored
  ASSERT_EQ(restored_state.tracks.size(), 1);
  auto* restored_track = restored_state.tracks[0].get();
  ASSERT_EQ(restored_track->timeline_clips.size(), 1);
  auto* restored_clip = restored_track->timeline_clips[0]->clip.get();
  ASSERT_NE(restored_clip, nullptr);
  EXPECT_EQ(restored_clip->type, hibiki::Clip::Type::MIDI);
  EXPECT_TRUE(restored_clip->path.empty());
  EXPECT_EQ(restored_clip->midi_events.size(), 2);
  EXPECT_EQ(restored_track->timeline_clips[0]->duration_beats, 4.0);
}

TEST_F(ProjectTest, UndoInstrumentLoadOverDrumMachine) {
  hibiki::ProjectState state;
  state.bpm = 120.0;
  state.sample_rate = 44100.0;

  hibiki::HistoryManager history;

  // 1. Create a track
  auto* track = hibiki::GetOrCreateTrack(state, 0);

  // 2. Load the drum machine plugin
  auto result =
      track->LoadPlugin("builtin://drum_machine", 0, state.sample_rate);
  ASSERT_GE(result.index, 0);
  ASSERT_EQ(track->plugins.size(), 1);
  EXPECT_EQ(track->plugins[0]->getPath(), "builtin://drum_machine");

  // 3. Add MIDI clip to the timeline
  track->AddTimelineClip("", 0.0, state.bpm, 4.0, state.sample_rate);
  ASSERT_EQ(track->timeline_clips.size(), 1);
  EXPECT_EQ(track->timeline_clips[0]->clip->type, hibiki::Clip::Type::MIDI);
  EXPECT_EQ(track->timeline_clips[0]->duration_beats, 4.0);

  // 4. Capture the project state before the next instrument load (which will be
  // undone)
  history.pushState(hibiki::CaptureProjectState(state));

  // 5. Load a new instrument (3xOsc) over the drum machine
  auto result2 = track->LoadPlugin("builtin://3xosc", 0, state.sample_rate);
  ASSERT_GE(result2.index, 0);
  ASSERT_EQ(track->plugins.size(), 1);
  EXPECT_EQ(track->plugins[0]->getPath(), "builtin://3xosc");

  // 6. Undo the instrument load
  ASSERT_TRUE(hibiki::test::UndoOnce(history, state));

  // 7. Verify the drum machine is restored and the MIDI timeline clip is still
  // there
  ASSERT_EQ(state.tracks[0]->plugins.size(), 1);
  EXPECT_EQ(state.tracks[0]->plugins[0]->getPath(), "builtin://drum_machine");
  ASSERT_EQ(state.tracks[0]->timeline_clips.size(), 1);
  EXPECT_EQ(state.tracks[0]->timeline_clips[0]->clip->type,
            hibiki::Clip::Type::MIDI);
  EXPECT_EQ(state.tracks[0]->timeline_clips[0]->duration_beats, 4.0);
}

// ── E2E: Automation Edit + Undo/Redo ────────────────────────────────
// NOTE: Automation clips DO round-trip their breakpoint data, so we test those.

TEST_F(ProjectTest, AutomationEditUndoRedo) {
  hibiki::HistoryManager history;
  hibiki::ProjectState state;
  state.bpm = 120.0;
  state.sample_rate = 44100.0;

  // Create track with automation lane containing 2 points
  auto* track = hibiki::GetOrCreateTrack(state, 0);
  hibiki::AutomationLane lane;
  lane.plugin_idx = 0;
  lane.param_id = 1;
  auto tc = std::make_unique<hibiki::TimelineClip>();
  tc->clip = hibiki::test::MakeAutomationClip({{0.0f, 0.0f}, {4.0f, 1.0f}});
  tc->start_time_sec = 0.0;
  tc->duration_beats = tc->clip->duration_beats;
  lane.clips.push_back(std::move(tc));
  track->automation_lanes.push_back(std::move(lane));

  // Push initial state, then edit: change second point value
  history.pushState(hibiki::CaptureProjectState(state));
  track->automation_lanes[0].clips[0]->clip->automation_points[1].set_value(
      0.5f);

  // Undo: should restore value 1.0
  ASSERT_TRUE(hibiki::test::UndoOnce(history, state));
  track = state.tracks[0].get();
  EXPECT_FLOAT_EQ(
      track->automation_lanes[0].clips[0]->clip->automation_points[1].value(),
      1.0f);

  // Redo: should restore value 0.5
  ASSERT_TRUE(hibiki::test::RedoOnce(history, state));
  track = state.tracks[0].get();
  EXPECT_FLOAT_EQ(
      track->automation_lanes[0].clips[0]->clip->automation_points[1].value(),
      0.5f);
}

TEST_F(ProjectTest, AutomationAddPointUndoRedo) {
  hibiki::HistoryManager history;
  hibiki::ProjectState state;
  state.bpm = 120.0;
  state.sample_rate = 44100.0;

  // Start with 1 automation point
  auto* track = hibiki::GetOrCreateTrack(state, 0);
  hibiki::AutomationLane lane;
  lane.plugin_idx = 0;
  lane.param_id = 2;
  auto tc = std::make_unique<hibiki::TimelineClip>();
  tc->clip = hibiki::test::MakeAutomationClip({{0.0f, 0.5f}});
  tc->start_time_sec = 0.0;
  tc->duration_beats = tc->clip->duration_beats;
  lane.clips.push_back(std::move(tc));
  track->automation_lanes.push_back(std::move(lane));
  ASSERT_EQ(track->automation_lanes[0].clips[0]->clip->automation_points.size(),
            1);

  // Push, then add a point
  history.pushState(hibiki::CaptureProjectState(state));
  pb::core::AutomationPoint pt;
  pt.set_time_beats(8.0f);
  pt.set_value(1.0f);
  track->automation_lanes[0].clips[0]->clip->automation_points.push_back(pt);
  ASSERT_EQ(track->automation_lanes[0].clips[0]->clip->automation_points.size(),
            2);

  // Undo: back to 1 point
  ASSERT_TRUE(hibiki::test::UndoOnce(history, state));
  track = state.tracks[0].get();
  EXPECT_EQ(track->automation_lanes[0].clips[0]->clip->automation_points.size(),
            1);

  // Redo: back to 2 points
  ASSERT_TRUE(hibiki::test::RedoOnce(history, state));
  track = state.tracks[0].get();
  EXPECT_EQ(track->automation_lanes[0].clips[0]->clip->automation_points.size(),
            2);
}

TEST_F(ProjectTest, TrackNameEditUndoRedo) {
  hibiki::HistoryManager history;
  hibiki::ProjectState state;
  state.bpm = 120.0;
  state.sample_rate = 44100.0;

  auto* track = hibiki::GetOrCreateTrack(state, 0);
  track->name = "Lead";
  history.pushState(hibiki::CaptureProjectState(state));

  // Rename
  track->name = "Synth Lead";

  // Undo
  ASSERT_TRUE(hibiki::test::UndoOnce(history, state));
  EXPECT_EQ(state.tracks[0]->name, "Lead");

  // Redo
  ASSERT_TRUE(hibiki::test::RedoOnce(history, state));
  EXPECT_EQ(state.tracks[0]->name, "Synth Lead");
}

TEST_F(ProjectTest, MultiTrackUndoRedo) {
  hibiki::HistoryManager history;
  hibiki::ProjectState state;
  state.bpm = 120.0;
  state.sample_rate = 44100.0;

  // State 0: 1 track
  hibiki::GetOrCreateTrack(state, 0)->name = "Track 1";
  history.pushState(hibiki::CaptureProjectState(state));

  // State 1: 2 tracks
  hibiki::GetOrCreateTrack(state, 1)->name = "Track 2";
  history.pushState(hibiki::CaptureProjectState(state));

  // State 2: 3 tracks
  hibiki::GetOrCreateTrack(state, 2)->name = "Track 3";

  // Undo to 2 tracks
  ASSERT_TRUE(hibiki::test::UndoOnce(history, state));
  EXPECT_EQ(state.tracks.size(), 2u);
  EXPECT_TRUE(state.tracks.count(0));
  EXPECT_TRUE(state.tracks.count(1));

  // Undo to 1 track
  ASSERT_TRUE(hibiki::test::UndoOnce(history, state));
  EXPECT_EQ(state.tracks.size(), 1u);
  EXPECT_EQ(state.tracks[0]->name, "Track 1");

  // Redo to 2 tracks
  ASSERT_TRUE(hibiki::test::RedoOnce(history, state));
  EXPECT_EQ(state.tracks.size(), 2u);
}

TEST_F(ProjectTest, TrackMixerStateSaveLoad) {
  hibiki::ProjectState state;
  state.bpm = 120.0;
  state.sample_rate = 44100.0;

  auto* track = hibiki::GetOrCreateTrack(state, 0);
  track->name = "Mixer Test";
  track->record_armed = true;
  track->input_device_id = "hw:0";
  track->input_channel_start = 2;
  track->input_stereo = false;

  // Save/load to snapshot (undo path)
  auto snapshot = hibiki::CaptureProjectState(state);
  hibiki::ProjectState restored;
  restored.sample_rate = 44100.0;
  ASSERT_THAT(hibiki::ApplyProjectState(restored, snapshot), IsOk());

  auto* rt = restored.tracks[0].get();
  EXPECT_EQ(rt->name, "Mixer Test");
  EXPECT_TRUE(rt->record_armed);
  EXPECT_EQ(rt->input_device_id, "hw:0");
  EXPECT_EQ(rt->input_channel_start, 2);
  EXPECT_FALSE(rt->input_stereo);
}

TEST_F(ProjectTest, AutomationLaneSaveLoad) {
  hibiki::ProjectState state;
  state.bpm = 120.0;
  state.sample_rate = 44100.0;

  auto* track = hibiki::GetOrCreateTrack(state, 0);
  hibiki::AutomationLane lane;
  lane.plugin_idx = 0;
  lane.param_id = 42;
  auto tc = std::make_unique<hibiki::TimelineClip>();
  tc->clip = hibiki::test::MakeAutomationClip(
      {{0.0f, 0.0f}, {4.0f, 1.0f}, {8.0f, 0.5f}});
  tc->start_time_sec = 0.0;
  tc->duration_beats = tc->clip->duration_beats;
  lane.clips.push_back(std::move(tc));
  track->automation_lanes.push_back(std::move(lane));

  // Save/load via snapshot
  auto snapshot = hibiki::CaptureProjectState(state);
  hibiki::ProjectState restored;
  restored.sample_rate = 44100.0;
  ASSERT_THAT(hibiki::ApplyProjectState(restored, snapshot), IsOk());

  ASSERT_EQ(restored.tracks[0]->automation_lanes.size(), 1u);
  auto& rl = restored.tracks[0]->automation_lanes[0];
  EXPECT_EQ(rl.plugin_idx, 0);
  EXPECT_EQ(rl.param_id, 42u);
  ASSERT_EQ(rl.clips.size(), 1u);
  ASSERT_EQ(rl.clips[0]->clip->automation_points.size(), 3);
  EXPECT_FLOAT_EQ(rl.clips[0]->clip->automation_points[0].value(), 0.0f);
  EXPECT_FLOAT_EQ(rl.clips[0]->clip->automation_points[1].value(), 1.0f);
  EXPECT_FLOAT_EQ(rl.clips[0]->clip->automation_points[2].value(), 0.5f);
}

TEST_F(ProjectTest, AddTrackSaveLoadRoundTrip) {
  hibiki::ProjectState state;
  state.bpm = 140.0;
  state.sample_rate = 44100.0;

  hibiki::GetOrCreateTrack(state, 0)->name = "Drums";
  hibiki::GetOrCreateTrack(state, 1)->name = "Bass";
  hibiki::GetOrCreateTrack(state, 2)->name = "Keys";

  std::string tmp = std::tmpnam(nullptr);
  ASSERT_THAT(hibiki::SaveProject(state, tmp), IsOk());

  hibiki::ProjectState loaded;
  loaded.sample_rate = 44100.0;
  ASSERT_THAT(hibiki::LoadProject(loaded, tmp), IsOk());

  EXPECT_DOUBLE_EQ(loaded.bpm, 140.0);
  EXPECT_EQ(loaded.tracks.size(), 3u);
  EXPECT_EQ(loaded.tracks[0]->name, "Drums");
  EXPECT_EQ(loaded.tracks[1]->name, "Bass");
  EXPECT_EQ(loaded.tracks[2]->name, "Keys");
  std::remove(tmp.c_str());
}

TEST_F(ProjectTest, FullProjectUndoRedoSaveLoad) {
  hibiki::HistoryManager history;
  hibiki::ProjectState state;
  state.bpm = 120.0;
  state.sample_rate = 44100.0;

  // Build a multi-track project (no file-backed clips to avoid load failures)
  auto* t0 = hibiki::GetOrCreateTrack(state, 0);
  t0->name = "Piano";
  auto* t1 = hibiki::GetOrCreateTrack(state, 1);
  t1->name = "Strings";

  // Add automation lane to track 1
  hibiki::AutomationLane lane;
  lane.plugin_idx = 0;
  lane.param_id = 7;
  auto tc = std::make_unique<hibiki::TimelineClip>();
  tc->clip = hibiki::test::MakeAutomationClip({{0.0f, 0.5f}, {4.0f, 1.0f}});
  tc->start_time_sec = 0.0;
  tc->duration_beats = tc->clip->duration_beats;
  lane.clips.push_back(std::move(tc));
  t1->automation_lanes.push_back(std::move(lane));

  // Push initial state
  history.pushState(hibiki::CaptureProjectState(state));

  // Modify: change BPM and rename
  state.bpm = 140.0;
  state.tracks[0]->name = "Grand Piano";
  history.pushState(hibiki::CaptureProjectState(state));

  // Further modify: delete track 1
  state.tracks.erase(1);

  // Undo: track 1 restored, BPM=140
  ASSERT_TRUE(hibiki::test::UndoOnce(history, state));
  EXPECT_EQ(state.tracks.size(), 2u);
  EXPECT_DOUBLE_EQ(state.bpm, 140.0);
  EXPECT_EQ(state.tracks[0]->name, "Grand Piano");

  // Undo again: BPM=120, original name
  ASSERT_TRUE(hibiki::test::UndoOnce(history, state));
  EXPECT_DOUBLE_EQ(state.bpm, 120.0);
  EXPECT_EQ(state.tracks[0]->name, "Piano");

  // Redo: BPM=140
  ASSERT_TRUE(hibiki::test::RedoOnce(history, state));
  EXPECT_DOUBLE_EQ(state.bpm, 140.0);

  // Save current state
  std::string tmp = std::tmpnam(nullptr);
  ASSERT_THAT(hibiki::SaveProject(state, tmp), IsOk());

  // Load into fresh state
  hibiki::ProjectState loaded;
  loaded.sample_rate = 44100.0;
  ASSERT_THAT(hibiki::LoadProject(loaded, tmp), IsOk());
  EXPECT_DOUBLE_EQ(loaded.bpm, 140.0);
  EXPECT_EQ(loaded.tracks.size(), 2u);
  EXPECT_EQ(loaded.tracks[0]->name, "Grand Piano");
  // Automation lane should be preserved
  EXPECT_EQ(loaded.tracks[1]->automation_lanes.size(), 1u);
  std::remove(tmp.c_str());
}

// ── Automation-Based Clip Operations ────────────────────────────────
// NOTE: Timeline clips (MIDI/audio) reload from file path during proto
// round-trip, so in-memory edits don't survive undo/redo. Only automation
// clips fully round-trip their data. These tests exercise clip operations
// (remove, copy, move, resize) through automation lanes.

TEST_F(ProjectTest, AutomationClipRemoveUndoRedo) {
  hibiki::HistoryManager history;
  hibiki::ProjectState state;
  state.bpm = 120.0;
  state.sample_rate = 44100.0;

  auto* track = hibiki::GetOrCreateTrack(state, 0);
  hibiki::AutomationLane lane;
  lane.plugin_idx = 0;
  lane.param_id = 1;
  auto tc = std::make_unique<hibiki::TimelineClip>();
  tc->clip = hibiki::test::MakeAutomationClip({{0.0f, 0.5f}});
  tc->start_time_sec = 0.0;
  tc->duration_beats = 4.0;
  lane.clips.push_back(std::move(tc));
  track->automation_lanes.push_back(std::move(lane));
  ASSERT_EQ(track->automation_lanes[0].clips.size(), 1u);

  // Push, remove the clip
  history.pushState(hibiki::CaptureProjectState(state));
  track->automation_lanes[0].clips.erase(
      track->automation_lanes[0].clips.begin());
  ASSERT_EQ(track->automation_lanes[0].clips.size(), 0u);

  // Undo: clip restored
  ASSERT_TRUE(hibiki::test::UndoOnce(history, state));
  EXPECT_EQ(state.tracks[0]->automation_lanes[0].clips.size(), 1u);

  // Redo: clip removed again
  ASSERT_TRUE(hibiki::test::RedoOnce(history, state));
  EXPECT_EQ(state.tracks[0]->automation_lanes[0].clips.size(), 0u);
}

TEST_F(ProjectTest, TimelineClipCopyUndoRedo) {
  hibiki::HistoryManager history;
  hibiki::ProjectState state;
  state.bpm = 120.0;
  state.sample_rate = 44100.0;

  auto* track = hibiki::GetOrCreateTrack(state, 0);
  hibiki::AutomationLane lane;
  lane.plugin_idx = 0;
  lane.param_id = 1;
  auto tc = std::make_unique<hibiki::TimelineClip>();
  tc->clip = hibiki::test::MakeAutomationClip({{0.0f, 0.5f}, {4.0f, 1.0f}});
  tc->start_time_sec = 0.0;
  tc->duration_beats = 4.0;
  lane.clips.push_back(std::move(tc));
  track->automation_lanes.push_back(std::move(lane));
  ASSERT_EQ(track->automation_lanes[0].clips.size(), 1u);

  // Push, copy clip
  history.pushState(hibiki::CaptureProjectState(state));
  auto copy = std::make_unique<hibiki::TimelineClip>();
  copy->clip = hibiki::test::MakeAutomationClip({{0.0f, 0.5f}, {4.0f, 1.0f}});
  copy->start_time_sec = 4.0;
  copy->duration_beats = 4.0;
  track->automation_lanes[0].clips.push_back(std::move(copy));
  ASSERT_EQ(track->automation_lanes[0].clips.size(), 2u);

  // Undo: back to 1 clip
  ASSERT_TRUE(hibiki::test::UndoOnce(history, state));
  EXPECT_EQ(state.tracks[0]->automation_lanes[0].clips.size(), 1u);

  // Redo: 2 clips
  ASSERT_TRUE(hibiki::test::RedoOnce(history, state));
  EXPECT_EQ(state.tracks[0]->automation_lanes[0].clips.size(), 2u);
}

// ── Automation Clip Lifecycle ───────────────────────────────────────

TEST_F(ProjectTest, AutomationLaneAddRemoveUndoRedo) {
  hibiki::HistoryManager history;
  hibiki::ProjectState state;
  state.bpm = 120.0;
  state.sample_rate = 44100.0;

  auto* track = hibiki::GetOrCreateTrack(state, 0);
  ASSERT_EQ(track->automation_lanes.size(), 0u);

  // Push, add lane
  history.pushState(hibiki::CaptureProjectState(state));
  hibiki::AutomationLane lane;
  lane.plugin_idx = 0;
  lane.param_id = 7;
  track->automation_lanes.push_back(std::move(lane));
  ASSERT_EQ(track->automation_lanes.size(), 1u);

  // Undo: lane removed
  ASSERT_TRUE(hibiki::test::UndoOnce(history, state));
  EXPECT_EQ(state.tracks[0]->automation_lanes.size(), 0u);

  // Redo: lane restored
  ASSERT_TRUE(hibiki::test::RedoOnce(history, state));
  EXPECT_EQ(state.tracks[0]->automation_lanes.size(), 1u);
  EXPECT_EQ(state.tracks[0]->automation_lanes[0].param_id, 7u);
}

TEST_F(ProjectTest, AutomationClipMoveUndoRedo) {
  hibiki::HistoryManager history;
  hibiki::ProjectState state;
  state.bpm = 120.0;
  state.sample_rate = 44100.0;

  auto* track = hibiki::GetOrCreateTrack(state, 0);
  hibiki::AutomationLane lane;
  lane.plugin_idx = 0;
  lane.param_id = 1;
  auto tc = std::make_unique<hibiki::TimelineClip>();
  tc->clip = hibiki::test::MakeAutomationClip({{0.0f, 0.0f}});
  tc->start_time_sec = 0.0;
  tc->duration_beats = 4.0;
  lane.clips.push_back(std::move(tc));
  track->automation_lanes.push_back(std::move(lane));

  // Push, move clip
  history.pushState(hibiki::CaptureProjectState(state));
  track->automation_lanes[0].clips[0]->start_time_sec = 10.0;

  // Undo: position reverts
  ASSERT_TRUE(hibiki::test::UndoOnce(history, state));
  EXPECT_DOUBLE_EQ(
      state.tracks[0]->automation_lanes[0].clips[0]->start_time_sec, 0.0);

  // Redo: position forward
  ASSERT_TRUE(hibiki::test::RedoOnce(history, state));
  EXPECT_DOUBLE_EQ(
      state.tracks[0]->automation_lanes[0].clips[0]->start_time_sec, 10.0);
}

TEST_F(ProjectTest, AutomationClipResizeUndoRedo) {
  hibiki::HistoryManager history;
  hibiki::ProjectState state;
  state.bpm = 120.0;
  state.sample_rate = 44100.0;

  auto* track = hibiki::GetOrCreateTrack(state, 0);
  hibiki::AutomationLane lane;
  lane.plugin_idx = 0;
  lane.param_id = 2;
  auto tc = std::make_unique<hibiki::TimelineClip>();
  tc->clip = hibiki::test::MakeAutomationClip({{0.0f, 0.5f}});
  tc->start_time_sec = 0.0;
  tc->duration_beats = 4.0;
  tc->clip->duration_beats = 4.0;
  lane.clips.push_back(std::move(tc));
  track->automation_lanes.push_back(std::move(lane));

  // Push, resize
  history.pushState(hibiki::CaptureProjectState(state));
  track->automation_lanes[0].clips[0]->duration_beats = 16.0;
  track->automation_lanes[0].clips[0]->clip->duration_beats = 16.0;

  // Undo: size reverts
  ASSERT_TRUE(hibiki::test::UndoOnce(history, state));
  EXPECT_DOUBLE_EQ(
      state.tracks[0]->automation_lanes[0].clips[0]->duration_beats, 4.0);

  // Redo: expanded again
  ASSERT_TRUE(hibiki::test::RedoOnce(history, state));
  EXPECT_DOUBLE_EQ(
      state.tracks[0]->automation_lanes[0].clips[0]->duration_beats, 16.0);
}

TEST_F(ProjectTest, AutomationUpdatePointsUndoRedo) {
  hibiki::HistoryManager history;
  hibiki::ProjectState state;
  state.bpm = 120.0;
  state.sample_rate = 44100.0;

  auto* track = hibiki::GetOrCreateTrack(state, 0);
  hibiki::AutomationLane lane;
  lane.plugin_idx = 0;
  lane.param_id = 1;
  auto tc = std::make_unique<hibiki::TimelineClip>();
  tc->clip = hibiki::test::MakeAutomationClip({{0.0f, 0.0f}, {4.0f, 1.0f}});
  tc->start_time_sec = 0.0;
  tc->duration_beats = 4.0;
  lane.clips.push_back(std::move(tc));
  track->automation_lanes.push_back(std::move(lane));

  // Push, replace all points with a different set
  history.pushState(hibiki::CaptureProjectState(state));
  auto& pts = track->automation_lanes[0].clips[0]->clip->automation_points;
  pts.clear();
  pb::core::AutomationPoint p;
  p.set_time_beats(0.0f);
  p.set_value(0.5f);
  pts.push_back(p);

  // Undo: 2 points restored
  ASSERT_TRUE(hibiki::test::UndoOnce(history, state));
  EXPECT_EQ(state.tracks[0]
                ->automation_lanes[0]
                .clips[0]
                ->clip->automation_points.size(),
            2u);

  // Redo: back to 1 point
  ASSERT_TRUE(hibiki::test::RedoOnce(history, state));
  EXPECT_EQ(state.tracks[0]
                ->automation_lanes[0]
                .clips[0]
                ->clip->automation_points.size(),
            1u);
}

// ── Project-Level State ──────────────────────────────────────────────

TEST_F(ProjectTest, BpmUndoRedoRoundTrip) {
  hibiki::HistoryManager history;
  hibiki::ProjectState state;
  state.bpm = 120.0;
  state.sample_rate = 44100.0;

  history.pushState(hibiki::CaptureProjectState(state));
  state.bpm = 180.0;

  ASSERT_TRUE(hibiki::test::UndoOnce(history, state));
  EXPECT_DOUBLE_EQ(state.bpm, 120.0);

  ASSERT_TRUE(hibiki::test::RedoOnce(history, state));
  EXPECT_DOUBLE_EQ(state.bpm, 180.0);
}

TEST_F(ProjectTest, PlayheadSaveLoadRoundTrip) {
  hibiki::ProjectState state;
  state.bpm = 120.0;
  state.sample_rate = 44100.0;
  state.playhead_pos_sec = 42.5;

  auto data = hibiki::CaptureProjectState(state);
  hibiki::ProjectState restored;
  restored.sample_rate = 44100.0;
  ASSERT_THAT(hibiki::ApplyProjectState(restored, data), IsOk());
  EXPECT_DOUBLE_EQ(restored.playhead_pos_sec, 42.5);
}

TEST_F(ProjectTest, TrackInputDeviceSaveLoad) {
  hibiki::ProjectState state;
  state.bpm = 120.0;
  state.sample_rate = 44100.0;

  auto* track = hibiki::GetOrCreateTrack(state, 0);
  track->name = "Guitar";
  track->record_armed = true;
  track->input_device_id = "hw:1,0";
  track->input_channel_start = 2;
  track->input_stereo = true;

  auto data = hibiki::CaptureProjectState(state);
  hibiki::ProjectState restored;
  restored.sample_rate = 44100.0;
  ASSERT_THAT(hibiki::ApplyProjectState(restored, data), IsOk());

  ASSERT_EQ(restored.tracks.size(), 1u);
  auto* rt = restored.tracks[0].get();
  EXPECT_EQ(rt->name, "Guitar");
  EXPECT_TRUE(rt->record_armed);
  EXPECT_EQ(rt->input_device_id, "hw:1,0");
  EXPECT_EQ(rt->input_channel_start, 2);
  EXPECT_TRUE(rt->input_stereo);
}

// ── Mixer State Serialization Gap Tests ─────────────────────────────
// These tests document that volume/pan/mute/solo are NOT serialized,
// meaning they don't survive undo/redo or save/load.

TEST_F(ProjectTest, MixerStateNotSerializedInSnapshot) {
  hibiki::ProjectState state;
  state.bpm = 120.0;
  state.sample_rate = 44100.0;

  auto* track = hibiki::GetOrCreateTrack(state, 0);
  track->volume = 0.5f;
  track->pan = -0.3f;
  track->muted = true;
  track->soloed = true;

  auto data = hibiki::CaptureProjectState(state);
  hibiki::ProjectState restored;
  restored.sample_rate = 44100.0;
  ASSERT_THAT(hibiki::ApplyProjectState(restored, data), IsOk());

  // These are default values because they are NOT in the proto
  auto* rt = restored.tracks[0].get();
  EXPECT_FLOAT_EQ(rt->volume, 0.31623f);  // default = -10 dB
  EXPECT_FLOAT_EQ(rt->pan, 0.0f);         // default
  EXPECT_FALSE(rt->muted);                // default
  EXPECT_FALSE(rt->soloed);               // default
}

TEST_F(ProjectTest, AutomationLaneFullRoundTrip) {
  // Verify all automation lane fields survive: plugin_idx, param_id,
  // clip count, clip start_time_sec, clip duration_beats, and all points.
  hibiki::ProjectState state;
  state.bpm = 120.0;
  state.sample_rate = 44100.0;

  auto* track = hibiki::GetOrCreateTrack(state, 0);
  hibiki::AutomationLane lane;
  lane.plugin_idx = 2;
  lane.param_id = 42;
  auto tc = std::make_unique<hibiki::TimelineClip>();
  tc->clip = hibiki::test::MakeAutomationClip(
      {{0.0f, 0.0f}, {2.0f, 0.75f}, {4.0f, 1.0f}});
  // Set tension on point 1
  tc->clip->automation_points[1].set_tension(0.5f);
  tc->start_time_sec = 3.0;
  tc->duration_beats = 8.0;
  tc->clip->duration_beats = 8.0;
  lane.clips.push_back(std::move(tc));
  track->automation_lanes.push_back(std::move(lane));

  auto data = hibiki::CaptureProjectState(state);
  hibiki::ProjectState restored;
  restored.sample_rate = 44100.0;
  ASSERT_THAT(hibiki::ApplyProjectState(restored, data), IsOk());

  ASSERT_EQ(restored.tracks[0]->automation_lanes.size(), 1u);
  auto& rl = restored.tracks[0]->automation_lanes[0];
  EXPECT_EQ(rl.plugin_idx, 2);
  EXPECT_EQ(rl.param_id, 42u);
  ASSERT_EQ(rl.clips.size(), 1u);
  EXPECT_DOUBLE_EQ(rl.clips[0]->start_time_sec, 3.0);
  EXPECT_DOUBLE_EQ(rl.clips[0]->duration_beats, 8.0);
  ASSERT_EQ(rl.clips[0]->clip->automation_points.size(), 3u);
  EXPECT_FLOAT_EQ(rl.clips[0]->clip->automation_points[0].value(), 0.0f);
  EXPECT_FLOAT_EQ(rl.clips[0]->clip->automation_points[1].value(), 0.75f);
  EXPECT_FLOAT_EQ(rl.clips[0]->clip->automation_points[1].tension(), 0.5f);
  EXPECT_FLOAT_EQ(rl.clips[0]->clip->automation_points[2].value(), 1.0f);
}

// ── File-Backed Timeline Clip Operations ─────────────────────────────
// These tests use real MIDI/audio files so clips survive proto round-trips
// (LoadTracksFromProto reloads clips from file path).

TEST_F(ProjectTest, TimelineClipAddUndoRedo) {
  hibiki::HistoryManager history;
  hibiki::ProjectState state;
  state.bpm = 120.0;
  state.sample_rate = 44100.0;

  auto* track = hibiki::GetOrCreateTrack(state, 0);
  std::string mid = hibiki::find_test_file("testdata/test.mid");

  // Push initial (empty), add clip
  history.pushState(hibiki::CaptureProjectState(state));
  track->AddTimelineClip(mid, 2.0, state.bpm);
  ASSERT_EQ(track->timeline_clips.size(), 1u);
  EXPECT_FLOAT_EQ(track->timeline_clips[0]->start_time_sec, 2.0f);

  // Undo: clip removed
  ASSERT_TRUE(hibiki::test::UndoOnce(history, state));
  EXPECT_EQ(state.tracks[0]->timeline_clips.size(), 0u);

  // Redo: clip re-added (reloaded from path)
  ASSERT_TRUE(hibiki::test::RedoOnce(history, state));
  ASSERT_EQ(state.tracks[0]->timeline_clips.size(), 1u);
  EXPECT_FLOAT_EQ(state.tracks[0]->timeline_clips[0]->start_time_sec, 2.0f);
}

TEST_F(ProjectTest, TimelineClipMoveUndoRedo) {
  hibiki::HistoryManager history;
  hibiki::ProjectState state;
  state.bpm = 120.0;
  state.sample_rate = 44100.0;

  auto* track = hibiki::GetOrCreateTrack(state, 0);
  std::string mid = hibiki::find_test_file("testdata/test.mid");
  track->AddTimelineClip(mid, 1.0, state.bpm);

  // Push, move clip
  history.pushState(hibiki::CaptureProjectState(state));
  track->timeline_clips[0]->start_time_sec = 5.0;

  // Undo: back to 1.0
  ASSERT_TRUE(hibiki::test::UndoOnce(history, state));
  EXPECT_FLOAT_EQ(state.tracks[0]->timeline_clips[0]->start_time_sec, 1.0f);

  // Redo: back to 5.0
  ASSERT_TRUE(hibiki::test::RedoOnce(history, state));
  EXPECT_FLOAT_EQ(state.tracks[0]->timeline_clips[0]->start_time_sec, 5.0f);
}

TEST_F(ProjectTest, TimelineClipResizeUndoRedo) {
  hibiki::HistoryManager history;
  hibiki::ProjectState state;
  state.bpm = 120.0;
  state.sample_rate = 44100.0;

  auto* track = hibiki::GetOrCreateTrack(state, 0);
  std::string mid = hibiki::find_test_file("testdata/test.mid");
  track->AddTimelineClip(mid, 0.0, state.bpm);
  double original_dur = track->timeline_clips[0]->duration_beats;
  ASSERT_GT(original_dur, 0.0);

  // Push, resize clip
  history.pushState(hibiki::CaptureProjectState(state));
  track->timeline_clips[0]->duration_beats = 16.0;

  // Undo: duration back to original (file-determined)
  ASSERT_TRUE(hibiki::test::UndoOnce(history, state));
  // After undo, clip reloads from file — duration_beats comes from file
  EXPECT_GT(state.tracks[0]->timeline_clips[0]->duration_beats, 0.0);

  // Redo: back to 16.0 — but since proto doesn't persist timeline
  // duration_beats directly, this round-trips the file's own duration.
  // This documents a known limitation.
  ASSERT_TRUE(hibiki::test::RedoOnce(history, state));
}

TEST_F(ProjectTest, TimelineClipTrimUndoRedo) {
  hibiki::HistoryManager history;
  hibiki::ProjectState state;
  state.bpm = 120.0;
  state.sample_rate = 44100.0;

  auto* track = hibiki::GetOrCreateTrack(state, 0);
  std::string mid = hibiki::find_test_file("testdata/test.mid");
  track->AddTimelineClip(mid, 0.0, state.bpm);
  EXPECT_DOUBLE_EQ(track->timeline_clips[0]->trim_start_beats, 0.0);

  // Push, set trim
  history.pushState(hibiki::CaptureProjectState(state));
  track->timeline_clips[0]->trim_start_beats = 2.5;

  // Undo: trim back to 0.0
  ASSERT_TRUE(hibiki::test::UndoOnce(history, state));
  EXPECT_DOUBLE_EQ(state.tracks[0]->timeline_clips[0]->trim_start_beats, 0.0);

  // Redo: trim back to 2.5
  ASSERT_TRUE(hibiki::test::RedoOnce(history, state));
  EXPECT_DOUBLE_EQ(state.tracks[0]->timeline_clips[0]->trim_start_beats, 2.5);
}

TEST_F(ProjectTest, TimelineClipDeleteUndoRedo) {
  hibiki::HistoryManager history;
  hibiki::ProjectState state;
  state.bpm = 120.0;
  state.sample_rate = 44100.0;

  auto* track = hibiki::GetOrCreateTrack(state, 0);
  std::string mid = hibiki::find_test_file("testdata/test.mid");
  track->AddTimelineClip(mid, 1.0, state.bpm);
  ASSERT_EQ(track->timeline_clips.size(), 1u);

  // Push, delete clip
  history.pushState(hibiki::CaptureProjectState(state));
  track->RemoveTimelineClip(0);
  ASSERT_EQ(track->timeline_clips.size(), 0u);

  // Undo: clip restored
  ASSERT_TRUE(hibiki::test::UndoOnce(history, state));
  ASSERT_EQ(state.tracks[0]->timeline_clips.size(), 1u);
  EXPECT_FLOAT_EQ(state.tracks[0]->timeline_clips[0]->start_time_sec, 1.0f);

  // Redo: clip deleted again
  ASSERT_TRUE(hibiki::test::RedoOnce(history, state));
  EXPECT_EQ(state.tracks[0]->timeline_clips.size(), 0u);
}

TEST_F(ProjectTest, TimelineClipLoopUndoRedo) {
  hibiki::HistoryManager history;
  hibiki::ProjectState state;
  state.bpm = 120.0;
  state.sample_rate = 44100.0;

  auto* track = hibiki::GetOrCreateTrack(state, 0);
  std::string mid = hibiki::find_test_file("testdata/test.mid");
  track->AddTimelineClip(mid, 0.0, state.bpm);
  EXPECT_FALSE(track->timeline_clips[0]->clip->is_loop);

  // Push, enable loop
  history.pushState(hibiki::CaptureProjectState(state));
  track->timeline_clips[0]->clip->is_loop = true;
  track->timeline_clips[0]->loop_interval_beats = 4.0;

  // Undo: loop disabled
  ASSERT_TRUE(hibiki::test::UndoOnce(history, state));
  EXPECT_FALSE(state.tracks[0]->timeline_clips[0]->clip->is_loop);
  EXPECT_DOUBLE_EQ(state.tracks[0]->timeline_clips[0]->loop_interval_beats,
                   0.0);

  // Redo: loop re-enabled
  ASSERT_TRUE(hibiki::test::RedoOnce(history, state));
  EXPECT_TRUE(state.tracks[0]->timeline_clips[0]->clip->is_loop);
  EXPECT_DOUBLE_EQ(state.tracks[0]->timeline_clips[0]->loop_interval_beats,
                   4.0);
}

TEST_F(ProjectTest, TimelineClipCopyFileBackedUndoRedo) {
  hibiki::HistoryManager history;
  hibiki::ProjectState state;
  state.bpm = 120.0;
  state.sample_rate = 44100.0;

  auto* track = hibiki::GetOrCreateTrack(state, 0);
  std::string mid = hibiki::find_test_file("testdata/test.mid");
  track->AddTimelineClip(mid, 0.0, state.bpm);
  ASSERT_EQ(track->timeline_clips.size(), 1u);

  // Push, add a copy by duplicating the clip
  history.pushState(hibiki::CaptureProjectState(state));
  auto& src = track->timeline_clips[0];
  auto copy = std::make_unique<hibiki::TimelineClip>();
  copy->clip = std::make_unique<hibiki::Clip>();
  copy->clip->path = src->clip->path;
  copy->clip->type = src->clip->type;
  copy->clip->midi_events = src->clip->midi_events;
  copy->clip->duration_beats = src->clip->duration_beats;
  copy->clip->duration_sec = src->clip->duration_sec;
  copy->start_time_sec = 4.0;
  copy->duration_beats = src->duration_beats;
  track->timeline_clips.push_back(std::move(copy));
  ASSERT_EQ(track->timeline_clips.size(), 2u);

  // Undo: back to 1 clip
  ASSERT_TRUE(hibiki::test::UndoOnce(history, state));
  EXPECT_EQ(state.tracks[0]->timeline_clips.size(), 1u);

  // Redo: back to 2 clips
  ASSERT_TRUE(hibiki::test::RedoOnce(history, state));
  EXPECT_EQ(state.tracks[0]->timeline_clips.size(), 2u);
}

TEST_F(ProjectTest, TimelineClipCrossTrackMoveUndoRedo) {
  hibiki::HistoryManager history;
  hibiki::ProjectState state;
  state.bpm = 120.0;
  state.sample_rate = 44100.0;

  auto* track0 = hibiki::GetOrCreateTrack(state, 0);
  auto* track1 = hibiki::GetOrCreateTrack(state, 1);
  std::string mid = hibiki::find_test_file("testdata/test.mid");
  track0->AddTimelineClip(mid, 1.0, state.bpm);
  ASSERT_EQ(track0->timeline_clips.size(), 1u);
  ASSERT_EQ(track1->timeline_clips.size(), 0u);

  // Push, move clip from track 0 to track 1
  history.pushState(hibiki::CaptureProjectState(state));
  auto tc = std::move(track0->timeline_clips[0]);
  track0->timeline_clips.erase(track0->timeline_clips.begin());
  tc->start_time_sec = 3.0;
  track1->timeline_clips.push_back(std::move(tc));

  EXPECT_EQ(track0->timeline_clips.size(), 0u);
  EXPECT_EQ(track1->timeline_clips.size(), 1u);

  // Undo: clip back on track 0
  ASSERT_TRUE(hibiki::test::UndoOnce(history, state));
  EXPECT_EQ(state.tracks[0]->timeline_clips.size(), 1u);
  EXPECT_EQ(state.tracks[1]->timeline_clips.size(), 0u);
  EXPECT_FLOAT_EQ(state.tracks[0]->timeline_clips[0]->start_time_sec, 1.0f);

  // Redo: clip on track 1
  ASSERT_TRUE(hibiki::test::RedoOnce(history, state));
  EXPECT_EQ(state.tracks[0]->timeline_clips.size(), 0u);
  EXPECT_EQ(state.tracks[1]->timeline_clips.size(), 1u);
}

TEST_F(ProjectTest, TimelineClipSaveLoadRoundTrip) {
  hibiki::ProjectState state;
  state.bpm = 140.0;
  state.sample_rate = 44100.0;

  auto* track = hibiki::GetOrCreateTrack(state, 0);
  std::string mid = hibiki::find_test_file("testdata/test.mid");
  track->AddTimelineClip(mid, 2.0, state.bpm);
  auto& tc = track->timeline_clips[0];
  tc->trim_start_beats = 1.5;
  tc->clip->is_loop = true;
  tc->loop_interval_beats = 4.0;

  // Save / load
  std::string tmp = std::tmpnam(nullptr);
  ASSERT_THAT(hibiki::SaveProject(state, tmp), IsOk());

  hibiki::ProjectState restored;
  restored.sample_rate = 44100.0;
  ASSERT_THAT(hibiki::LoadProject(restored, tmp), IsOk());

  // Verify all fields round-tripped
  ASSERT_EQ(restored.tracks[0]->timeline_clips.size(), 1u);
  auto& rtc = restored.tracks[0]->timeline_clips[0];
  ASSERT_NE(rtc->clip, nullptr);
  EXPECT_FLOAT_EQ(rtc->start_time_sec, 2.0f);
  EXPECT_DOUBLE_EQ(rtc->trim_start_beats, 1.5);
  EXPECT_TRUE(rtc->clip->is_loop);
  EXPECT_DOUBLE_EQ(rtc->loop_interval_beats, 4.0);
  EXPECT_EQ(rtc->clip->type, hibiki::Clip::Type::MIDI);

  restored.tracks.clear();
  std::remove(tmp.c_str());
}

TEST_F(ProjectTest, AudioClipSaveLoadRoundTrip) {
  hibiki::ProjectState state;
  state.bpm = 140.0;
  state.sample_rate = 44100.0;

  auto* track = hibiki::GetOrCreateTrack(state, 0);
  std::string wav = hibiki::find_test_file("testdata/bb140.wav");
  track->AddTimelineClip(wav, 0.0, state.bpm, 0, state.sample_rate);
  ASSERT_EQ(track->timeline_clips.size(), 1u);
  auto& tc = track->timeline_clips[0];
  ASSERT_NE(tc->clip, nullptr);
  EXPECT_EQ(tc->clip->type, hibiki::Clip::Type::AUDIO);

  // Save / load
  std::string tmp = std::tmpnam(nullptr);
  ASSERT_THAT(hibiki::SaveProject(state, tmp), IsOk());

  hibiki::ProjectState restored;
  restored.sample_rate = 44100.0;
  ASSERT_THAT(hibiki::LoadProject(restored, tmp), IsOk());

  ASSERT_EQ(restored.tracks[0]->timeline_clips.size(), 1u);
  auto& rtc = restored.tracks[0]->timeline_clips[0];
  ASSERT_NE(rtc->clip, nullptr);
  EXPECT_EQ(rtc->clip->type, hibiki::Clip::Type::AUDIO);
  EXPECT_FLOAT_EQ(rtc->start_time_sec, 0.0f);
  EXPECT_GT(rtc->clip->audio_data.size(), 0u);

  restored.tracks.clear();
  std::remove(tmp.c_str());
}

// ── Plugin/Device Operations ────────────────────────────────────────

TEST_F(ProjectTest, PluginAddSaveLoadRoundTrip) {
  hibiki::ProjectState state;
  state.bpm = 120.0;
  state.sample_rate = 44100.0;

  auto* track = hibiki::GetOrCreateTrack(state, 0);
  std::string dexed_path = GetDexedPath();
  auto result = track->LoadPlugin(dexed_path, 0, state.sample_rate);
  ASSERT_GE(result.index, 0) << "Failed to load Dexed plugin";

  std::string tmp = std::tmpnam(nullptr);
  ASSERT_THAT(hibiki::SaveProject(state, tmp), IsOk());

  // Clear and reload — plugins are saved by path, so they reload
  state.tracks.clear();
  ASSERT_THAT(hibiki::LoadProject(state, tmp), IsOk());

  ASSERT_EQ(state.tracks[0]->plugins.size(), 1u);
  EXPECT_EQ(state.tracks[0]->plugins[0]->getPath(), dexed_path);

  state.tracks.clear();
  std::remove(tmp.c_str());
}

TEST_F(ProjectTest, PluginStateSaveLoadRoundTrip) {
  hibiki::ProjectState state;
  state.bpm = 120.0;
  state.sample_rate = 44100.0;

  auto* track = hibiki::GetOrCreateTrack(state, 0);
  std::string dexed_path = GetDexedPath();
  auto result = track->LoadPlugin(dexed_path, 0, state.sample_rate);
  ASSERT_GE(result.index, 0) << "Failed to load Dexed plugin";
  auto* plugin = track->plugins[result.index].get();

  // Get a parameter and change it so the state changes from default
  VstParamInfo param_info;
  ASSERT_TRUE(plugin->getParameterInfo(0, param_info));
  uint32_t param_id = param_info.id;
  double original_val = plugin->getParameterValue(param_id);
  double target_val = original_val > 0.5 ? 0.1 : 0.9;
  plugin->setParameterValue(param_id, target_val);

  std::vector<uint8_t> modified_state;
  ASSERT_TRUE(plugin->getState(modified_state)) << "getState returned false";
  ASSERT_FALSE(modified_state.empty()) << "getState returned empty vector";

  // Save project to a temp file
  std::string tmp = std::tmpnam(nullptr);
  ASSERT_THAT(hibiki::SaveProject(state, tmp), IsOk());

  // Verify that the serialized protobuf contains the state bytes, then clear
  // parameter mappings to force verification of setState.
  {
    std::ifstream ifs(tmp, std::ios::binary);
    hibiki::pb::core::Project project_proto;
    ASSERT_TRUE(project_proto.ParseFromIstream(&ifs))
        << "Failed to parse project proto";
    ifs.close();

    ASSERT_EQ(project_proto.tracks_size(), 1);
    ASSERT_EQ(project_proto.tracks(0).plugins_size(), 1);
    EXPECT_FALSE(project_proto.tracks(0).plugins(0).state().empty())
        << "Plugin state was not written to protobuf";

    // Clear the params to force setState to do the restoration work
    project_proto.mutable_tracks(0)->mutable_plugins(0)->clear_params();

    std::ofstream ofs(tmp, std::ios::binary);
    ASSERT_TRUE(project_proto.SerializeToOstream(&ofs));
    ofs.close();
  }

  // Load project and verify that the reloaded plugin's parameter value matches
  // target_val, which means the state was successfully loaded and restored.
  state.tracks.clear();
  ASSERT_THAT(hibiki::LoadProject(state, tmp), IsOk());

  ASSERT_EQ(state.tracks[0]->plugins.size(), 1u);
  auto* reloaded_plugin = state.tracks[0]->plugins[0].get();
  EXPECT_NEAR(reloaded_plugin->getParameterValue(param_id), target_val, 1e-6)
      << "Parameter value was not restored via setState (since params list was "
         "cleared)";

  state.tracks.clear();
  std::remove(tmp.c_str());
}

TEST_F(ProjectTest, MockPluginStateRoundTrip) {
  // Dynamically register mock plugin for this test
  hibiki::registerTestBuiltinPlugin(MockPlugin::kPath, [](const std::string&) {
    return std::make_unique<MockPlugin>();
  });

  hibiki::ProjectState state;
  state.bpm = 120.0;
  state.sample_rate = 44100.0;

  auto* track = hibiki::GetOrCreateTrack(state, 0);
  auto result =
      track->LoadPlugin("builtin://mock_plugin", 0, state.sample_rate);
  ASSERT_GE(result.index, 0);
  auto* plugin = dynamic_cast<MockPlugin*>(track->plugins[result.index].get());
  ASSERT_NE(plugin, nullptr);

  // Set mock-specific state
  std::vector<uint8_t> new_mock_data = {9, 8, 7, 6, 5};
  plugin->setInternalMockData(new_mock_data);
  plugin->setParameterValue(0, 0.25);

  // Save project
  std::string tmp = std::tmpnam(nullptr);
  ASSERT_THAT(hibiki::SaveProject(state, tmp), IsOk());

  // Load project
  state.tracks.clear();
  ASSERT_THAT(hibiki::LoadProject(state, tmp), IsOk());

  ASSERT_EQ(state.tracks[0]->plugins.size(), 1u);
  auto* reloaded_plugin =
      dynamic_cast<MockPlugin*>(state.tracks[0]->plugins[0].get());
  ASSERT_NE(reloaded_plugin, nullptr);

  // Verify state and parameter values are restored
  EXPECT_EQ(reloaded_plugin->getInternalMockData(), new_mock_data);
  EXPECT_NEAR(reloaded_plugin->getParameterValue(0), 0.25, 1e-6);

  state.tracks.clear();
  std::remove(tmp.c_str());
}

TEST_F(ProjectTest, PluginRemoveSaveLoadRoundTrip) {
  hibiki::ProjectState state;
  state.bpm = 120.0;
  state.sample_rate = 44100.0;

  auto* track = hibiki::GetOrCreateTrack(state, 0);
  std::string dexed_path = GetDexedPath();
  auto result = track->LoadPlugin(dexed_path, 0, state.sample_rate);
  ASSERT_GE(result.index, 0);
  ASSERT_EQ(track->plugins.size(), 1u);

  // Remove the plugin
  auto displaced = track->RemovePlugin(0);
  EXPECT_NE(displaced, nullptr);
  EXPECT_EQ(track->plugins.size(), 0u);

  // Save / load — track should have no plugins
  std::string tmp = std::tmpnam(nullptr);
  ASSERT_THAT(hibiki::SaveProject(state, tmp), IsOk());

  state.tracks.clear();
  ASSERT_THAT(hibiki::LoadProject(state, tmp), IsOk());
  EXPECT_EQ(state.tracks[0]->plugins.size(), 0u);

  state.tracks.clear();
  std::remove(tmp.c_str());
}

TEST_F(ProjectTest, BounceTrackClipWithMidiClip) {
  hibiki::ProjectState state;
  state.bpm = 120.0;
  state.sample_rate = 44100.0;

  auto* track = hibiki::GetOrCreateTrack(state, 0);
  std::string dexed_path = GetDexedPath();
  int pidx = track->LoadPlugin(dexed_path, 0, state.sample_rate).index;
  ASSERT_GE(pidx, 0) << "Failed to load Dexed plugin";

  // Create an in-memory MIDI clip (no file path, like piano roll editing)
  auto tc = std::make_unique<hibiki::TimelineClip>();
  tc->clip = std::make_unique<hibiki::Clip>();
  tc->clip->type = hibiki::Clip::Type::MIDI;
  tc->start_time_sec = 0.0;
  tc->duration_beats = 4.0;  // 4 beats = 2 sec at 120bpm
  tc->duration_sec = 2.0;
  // Add some MIDI notes
  hibiki::MidiEvent noteOn;
  noteOn.beats = 0.0;
  noteOn.note = 60;
  noteOn.velocity = 100;
  noteOn.type = 0x90;  // note on
  noteOn.channel = 0;
  tc->clip->midi_events.push_back(noteOn);
  hibiki::MidiEvent noteOff;
  noteOff.beats = 1.0;
  noteOff.note = 60;
  noteOff.velocity = 0;
  noteOff.type = 0x80;  // note off
  noteOff.channel = 0;
  tc->clip->midi_events.push_back(noteOff);
  track->timeline_clips.push_back(std::move(tc));

  // Deep-copy timeline clips (same procedure as handler)
  std::vector<std::unique_ptr<hibiki::TimelineClip>> clips_copy;
  for (const auto& src : track->timeline_clips) {
    if (!src || !src->clip) continue;
    auto copy = std::make_unique<hibiki::TimelineClip>();
    copy->start_time_sec = src->start_time_sec;
    copy->duration_sec = src->duration_sec;
    copy->duration_beats = src->duration_beats;
    copy->trim_start_beats = src->trim_start_beats;
    copy->alias_source = src->alias_source;
    copy->loop_interval_beats = src->loop_interval_beats;
    copy->fade_in_sec = src->fade_in_sec;
    copy->fade_out_sec = src->fade_out_sec;
    copy->muted = src->muted;
    copy->clip = std::make_unique<hibiki::Clip>();
    *copy->clip = *src->clip;
    clips_copy.push_back(std::move(copy));
  }
  ASSERT_EQ(clips_copy.size(), 1u);
  ASSERT_EQ(clips_copy[0]->clip->midi_events.size(), 2u)
      << "Deep copy should preserve MIDI events";

  // Capture snapshot (for plugin chain only)
  auto snapshot = hibiki::CaptureProjectState(state);

  std::string tmp_wav = "test_bounce_track_clip.wav";
  bool ok = hibiki::BounceTrackClip(snapshot, state.sample_rate, state.bpm, 0,
                                    clips_copy, 0.0, 2.5, tmp_wav);
  ASSERT_TRUE(ok) << "BounceTrackClip should succeed";

  // Verify output is non-silent
  std::vector<float> audio_data;
  int channels = 0;
  double duration = 0.0;
  auto loaded = hibiki::LoadWav(tmp_wav, audio_data, channels, duration);
  EXPECT_THAT(loaded, IsOk());
  EXPECT_GT(audio_data.size(), 0u) << "Should have written audio";

  float max_amp = 0.0f;
  for (float f : audio_data) {
    if (std::abs(f) > max_amp) max_amp = std::abs(f);
  }
  EXPECT_GT(max_amp, 0.0f) << "BounceTrackClip should produce non-silent "
                              "output from MIDI+instrument";

  state.tracks.clear();
  std::remove(tmp_wav.c_str());
}

TEST_F(ProjectTest, BounceTrackClipWithFades) {
  hibiki::ProjectState state;
  state.bpm = 120.0;
  state.sample_rate = 44100.0;

  auto* track = hibiki::GetOrCreateTrack(state, 0);

  // Create a 2-second mono audio clip filled with constant amplitude 1.0
  auto tc = std::make_unique<hibiki::TimelineClip>();
  tc->clip = std::make_unique<hibiki::Clip>();
  tc->clip->type = hibiki::Clip::Type::AUDIO;
  tc->clip->num_channels = 1;
  int total_samples = (int)(2.0 * state.sample_rate);
  tc->clip->audio_data.resize(total_samples, 1.0f);
  tc->clip->duration_sec = 2.0;
  tc->start_time_sec = 0.0;
  tc->duration_sec = 2.0;
  tc->fade_in_sec = 0.5;   // 0.5s fade-in
  tc->fade_out_sec = 0.5;  // 0.5s fade-out
  track->timeline_clips.push_back(std::move(tc));

  // Deep-copy timeline clips
  std::vector<std::unique_ptr<hibiki::TimelineClip>> clips_copy;
  for (const auto& src : track->timeline_clips) {
    if (!src || !src->clip) continue;
    auto copy = std::make_unique<hibiki::TimelineClip>();
    copy->start_time_sec = src->start_time_sec;
    copy->duration_sec = src->duration_sec;
    copy->duration_beats = src->duration_beats;
    copy->trim_start_beats = src->trim_start_beats;
    copy->alias_source = src->alias_source;
    copy->loop_interval_beats = src->loop_interval_beats;
    copy->fade_in_sec = src->fade_in_sec;
    copy->fade_out_sec = src->fade_out_sec;
    copy->muted = src->muted;
    copy->clip = std::make_unique<hibiki::Clip>();
    *copy->clip = *src->clip;
    clips_copy.push_back(std::move(copy));
  }

  auto snapshot = hibiki::CaptureProjectState(state);
  std::string tmp_wav = "test_bounce_fades.wav";
  bool ok = hibiki::BounceTrackClip(snapshot, state.sample_rate, state.bpm, 0,
                                    clips_copy, 0.0, 2.0, tmp_wav);
  ASSERT_TRUE(ok) << "BounceTrackClip should succeed";

  std::vector<float> audio_data;
  int channels = 0;
  double duration = 0.0;
  auto loaded = hibiki::LoadWav(tmp_wav, audio_data, channels, duration);
  EXPECT_THAT(loaded, IsOk());
  EXPECT_EQ(channels, 2);

  // Check fade-in: first 100 samples (L channel at index 0,2,4,...) should be
  // significantly attenuated compared to mid-clip. At t=0, fade gain ~= -30dB
  // ≈ 0.032, so very early samples should be well below 0.5.
  float early_max = 0.0f;
  for (int i = 0; i < 100 && i * 2 < (int)audio_data.size(); ++i) {
    early_max = std::max(early_max, std::abs(audio_data[i * 2]));
  }

  // Check middle: around 1.0 second mark, fade gain should be 1.0
  int mid_start = (int)(1.0 * state.sample_rate);
  float mid_max = 0.0f;
  for (int i = mid_start; i < mid_start + 100 && i * 2 < (int)audio_data.size();
       ++i) {
    mid_max = std::max(mid_max, std::abs(audio_data[i * 2]));
  }

  // Check fade-out: last 100 samples should be attenuated
  int end_start = (int)audio_data.size() / 2 - 100;
  float late_max = 0.0f;
  for (int i = std::max(0, end_start); i * 2 + 1 < (int)audio_data.size();
       ++i) {
    late_max = std::max(late_max, std::abs(audio_data[i * 2]));
  }

  EXPECT_GT(mid_max, 0.5f) << "Mid-clip should be near full amplitude";
  EXPECT_LT(early_max, mid_max * 0.5f)
      << "Fade-in: early samples should be significantly quieter than middle";
  EXPECT_LT(late_max, mid_max * 0.5f)
      << "Fade-out: late samples should be significantly quieter than middle";

  state.tracks.clear();
  std::remove(tmp_wav.c_str());
}

}  // namespace hibiki
