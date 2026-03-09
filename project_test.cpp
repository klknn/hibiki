#include <gtest/gtest.h>
#include "project.hpp"
#include "test_utils.hpp"
#include "audio_file.hpp"
#include "ipc.hpp"
#include <cstdio>
#include <cmath>

void Vst3Plugin::stopEditor() {}  // Stub for test linking.

class ProjectTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Disable IPC to prevent blocking on stdout writes during tests
        hibiki::g_ipc_enabled = false;
    }
};

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
    std::string dexed_path = hibiki::find_test_file("testdata/Dexed.vst3");
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
    state.sample_rate = 44100.0;
    
    // Load the timeline_midi.hbk project file
    std::string project_path = hibiki::find_test_file("testdata/timeline_midi.hbk");
    bool result = hibiki::LoadProject(state, project_path);
    ASSERT_TRUE(result) << "Failed to load timeline_midi.hbk";
    
    // Verify BPM was loaded
    EXPECT_GT(state.bpm, 0.0) << "BPM should be set";
    std::cerr << "Loaded BPM: " << state.bpm << std::endl;
    
    // Verify tracks were created
    EXPECT_GT(state.tracks.size(), 0) << "Should have at least one track";
    std::cerr << "Number of tracks: " << state.tracks.size() << std::endl;
    
    // Check each track
    for (const auto& [idx, track] : state.tracks) {
        std::cerr << "Track " << idx << ":" << std::endl;
        std::cerr << "  Plugins: " << track->plugins.size() << std::endl;
        std::cerr << "  Session Clips: " << track->clips.size() << std::endl;
        std::cerr << "  Timeline Clips: " << track->timeline_clips.size() << std::endl;
        
        for (size_t p = 0; p < track->plugins.size(); ++p) {
            std::cerr << "    Plugin " << p << ": " << track->plugins[p]->getName() 
                      << " (isInstrument=" << track->plugins[p]->isInstrument() << ")" << std::endl;
        }
        
        for (size_t tc_idx = 0; tc_idx < track->timeline_clips.size(); ++tc_idx) {
            const auto& tc = track->timeline_clips[tc_idx];
            std::cerr << "    Timeline clip " << tc_idx << ": " << tc->clip->path 
                      << " (type=" << (tc->clip->type == hibiki::Clip::Type::MIDI ? "MIDI" : "AUDIO")
                      << ", start=" << tc->start_time_sec
                      << ", duration=" << tc->duration_sec << ")" << std::endl;
        }
        
        // Verify MIDI timeline clips need a plugin on the same track
        if (!track->timeline_clips.empty() && track->plugins.empty()) {
            for (const auto& tc : track->timeline_clips) {
                if (tc->clip->type == hibiki::Clip::Type::MIDI) {
                    std::cerr << "  WARNING: MIDI timeline clip without plugin on track " << idx << std::endl;
                }
            }
        }
    }
    
    // Clean up
    state.tracks.clear();
}

// Test that a correctly structured project (plugin + MIDI on same track) plays back correctly
TEST_F(ProjectTest, SaveAndLoadCorrectProjectStructure) {
    hibiki::ProjectState state;
    state.bpm = 120.0;
    state.sample_rate = 44100.0;
    
    // Create a track with BOTH plugin AND timeline clip (correct structure)
    auto track = hibiki::GetOrCreateTrack(state, 1);
    std::string dexed_path = hibiki::find_test_file("testdata/Dexed.vst3");
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
    EXPECT_EQ(loaded_track->timeline_clips.size(), 1) << "Should have 1 timeline clip";
    
    if (!loaded_track->plugins.empty() && !loaded_track->timeline_clips.empty()) {
        EXPECT_TRUE(loaded_track->plugins[0]->isInstrument()) << "Plugin should be instrument";
        EXPECT_EQ(loaded_track->timeline_clips[0]->clip->type, hibiki::Clip::Type::MIDI);
        std::cerr << "Correct structure: Track 1 has both Dexed plugin and MIDI timeline clip" << std::endl;
    }
    
    // Clean up
    state.tracks.clear();
    std::remove(tmp_file.c_str());
}

// Test for bug: crash when deleting plugin then loading new plugin to empty track
TEST_F(ProjectTest, DeletePluginThenLoadNew) {
    hibiki::ProjectState state;
    state.bpm = 120.0;
    state.sample_rate = 44100.0;
    
    auto track = hibiki::GetOrCreateTrack(state, 0);
    std::string dexed_path = hibiki::find_test_file("testdata/Dexed.vst3");
    
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
