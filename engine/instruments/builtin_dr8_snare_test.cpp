#include "engine/instruments/builtin_dr8_snare.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

namespace hibiki {

class BuiltinDr8SnareTest : public ::testing::Test {
 protected:
  BuiltinDr8Snare plugin;

  void SetUp() override { plugin.load(BuiltinDr8Snare::kPath, 0, 44100.0); }
};

TEST_F(BuiltinDr8SnareTest, MetadataAndParams) {
  EXPECT_EQ(plugin.getName(), "DR8 Snare");
  EXPECT_EQ(plugin.getPath(), "builtin://dr8_snare");
  EXPECT_TRUE(plugin.isInstrument());
  EXPECT_EQ(plugin.getParameterCount(), 7);

  VstParamInfo info;
  EXPECT_TRUE(plugin.getParameterInfo(0, info));
  EXPECT_EQ(info.name, "Pitch");
}

TEST_F(BuiltinDr8SnareTest, PlayAndDecay) {
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

  // Trigger Snare note
  plugin.setParameterValue(BuiltinDr8Snare::PARAM_DECAY, 0.0);  // fast decay
  plugin.setParameterValue(BuiltinDr8Snare::PARAM_NOISE_DECAY,
                           0.0);  // fast noise decay
  MidiNoteEvent note_on{};
  note_on.isNoteOn = true;
  note_on.pitch = 38;  // Snare D1
  note_on.velocity = 0.9f;
  events.push_back(note_on);

  plugin.process(nullptr, outputs, 500, context, events);

  // Output should have non-zero snare sound
  bool has_sound = false;
  for (int i = 5; i < 500; ++i) {
    if (std::abs(output_l[i]) > 0.001f) {
      has_sound = true;
      break;
    }
  }
  EXPECT_TRUE(has_sound) << "Snare drum should produce sound when triggered";

  // Let it play until it decays completely to silence (decay = 0.0 -> 0.05
  // seconds = 2205 samples = 5 blocks)
  events.clear();
  for (int block = 0; block < 100; ++block) {
    plugin.process(nullptr, outputs, 500, context, events);
  }

  // Now it must be silent
  for (int i = 0; i < 500; ++i) {
    EXPECT_NEAR(output_l[i], 0.0f, 0.0001f)
        << "Snare drum should decay to silence after playing out, sample " << i;
  }
}

}  // namespace hibiki
