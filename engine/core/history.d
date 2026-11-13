module engine.core.history;

/// @safe D undo/redo history manager with bounded capacity (default 50 states).
struct HistoryManagerD {
    private ubyte[][] undoStack_;
    private ubyte[][] redoStack_;

    /// Maximum number of undo snapshots retained.
    size_t maxHistory = 50;

    /// Pushes a snapshot onto the undo stack and clears the redo stack.
    void pushState(const(ubyte)[] state) pure nothrow @safe {
        if (maxHistory == 0) return;
        if (undoStack_.length >= maxHistory) {
            const size_t keep = maxHistory - 1;
            const size_t shift = undoStack_.length - keep;
            foreach (i; 0 .. keep) {
                undoStack_[i] = undoStack_[i + shift];
            }
            undoStack_.length = maxHistory;
            undoStack_[keep] = state.dup;
        } else {
            undoStack_ ~= state.dup;
        }
        redoStack_.length = 0;
    }

    /// Pops the latest undo snapshot into `outState` and records `currentState` for redo.
    bool undo(const(ubyte)[] currentState, out ubyte[] outState) pure nothrow @safe {
        if (undoStack_.length == 0) {
            return false;
        }
        redoStack_ ~= currentState.dup;
        outState = undoStack_[$ - 1];
        undoStack_ = undoStack_[0 .. $ - 1];
        return true;
    }

    /// Pops the latest redo snapshot into `outState` and pushes `currentState` to undo.
    bool redo(const(ubyte)[] currentState, out ubyte[] outState) pure nothrow @safe {
        if (redoStack_.length == 0) {
            return false;
        }
        undoStack_ ~= currentState.dup;
        outState = redoStack_[$ - 1];
        redoStack_ = redoStack_[0 .. $ - 1];
        return true;
    }

    /// Returns the number of available undo steps.
    size_t undoCount() const pure nothrow @nogc @safe {
        return undoStack_.length;
    }

    /// Returns the number of available redo steps.
    size_t redoCount() const pure nothrow @nogc @safe {
        return redoStack_.length;
    }
}
