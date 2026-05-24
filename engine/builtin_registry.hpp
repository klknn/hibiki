#pragma once

#include <memory>
#include <string>

#include "engine/plugin/iplugin.hpp"

namespace hibiki {

std::unique_ptr<IPlugin> createBuiltinPlugin(const std::string& path);
bool isBuiltinPluginPath(const std::string& path);

}  // namespace hibiki
