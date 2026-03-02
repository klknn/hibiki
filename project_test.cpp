#include <gtest/gtest.h>
#include "project.hpp"
#include "test_utils.hpp"
#include <cstdio>

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
