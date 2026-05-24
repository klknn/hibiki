#pragma once

#include <memory>
#include <string>

#include "engine/plugin/iplugin.hpp"

namespace hibiki {

std::unique_ptr<IPlugin> createBuiltinEffect(const std::string& path);

}  // namespace hibiki
