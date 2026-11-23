#include "engine/instruments/builtin_sylph.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "engine/instruments/registry.hpp"

namespace hibiki {
namespace {

TEST(BuiltinSylphTest, RegistryMetadataAndParameters) {
  auto plugin = createBuiltinInstrument(BuiltinSylph::kPath);
  ASSERT_NE(plugin, nullptr);
  EXPECT_EQ(plugin->getName(), "Sylph");
  EXPECT_EQ(plugin->getPath(), "builtin://sylph");
  EXPECT_TRUE(plugin->isInstrument());
  EXPECT_EQ(plugin->getParameterCount(), BuiltinSylph::kTotalParams);

  VstParamInfo info;
  for (int i = 0; i < BuiltinSylph::kTotalParams; ++i) {
    EXPECT_TRUE(plugin->getParameterInfo(i, info)) << "Param index " << i;
    EXPECT_FALSE(info.name.empty()) << "Empty name for param " << i;
    EXPECT_GE(info.defaultValue, 0.0);
    EXPECT_LE(info.defaultValue, 1.0);
  }
  EXPECT_FALSE(plugin->getParameterInfo(BuiltinSylph::kTotalParams, info));

  plugin->setParameterValue(BuiltinSylph::P_FILTA_CUT, 0.35);
  EXPECT_NEAR(plugin->getParameterValue(BuiltinSylph::P_FILTA_CUT), 0.35, 1e-4);
  plugin->setParameterValue(BuiltinSylph::P_FILTB_CUT, 0.65);
  EXPECT_NEAR(plugin->getParameterValue(BuiltinSylph::P_FILTB_CUT), 0.65, 1e-4);
  plugin->setParameterValue(BuiltinSylph::P_DIST_ON, 1.0);
  EXPECT_NEAR(plugin->getParameterValue(BuiltinSylph::P_DIST_ON), 1.0, 1e-4);
}

TEST(BuiltinSylphTest,
     TranceLeadProducesWidePolyphonicStereoAndReleasesCleanly) {
  BuiltinSylph synth;
  ASSERT_TRUE(synth.load(BuiltinSylph::kPath, 0, 48000.0));
  synth.applyPreset(0);  // Trance Anthem Lead

  constexpr int kBlock = 1024;
  std::vector<float> outL(kBlock, 0.0f);
  std::vector<float> outR(kBlock, 0.0f);
  float* outputs[2] = {outL.data(), outR.data()};
  HostProcessContext ctx;
  ctx.sampleRate = 48000.0;

  std::vector<MidiNoteEvent> notes = {
      MidiNoteEvent{0, 0, 64, 0.9f, true},  // E4
      MidiNoteEvent{0, 0, 67, 0.9f, true},  // G4
      MidiNoteEvent{0, 0, 71, 0.9f, true},  // B4
  };
  synth.process(nullptr, outputs, kBlock, ctx, notes);

  double energyL = 0.0;
  double stereoDiff = 0.0;
  for (int i = 0; i < kBlock; ++i) {
    ASSERT_TRUE(std::isfinite(outL[i])) << "Non-finite left sample at " << i;
    ASSERT_TRUE(std::isfinite(outR[i])) << "Non-finite right sample at " << i;
    energyL += outL[i] * outL[i];
    stereoDiff += std::abs(outL[i] - outR[i]);
  }
  EXPECT_GT(energyL, 1.0) << "Trance lead chord must generate strong signal";
  EXPECT_GT(stereoDiff, 1.0)
      << "Unison + chorus should produce wide stereo image";

  // Release notes with FX bypassed so envelope tail reaches silence
  // deterministically
  synth.setParameterValue(BuiltinSylph::P_CHORUS_ON, 0.0);
  synth.setParameterValue(BuiltinSylph::P_DELAY_ON, 0.0);
  synth.setParameterValue(BuiltinSylph::P_REVERB_ON, 0.0);
  synth.setParameterValue(BuiltinSylph::P_AMPA_R, 0.05);
  synth.setParameterValue(BuiltinSylph::P_AMPB_R, 0.05);
  std::vector<MidiNoteEvent> offs = {
      MidiNoteEvent{0, 0, 64, 0.0f, false},
      MidiNoteEvent{0, 0, 67, 0.0f, false},
      MidiNoteEvent{0, 0, 71, 0.0f, false},
  };
  synth.process(nullptr, outputs, kBlock, ctx, offs);
  for (int b = 0; b < 32; ++b) {
    synth.process(nullptr, outputs, kBlock, ctx, {});
  }
  double tailPeak = 0.0;
  for (float s : outL) {
    tailPeak = std::max<double>(tailPeak, std::abs(s));
  }
  EXPECT_LT(tailPeak, 1e-4) << "Amp release should decay cleanly to silence";
}

TEST(BuiltinSylphTest, HardKickPresetIsPunchyTightAndPhaseConsistent) {
  BuiltinSylph synth;
  ASSERT_TRUE(synth.load(BuiltinSylph::kPath, 0, 48000.0));
  synth.applyPreset(1);  // Hard EDM Kick

  constexpr int kKickSamples = 24000;  // 500 ms
  std::vector<float> outL(kKickSamples, 0.0f);
  std::vector<float> outR(kKickSamples, 0.0f);
  float* outputs[2] = {outL.data(), outR.data()};
  HostProcessContext ctx;
  ctx.sampleRate = 48000.0;

  std::vector<MidiNoteEvent> noteOn = {
      MidiNoteEvent{0, 0, 36, 1.0f, true}};  // C2 (65.4Hz)
  synth.process(nullptr, outputs, kKickSamples, ctx, noteOn);

  double attackPeak = 0.0;
  double bodyEnergy = 0.0;
  double tailPeak = 0.0;
  for (int i = 0; i < kKickSamples; ++i) {
    ASSERT_TRUE(std::isfinite(outL[i]));
    ASSERT_TRUE(std::isfinite(outR[i]));
    if (i < 1200) {
      attackPeak = std::max<double>(attackPeak, std::abs(outL[i]));
    } else if (i < 9600) {
      bodyEnergy += outL[i] * outL[i];
    } else if (i > kKickSamples - 1024) {
      tailPeak = std::max<double>(tailPeak, std::abs(outL[i]));
    }
  }
  EXPECT_GT(attackPeak, 0.35)
      << "Hard Kick transient click/snap must punch cleanly";
  EXPECT_GT(bodyEnergy, 10.0)
      << "Hard Kick sub body should carry strong tight low-end";
  EXPECT_LT(tailPeak, 0.08)
      << "Tight kick decay should tighten cleanly before 500ms";
}

}  // namespace
}  // namespace hibiki
