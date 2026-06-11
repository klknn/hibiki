#pragma once

#include <functional>
#include <memory>
#include <string>

#include "engine/plugin/iplugin.hpp"

namespace hibiki {

using BuiltinPluginFactory =
    std::function<std::unique_ptr<IPlugin>(const std::string&)>;

// Registers a custom plugin factory at runtime (e.g. for testing mock plugins).
void registerTestBuiltinPlugin(const std::string& path,
                               BuiltinPluginFactory factory);

std::unique_ptr<IPlugin> createBuiltinPlugin(const std::string& path);
bool isBuiltinPluginPath(const std::string& path);

}  // namespace hibiki
