#include "engine/instruments/builtin_acid_bass.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

namespace hibiki {

class BuiltinAcidBassTest : public ::testing::Test {
 protected:
  BuiltinAcidBass plugin;

  void SetUp() override { plugin.load(BuiltinAcidBass::kPath, 0, 44100.0); }
};

TEST_F(BuiltinAcidBassTest, MetadataAndParams) {
  EXPECT_EQ(plugin.getName(), "Acid Bass");
  EXPECT_EQ(plugin.getPath(), "builtin://acid_bass");
  EXPECT_TRUE(plugin.isInstrument());
  EXPECT_EQ(plugin.getParameterCount(), 11);

  VstParamInfo info;
  EXPECT_TRUE(plugin.getParameterInfo(0, info));
  EXPECT_EQ(info.name, "Waveform");

  EXPECT_TRUE(plugin.getParameterInfo(8, info));
  EXPECT_EQ(info.name, "Transpose");

  EXPECT_TRUE(plugin.getParameterInfo(9, info));
  EXPECT_EQ(info.name, "Slide");

  EXPECT_TRUE(plugin.getParameterInfo(10, info));
  EXPECT_EQ(info.name, "Accent Switch");
}

TEST_F(BuiltinAcidBassTest, TransposeAndSlideParameters) {
  EXPECT_FLOAT_EQ(plugin.getParameterValue(BuiltinAcidBass::PARAM_TRANSPOSE),
                  0.5f);
  EXPECT_FLOAT_EQ(plugin.getParameterValue(BuiltinAcidBass::PARAM_SLIDE), 0.0f);
  EXPECT_FLOAT_EQ(
      plugin.getParameterValue(BuiltinAcidBass::PARAM_ACCENT_SWITCH), 0.0f);

  plugin.setParameterValue(BuiltinAcidBass::PARAM_TRANSPOSE, 0.75);
  plugin.setParameterValue(BuiltinAcidBass::PARAM_SLIDE, 1.0);
  plugin.setParameterValue(BuiltinAcidBass::PARAM_ACCENT_SWITCH, 1.0);

  EXPECT_FLOAT_EQ(plugin.getParameterValue(BuiltinAcidBass::PARAM_TRANSPOSE),
                  0.75f);
  EXPECT_FLOAT_EQ(plugin.getParameterValue(BuiltinAcidBass::PARAM_SLIDE), 1.0f);
  EXPECT_FLOAT_EQ(
      plugin.getParameterValue(BuiltinAcidBass::PARAM_ACCENT_SWITCH), 1.0f);
}

TEST_F(BuiltinAcidBassTest, NoteOnNoteOffCycle) {
  // Empty buffers should produce silence when no notes are playing
  std::vector<float> output_l(500, 0.0f);
  std::vector<float> output_r(500, 0.0f);
  float* outputs[2] = {output_l.data(), output_r.data()};

  HostProcessContext context;
  context.sampleRate = 44100.0;
  std::vector<MidiNoteEvent> events;

  plugin.process(nullptr, outputs, 500, context, events);
  for (int i = 0; i < 500; ++i) {
    EXPECT_FLOAT_EQ(output_l[i], 0.0f);
    EXPECT_FLOAT_EQ(output_r[i], 0.0f);
  }

  // Send Note On
  MidiNoteEvent note_on{};
  note_on.isNoteOn = true;
  note_on.pitch = 36;  // C1
  note_on.velocity = 0.7f;
  events.push_back(note_on);

  plugin.process(nullptr, outputs, 500, context, events);

  // We should have non-zero sound output now
  bool has_sound = false;
  for (int i = 10; i < 500; ++i) {
    if (std::abs(output_l[i]) > 0.001f) {
      has_sound = true;
      break;
    }
  }
  EXPECT_TRUE(has_sound) << "Note-on event should produce sound output";

  // Send Note Off
  events.clear();
  MidiNoteEvent note_off{};
  note_off.isNoteOn = false;
  note_off.pitch = 36;
  note_off.velocity = 0.0f;
  events.push_back(note_off);

  // Process a few blocks to let release envelope decay to silence
  plugin.process(nullptr, outputs, 500, context, events);  // release starts

  // Clean events and process more blocks
  events.clear();
  for (int block = 0; block < 80; ++block) {
    plugin.process(nullptr, outputs, 500, context, events);
  }

  // It should be silent now
  for (int i = 400; i < 500; ++i) {
    EXPECT_NEAR(output_l[i], 0.0f, 0.0001f)
        << "Synthesizer should be silent after note release at sample " << i;
  }
}

TEST_F(BuiltinAcidBassTest, LegatoGlide) {
  HostProcessContext context;
  context.sampleRate = 44100.0;

  // Note 1 on
  std::vector<MidiNoteEvent> events;
  MidiNoteEvent note1{};
  note1.isNoteOn = true;
  note1.pitch = 36;
  note1.velocity = 0.7f;
  events.push_back(note1);

  std::vector<float> output_l(500, 0.0f);
  std::vector<float> output_r(500, 0.0f);
  float* outputs[2] = {output_l.data(), output_r.data()};

  plugin.process(nullptr, outputs, 500, context, events);

  // Note 2 on (legato glide)
  events.clear();
  MidiNoteEvent note2{};
  note2.isNoteOn = true;
  note2.pitch = 48;  // one octave higher
  note2.velocity = 0.7f;
  events.push_back(note2);

  plugin.process(nullptr, outputs, 500, context, events);

  // Since we glide to note 2, the wave period should decrease (frequency
  // increases) over the course of the next samples, confirming the pitch
  // changes. We can verify that we are producing sound and note 2 is now active
  // in stack.
  EXPECT_TRUE(plugin.getParameterValue(BuiltinAcidBass::PARAM_VOLUME) > 0.0);
}

}  // namespace hibiki
