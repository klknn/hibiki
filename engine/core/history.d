module engine.core.history;

/// @safe D undo/redo history manager with bounded capacity (default 50 states).
struct HistoryManagerD {
    private ubyte[][] undoStack_;
    private ubyte[][] redoStack_;
    size_t maxHistory = 50;

    void pushState(const(ubyte)[] state) pure nothrow @safe {
        undoStack_ ~= state.dup;
        if (undoStack_.length > maxHistory) {
            undoStack_ = undoStack_[1 .. $];
        }
        redoStack_.length = 0;
    }

    bool undo(const(ubyte)[] currentState, out ubyte[] outState) pure nothrow @safe {
        if (undoStack_.length == 0) {
            return false;
        }
        redoStack_ ~= currentState.dup;
        outState = undoStack_[$ - 1];
        undoStack_ = undoStack_[0 .. $ - 1];
        return true;
    }

    bool redo(const(ubyte)[] currentState, out ubyte[] outState) pure nothrow @safe {
        if (redoStack_.length == 0) {
            return false;
        }
        undoStack_ ~= currentState.dup;
        outState = redoStack_[$ - 1];
        redoStack_ = redoStack_[0 .. $ - 1];
        return true;
    }

    size_t undoCount() const pure nothrow @nogc @safe {
        return undoStack_.length;
    }

    size_t redoCount() const pure nothrow @nogc @safe {
        return redoStack_.length;
    }
}
