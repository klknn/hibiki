#include "engine/effects/registry.hpp"

#include "engine/effects/builtin_aux.hpp"
#include "engine/effects/builtin_bitcrusher.hpp"
#include "engine/effects/builtin_chorus.hpp"
#include "engine/effects/builtin_compressor.hpp"
#include "engine/effects/builtin_convolver.hpp"
#include "engine/effects/builtin_delay.hpp"
#include "engine/effects/builtin_envelope_shaper.hpp"
#include "engine/effects/builtin_eq.hpp"
#include "engine/effects/builtin_hott.hpp"
#include "engine/effects/builtin_limiter.hpp"
#include "engine/effects/builtin_maxim.hpp"
#include "engine/effects/builtin_phaser.hpp"
#include "engine/effects/builtin_reverb.hpp"
#include "engine/effects/builtin_stereo_width.hpp"
#include "engine/effects/builtin_vocodey.hpp"

namespace hibiki {

std::unique_ptr<IPlugin> createBuiltinEffect(const std::string& path) {
  if (path == BuiltinAux::kPath) {
    return std::make_unique<BuiltinAux>();
  } else if (path == BuiltinEq::kPath) {
    return std::make_unique<BuiltinEq>();
  } else if (path == BuiltinCompressor::kPath) {
    return std::make_unique<BuiltinCompressor>();
  } else if (path == BuiltinDelay::kPath) {
    return std::make_unique<BuiltinDelay>();
  } else if (path == BuiltinReverb::kPath) {
    return std::make_unique<BuiltinReverb>();
  } else if (path == BuiltinLimiter::kPath) {
    return std::make_unique<BuiltinLimiter>();
  } else if (path == BuiltinBitcrusher::kPath) {
    return std::make_unique<BuiltinBitcrusher>();
  } else if (path == BuiltinChorus::kPath) {
    return std::make_unique<BuiltinChorus>();
  } else if (path == BuiltinStereoWidth::kPath) {
    return std::make_unique<BuiltinStereoWidth>();
  } else if (path == BuiltinHott::kPath) {
    return std::make_unique<BuiltinHott>();
  } else if (path == BuiltinMaxim::kPath) {
    return std::make_unique<BuiltinMaxim>();
  } else if (path == BuiltinVocodey::kPath) {
    return std::make_unique<BuiltinVocodey>();
  } else if (path == BuiltinEnvelopeShaper::kPath) {
    return std::make_unique<BuiltinEnvelopeShaper>();
  } else if (path == BuiltinPhaser::kPath) {
    return std::make_unique<BuiltinPhaser>();
  } else if (path.rfind(BuiltinConvolver::kPath, 0) == 0) {
    return std::make_unique<BuiltinConvolver>();
  }
  return nullptr;
}

}  // namespace hibiki
