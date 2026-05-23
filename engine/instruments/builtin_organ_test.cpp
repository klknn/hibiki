#include "engine/instruments/builtin_organ.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

namespace hibiki {

class BuiltinOrganTest : public ::testing::Test {
 protected:
  BuiltinOrgan plugin;

  void SetUp() override { plugin.load(BuiltinOrgan::kPath, 0, 44100.0); }
};

TEST_F(BuiltinOrganTest, MetadataAndParams) {
  EXPECT_EQ(plugin.getName(), "Drawbar Organ");
  EXPECT_EQ(plugin.getPath(), "builtin://organ");
  EXPECT_TRUE(plugin.isInstrument());
  EXPECT_EQ(plugin.getParameterCount(), 13);

  VstParamInfo info;
  EXPECT_TRUE(plugin.getParameterInfo(0, info));
  EXPECT_EQ(info.name, "Drawbar 1 (16')");
}

TEST_F(BuiltinOrganTest, NotePlayAndRelease) {
  std::vector<float> output_l(500, 0.0f);
  std::vector<float> output_r(500, 0.0f);
  float* outputs[2] = {output_l.data(), output_r.data()};

  HostProcessContext context;
  context.sampleRate = 44100.0;
  std::vector<MidiNoteEvent> events;

  // Empty process is silent
  plugin.process(nullptr, outputs, 500, context, events);
  for (int i = 0; i < 500; ++i) {
    EXPECT_FLOAT_EQ(output_l[i], 0.0f);
    EXPECT_FLOAT_EQ(output_r[i], 0.0f);
  }

  // Trigger Note
  MidiNoteEvent note_on{};
  note_on.isNoteOn = true;
  note_on.pitch = 60;  // Middle C
  note_on.velocity = 0.8f;
  events.push_back(note_on);

  plugin.process(nullptr, outputs, 500, context, events);

  // Output should be non-zero
  bool has_sound = false;
  for (int i = 10; i < 500; ++i) {
    if (std::abs(output_l[i]) > 0.001f) {
      has_sound = true;
      break;
    }
  }
  EXPECT_TRUE(has_sound) << "Organ should produce sound when note is held";

  // Trigger Note Off
  events.clear();
  MidiNoteEvent note_off{};
  note_off.isNoteOn = false;
  note_off.pitch = 60;
  note_off.velocity = 0.0f;
  events.push_back(note_off);

  plugin.process(nullptr, outputs, 500, context, events);  // release starts

  // Process four more blocks to let the release envelope (1218 samples decay)
  // and Leslie delay line clear completely
  events.clear();
  plugin.process(nullptr, outputs, 500, context, events);
  plugin.process(nullptr, outputs, 500, context, events);
  plugin.process(nullptr, outputs, 500, context, events);
  plugin.process(nullptr, outputs, 500, context, events);

  // It should be completely silent now
  for (int i = 0; i < 500; ++i) {
    EXPECT_NEAR(output_l[i], 0.0f, 0.0001f)
        << "Organ should quickly cut to silence on note off, sample " << i;
  }
}

TEST_F(BuiltinOrganTest, LeslieModulation) {
  // Speed it up to fast Leslie to get dramatic modulation
  plugin.setParameterValue(BuiltinOrgan::PARAM_ROTARY_SPEED,
                           1.0);  // fast Leslie

  // Hold a note
  MidiNoteEvent note_on{};
  note_on.isNoteOn = true;
  note_on.pitch = 60;
  note_on.velocity = 0.8f;
  std::vector<MidiNoteEvent> events = {note_on};

  std::vector<float> output_l(1000, 0.0f);
  std::vector<float> output_r(1000, 0.0f);
  float* outputs[2] = {output_l.data(), output_r.data()};

  HostProcessContext context;
  context.sampleRate = 44100.0;

  plugin.process(nullptr, outputs, 1000, context, events);

  // Left and Right channels should differ due to the 90-degree phase-shifted
  // Leslie delay/tremolo
  bool channels_differ = false;
  for (int i = 200; i < 1000; ++i) {
    if (std::abs(output_l[i] - output_r[i]) > 0.01f) {
      channels_differ = true;
      break;
    }
  }
  EXPECT_TRUE(channels_differ) << "Leslie speaker emulation should create "
                                  "stereo differences between channels";
}

}  // namespace hibiki
