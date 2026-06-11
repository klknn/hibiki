#include "engine/builtin_registry.hpp"

#include <map>

#include "engine/effects/registry.hpp"
#include "engine/instruments/builtin_drum_machine.hpp"
#include "engine/instruments/registry.hpp"

namespace hibiki {

static std::map<std::string, BuiltinPluginFactory>& getCustomRegistry() {
  static auto* registry = new std::map<std::string, BuiltinPluginFactory>();
  return *registry;
}

void registerTestBuiltinPlugin(const std::string& path,
                               BuiltinPluginFactory factory) {
  getCustomRegistry()[path] = factory;
}

std::unique_ptr<IPlugin> createBuiltinPlugin(const std::string& path) {
  if (path == BuiltinDrumMachine::kPath) {
    return std::make_unique<BuiltinDrumMachine>();
  }

  auto& custom = getCustomRegistry();
  auto it = custom.find(path);
  if (it != custom.end()) {
    return it->second(path);
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
