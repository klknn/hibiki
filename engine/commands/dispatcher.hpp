#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/core/history.hpp"
#include "engine/core/project.hpp"

namespace hibiki {

bool dispatchIpcCommand(const uint8_t* data, size_t size, ProjectState& state,
                        HistoryManager& history);

}  // namespace hibiki
