#pragma once

#include <vector>
#include <deque>
#include <cstdint>

namespace hibiki {

class HistoryManager {
public:
    void pushState(const std::vector<uint8_t>& state) {
        undo_stack_.push_back(state);
        if (undo_stack_.size() > max_history_) {
            undo_stack_.pop_front();
        }
        redo_stack_.clear();
    }

    bool undo(std::vector<uint8_t>& current_state, std::vector<uint8_t>& out_state) {
        if (undo_stack_.empty()) return false;
        
        redo_stack_.push_back(current_state);
        out_state = undo_stack_.back();
        undo_stack_.pop_back();
        return true;
    }

    bool redo(std::vector<uint8_t>& current_state, std::vector<uint8_t>& out_state) {
        if (redo_stack_.empty()) return false;
        
        undo_stack_.push_back(current_state);
        out_state = redo_stack_.back();
        redo_stack_.pop_back();
        return true;
    }

private:
    std::deque<std::vector<uint8_t>> undo_stack_;
    std::deque<std::vector<uint8_t>> redo_stack_;
    const size_t max_history_ = 50;
};

} // namespace hibiki
