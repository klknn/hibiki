module engine.core.history;

/// @safe D undo/redo history manager with bounded capacity (default 50 states).
struct HistoryManagerD {
    private ubyte[][] undoRing_;
    private size_t[] undoLens_;
    private size_t undoHead_ = 0;
    private size_t undoCount_ = 0;
    private ubyte[][] redoStack_;
    private size_t[] redoLens_;
    private size_t redoCount_ = 0;

    /// Maximum number of undo snapshots retained.
    size_t maxHistory = 50;

    private static void copyIntoSlot(ref ubyte[] slot, const(ubyte)[] src) pure nothrow @safe {
        if (slot.length < src.length) {
            slot = new ubyte[](src.length);
        }
        slot[0 .. src.length] = src[];
    }

    /// Pushes a snapshot onto the undo stack and clears the redo stack.
    void pushState(const(ubyte)[] state) pure nothrow @safe {
        if (maxHistory == 0) return;
        if (undoRing_.length < maxHistory) {
            undoRing_.length = maxHistory;
            undoLens_.length = maxHistory;
        }
        size_t idx;
        if (undoCount_ >= maxHistory) {
            idx = undoHead_;
            undoHead_ = (undoHead_ + 1) % maxHistory;
        } else {
            idx = (undoHead_ + undoCount_) % maxHistory;
            undoCount_++;
        }
        copyIntoSlot(undoRing_[idx], state);
        undoLens_[idx] = state.length;
        redoCount_ = 0;
    }

    /// Pops the latest undo snapshot into `outState` and records `currentState` for redo.
    bool undo(const(ubyte)[] currentState, out ubyte[] outState) pure nothrow @safe {
        if (undoCount_ == 0) {
            return false;
        }
        if (redoStack_.length <= redoCount_) {
            size_t newCap = redoStack_.length == 0 ? 16 : redoStack_.length * 2;
            redoStack_.length = newCap;
            redoLens_.length = newCap;
        }
        copyIntoSlot(redoStack_[redoCount_], currentState);
        redoLens_[redoCount_] = currentState.length;
        redoCount_++;

        undoCount_--;
        size_t topIdx = (undoHead_ + undoCount_) % undoRing_.length;
        outState = undoRing_[topIdx][0 .. undoLens_[topIdx]];
        undoRing_[topIdx] = null;
        undoLens_[topIdx] = 0;
        return true;
    }

    /// Pops the latest redo snapshot into `outState` and pushes `currentState` to undo.
    bool redo(const(ubyte)[] currentState, out ubyte[] outState) pure nothrow @safe {
        if (redoCount_ == 0) {
            return false;
        }
        if (maxHistory > 0) {
            if (undoRing_.length < maxHistory) {
                undoRing_.length = maxHistory;
                undoLens_.length = maxHistory;
            }
            size_t idx;
            if (undoCount_ >= maxHistory) {
                idx = undoHead_;
                undoHead_ = (undoHead_ + 1) % maxHistory;
            } else {
                idx = (undoHead_ + undoCount_) % maxHistory;
                undoCount_++;
            }
            copyIntoSlot(undoRing_[idx], currentState);
            undoLens_[idx] = currentState.length;
        }

        redoCount_--;
        outState = redoStack_[redoCount_][0 .. redoLens_[redoCount_]];
        redoStack_[redoCount_] = null;
        redoLens_[redoCount_] = 0;
        return true;
    }

    /// Returns the number of available undo steps.
    size_t undoCount() const pure nothrow @nogc @safe {
        return undoCount_;
    }

    /// Returns the number of available redo steps.
    size_t redoCount() const pure nothrow @nogc @safe {
        return redoCount_;
    }
}
