#include "engine/core/clip.hpp"

#include <gtest/gtest.h>

#include <fstream>

#include "engine/test_utils.hpp"

namespace hibiki {

TEST(ClipTest, LoadAudioClip) {
  auto clip =
      hibiki::LoadClip(hibiki::find_test_file("testdata/loop140.wav"), true);
  ASSERT_NE(clip, nullptr);
  EXPECT_EQ(clip->type, hibiki::Clip::Type::AUDIO);
  EXPECT_TRUE(clip->is_loop);
  EXPECT_GT(clip->audio_data.size(), 0);
  EXPECT_GT(clip->duration_sec, 0.0);
}

TEST(ClipTest, LoadMidiClip) {
  auto clip = hibiki::LoadClip(hibiki::find_test_file("testdata/test.mid"));
  ASSERT_NE(clip, nullptr);
  EXPECT_EQ(clip->type, hibiki::Clip::Type::MIDI);
  EXPECT_FALSE(clip->is_loop);
  EXPECT_GT(clip->midi_events.size(), 0);
  EXPECT_GT(clip->duration_beats,
            0.0);  // MIDI clips use duration_beats, not duration_sec
}

TEST(ClipTest, LoadClipFileNotFound) {
  auto clip = hibiki::LoadClip("/nonexistent/path/to/clip.wav");
  EXPECT_EQ(clip, nullptr) << "LoadClip should return nullptr for missing file";
}

TEST(ClipTest, LoadClipCorruptedWav) {
  // Create a file with .wav extension but garbage content
  std::string tmp = "test_corrupt.wav";
  {
    std::ofstream out(tmp, std::ios::binary);
    out << "THIS_IS_NOT_A_WAV_FILE";
  }
  auto clip = hibiki::LoadClip(tmp);
  EXPECT_EQ(clip, nullptr)
      << "LoadClip should return nullptr for corrupted WAV";
  std::remove(tmp.c_str());
}

}  // namespace hibiki
