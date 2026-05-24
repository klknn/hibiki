#include "engine/instruments/registry.hpp"

#include "engine/instruments/builtin_3xosc.hpp"
#include "engine/instruments/builtin_acid_bass.hpp"
#include "engine/instruments/builtin_dr8_clap.hpp"
#include "engine/instruments/builtin_dr8_conga.hpp"
#include "engine/instruments/builtin_dr8_cowbell.hpp"
#include "engine/instruments/builtin_dr8_crash.hpp"
#include "engine/instruments/builtin_dr8_hat.hpp"
#include "engine/instruments/builtin_dr8_kick.hpp"
#include "engine/instruments/builtin_dr8_rim.hpp"
#include "engine/instruments/builtin_dr8_snare.hpp"
#include "engine/instruments/builtin_dr8_tom.hpp"
#include "engine/instruments/builtin_film.hpp"
#include "engine/instruments/builtin_organ.hpp"
#include "engine/instruments/builtin_sampler.hpp"

namespace hibiki {

std::unique_ptr<IPlugin> createBuiltinInstrument(const std::string& path) {
  if (path == Builtin3xOsc::kPath) {
    return std::make_unique<Builtin3xOsc>();
  } else if (path == BuiltinAcidBass::kPath) {
    return std::make_unique<BuiltinAcidBass>();
  } else if (path == BuiltinDr8Kick::kPath) {
    return std::make_unique<BuiltinDr8Kick>();
  } else if (path == BuiltinDr8Snare::kPath) {
    return std::make_unique<BuiltinDr8Snare>();
  } else if (path == BuiltinDr8Hat::kPath) {
    return std::make_unique<BuiltinDr8Hat>();
  } else if (path == BuiltinDr8Tom::kPath) {
    return std::make_unique<BuiltinDr8Tom>();
  } else if (path == BuiltinDr8Clap::kPath) {
    return std::make_unique<BuiltinDr8Clap>();
  } else if (path == BuiltinDr8Cowbell::kPath) {
    return std::make_unique<BuiltinDr8Cowbell>();
  } else if (path == BuiltinDr8Crash::kPath) {
    return std::make_unique<BuiltinDr8Crash>();
  } else if (path == BuiltinDr8Rim::kPath) {
    return std::make_unique<BuiltinDr8Rim>();
  } else if (path == BuiltinDr8Conga::kPath) {
    return std::make_unique<BuiltinDr8Conga>();
  } else if (path == BuiltinOrgan::kPath) {
    return std::make_unique<BuiltinOrgan>();
  } else if (path == BuiltinSampler::kPath) {
    return std::make_unique<BuiltinSampler>();
  } else if (path.rfind(BuiltinFilm::kPath, 0) == 0) {
    return std::make_unique<BuiltinFilm>();
  }
  return nullptr;
}

}  // namespace hibiki
