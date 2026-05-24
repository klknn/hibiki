#include "engine/builtin_registry.hpp"

#include "engine/effects/registry.hpp"
#include "engine/instruments/builtin_drum_machine.hpp"
#include "engine/instruments/registry.hpp"

namespace hibiki {

std::unique_ptr<IPlugin> createBuiltinPlugin(const std::string& path) {
  if (path == BuiltinDrumMachine::kPath) {
    return std::make_unique<BuiltinDrumMachine>();
  }
  if (auto effect = createBuiltinEffect(path)) {
    return effect;
  }
  if (auto inst = createBuiltinInstrument(path)) {
    return inst;
  }
  return nullptr;
}

bool isBuiltinPluginPath(const std::string& path) {
  return path.rfind("builtin://", 0) == 0;
}

}  // namespace hibiki
