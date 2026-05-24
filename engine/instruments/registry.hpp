#pragma once

#include <memory>
#include <string>

#include "engine/plugin/iplugin.hpp"

namespace hibiki {

std::unique_ptr<IPlugin> createBuiltinInstrument(const std::string& path);

}  // namespace hibiki
