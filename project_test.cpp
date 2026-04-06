#include "project.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>

#include "audio_file.hpp"
#include "ipc.hpp"
#include "test_utils.hpp"

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
  hibiki::SaveProject(state, tmp_file);

  // Modify state before load
  state.bpm = 140.0;
  state.tracks.clear();

  // Load
  hibiki::LoadProject(state, tmp_file);

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
  hibiki::SaveProject(state, tmp_file);

  // Modify state before load
  state.tracks.clear();

  // Load
  hibiki::LoadProject(state, tmp_file);

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
  int pidx = track->LoadPlugin(dexed_path, 0, state.sample_rate);
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
  bool loaded = hibiki::LoadWav(tmp_wav, audio_data, channels, duration);

  EXPECT_TRUE(loaded) << "Should load the generated wav file";
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
  int pidx = track->LoadPlugin(dexed_path, 0, state.sample_rate);
  ASSERT_GE(pidx, 0) << "Failed to load Dexed plugin";

  std::string mid_path = hibiki::find_test_file("testdata/test.mid");
  track->AddTimelineClip(mid_path, 2.0, state.bpm);
  ASSERT_EQ(track->timeline_clips.size(), 1);

  // Save the project
  std::string tmp_file = std::tmpnam(nullptr);
  ASSERT_TRUE(hibiki::SaveProject(state, tmp_file));

  // Clear state and reload
  state.tracks.clear();
  bool result = hibiki::LoadProject(state, tmp_file);
  ASSERT_TRUE(result) << "Failed to load saved project";

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
  int pidx = track->LoadPlugin(dexed_path, 0, state.sample_rate);
  ASSERT_GE(pidx, 0) << "Failed to load Dexed plugin";

  std::string mid_path = hibiki::find_test_file("testdata/test.mid");
  track->AddTimelineClip(mid_path, 0.0, state.bpm);
  ASSERT_EQ(track->timeline_clips.size(), 1);

  // Save the project
  std::string tmp_file = "test_correct_project.hbk";
  ASSERT_TRUE(hibiki::SaveProject(state, tmp_file));

  // Clear and reload
  state.tracks.clear();
  ASSERT_TRUE(hibiki::LoadProject(state, tmp_file));

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
    std::cerr << "Correct structure: Track 1 has both Dexed plugin and MIDI "
                 "timeline clip"
              << std::endl;
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
  int pidx = track->LoadPlugin(dexed_path, 0, state.sample_rate);
  ASSERT_GE(pidx, 0) << "Failed to load Dexed plugin";
  EXPECT_EQ(track->plugins.size(), 1);

  // Step 2: Delete the plugin
  bool removed = track->RemovePlugin(0);
  EXPECT_TRUE(removed) << "Should successfully remove plugin";
  EXPECT_EQ(track->plugins.size(), 0) << "Track should now be empty";

  // Step 3: Load a new plugin to the now-empty track (this was crashing)
  int pidx2 = track->LoadPlugin(dexed_path, 0, state.sample_rate);
  ASSERT_GE(pidx2, 0) << "Failed to load Dexed plugin to empty track";
  EXPECT_EQ(track->plugins.size(), 1);

  // Clean up
  state.tracks.clear();
}

}  // namespace hibiki
