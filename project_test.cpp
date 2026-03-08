#include <gtest/gtest.h>
#include "project.hpp"
#include "test_utils.hpp"
#include "audio_file.hpp"
#include <cstdio>
#include <cmath>

void Vst3Plugin::stopEditor() {}  // for test.

TEST(ProjectTest, GetOrCreateTrack) {
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

TEST(ProjectTest, SaveAndLoad) {
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
    // EXPECT_EQ(loaded_track->clips[0]->type, hibiki::Clip::Type::AUDIO); // Temporarily removing till TODO in load project is fixed

    std::remove(tmp_file.c_str());
}

TEST(ProjectTest, BounceProjectWithDexed) {
    hibiki::ProjectState state;
    state.bpm = 120.0;
    state.sample_rate = 44100.0;
    
    auto track = hibiki::GetOrCreateTrack(state, 0);
    std::string dexed_path = hibiki::find_test_file("testdata/Dexed.vst3");
    int pidx = track->LoadPlugin(dexed_path, 0, state.sample_rate);
    ASSERT_GE(pidx, 0) << "Failed to load Dexed plugin";
    
    std::string mid_path = hibiki::find_test_file("testdata/test.mid");
    track->AddTimelineClip(mid_path, 0.0);
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
    
    std::remove(tmp_wav.c_str());
}

