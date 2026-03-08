#pragma once

#include <vector>
#include <deque>
#include <cstdint>

namespace hibiki {

class HistoryManager {
public:
    void pushState(const std::vector<uint8_t>& state);
    bool undo(std::vector<uint8_t>& current_state, std::vector<uint8_t>& out_state);
    bool redo(std::vector<uint8_t>& current_state, std::vector<uint8_t>& out_state);

private:
    std::deque<std::vector<uint8_t>> undo_stack_;
    std::deque<std::vector<uint8_t>> redo_stack_;
    const size_t max_history_ = 50;
};

} // namespace hibiki
