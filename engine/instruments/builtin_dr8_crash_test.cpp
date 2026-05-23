#include "engine/instruments/builtin_dr8_crash.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

namespace hibiki {

class BuiltinDr8CrashTest : public ::testing::Test {
 protected:
  BuiltinDr8Crash plugin;

  void SetUp() override { plugin.load(BuiltinDr8Crash::kPath, 0, 44100.0); }
};

TEST_F(BuiltinDr8CrashTest, MetadataAndParams) {
  EXPECT_EQ(plugin.getName(), "DR8 Crash");
  EXPECT_EQ(plugin.getPath(), "builtin://dr8_crash");
  EXPECT_TRUE(plugin.isInstrument());
  EXPECT_EQ(plugin.getParameterCount(), 4);

  VstParamInfo info;
  EXPECT_TRUE(plugin.getParameterInfo(0, info));
  EXPECT_EQ(info.name, "Decay");
}

TEST_F(BuiltinDr8CrashTest, PlayAndDecay) {
  std::vector<float> output_l(500, 0.0f);
  std::vector<float> output_r(500, 0.0f);
  float* outputs[2] = {output_l.data(), output_r.data()};

  HostProcessContext context;
  context.sampleRate = 44100.0;
  std::vector<MidiNoteEvent> events;

  // Verify silent when idle
  plugin.process(nullptr, outputs, 500, context, events);
  for (int i = 0; i < 500; ++i) {
    EXPECT_FLOAT_EQ(output_l[i], 0.0f);
    EXPECT_FLOAT_EQ(output_r[i], 0.0f);
  }

  // Trigger Crash note
  plugin.setParameterValue(BuiltinDr8Crash::PARAM_DECAY, 0.0);  // fast decay
  MidiNoteEvent note_on{};
  note_on.isNoteOn = true;
  note_on.pitch = 49;  // Crash Cymbal C#1
  note_on.velocity = 0.9f;
  events.push_back(note_on);

  plugin.process(nullptr, outputs, 500, context, events);

  // Output should have non-zero crash sound
  bool has_sound = false;
  for (int i = 5; i < 500; ++i) {
    if (std::abs(output_l[i]) > 0.001f) {
      has_sound = true;
      break;
    }
  }
  EXPECT_TRUE(has_sound) << "Crash cymbal should produce sound when triggered";

  // Let it play until it decays completely to silence (decay = 0.0 -> 0.2
  // seconds = 8820 samples = 18 blocks)
  events.clear();
  for (int block = 0; block < 200; ++block) {
    plugin.process(nullptr, outputs, 500, context, events);
  }

  // Now it must be silent
  for (int i = 0; i < 500; ++i) {
    EXPECT_NEAR(output_l[i], 0.0f, 0.0001f)
        << "Crash cymbal should decay to silence after playing out, sample "
        << i;
  }
}

}  // namespace hibiki
