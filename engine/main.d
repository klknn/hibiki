module engine.main;

import core.stdc.stdio : fread, stdin;
import core.thread : Thread, msecs, thread_detachThis;
import engine.bridge : hibiki;
import engine.commands.dispatcher : dispatchCommandWithFallback;
import std.stdio : stderr, writeln;
import std.string : startsWith;

version (Windows) {
    import core.stdc.stdio : _fileno;
    import core.sys.windows.fcntl : _O_BINARY;
    import core.sys.windows.io : _setmode;
}

/// Reads length-prefixed Protobuf requests from stdin until EOF or quit.
void runIpcLoop(hibiki.ProjectState* state) {
    auto history = hibiki.createHistoryManager();
    scope (exit) hibiki.destroyHistoryManager(history);

    hibiki.sendInitialConfig(*state);

    enum uint maxMessageBytes = 1024 * 1024;
    while (!hibiki.isStateQuit(state)) {
        uint msgSize = 0;
        if (fread(&msgSize, 4, 1, stdin) != 1 || msgSize > maxMessageBytes) {
            break;
        }

        auto buffer = new ubyte[msgSize];
        if (fread(buffer.ptr, 1, msgSize, stdin) != msgSize) {
            break;
        }

        if (!dispatchCommandWithFallback(buffer, state, history)) {
            stderr.writeln("Failed to dispatch IPC request");
        }
    }

    hibiki.setStateQuit(state, true);
}

private bool tryHandleListFlag(string[] args) {
    foreach (i, arg; args) {
        string listPath;
        if (arg == "--list" && i + 1 < args.length) {
            listPath = args[i + 1];
        } else if (arg.startsWith("--list=")) {
            listPath = arg[7 .. $];
        } else {
            continue;
        }
        hibiki.listVst3Plugins(listPath.ptr, listPath.length);
        return true;
    }
    return false;
}

/// Entry point for the Hibiki audio engine process (`hbk-play`).
int main(string[] args) {
    if (tryHandleListFlag(args)) {
        return 0;
    }

    version (Windows) {
        _setmode(_fileno(stdin), _O_BINARY);
        _setmode(_fileno(stdout), _O_BINARY);
    }

    auto state = hibiki.createProjectState();
    scope (exit) hibiki.destroyProjectState(state);

    hibiki.loadConfig(*state);

    // Detach audio thread from the D GC so real-time callbacks never pause.
    auto audioThread = new Thread({
        thread_detachThis();
        hibiki.playback_thread(*state);
    });
    audioThread.start();

    auto notifThread = new Thread({
        while (!hibiki.isStateQuit(state)) {
            Thread.sleep(33.msecs);
            hibiki.sendNotificationTick(*state);
        }
    });
    notifThread.start();

    version (OSX) {
        auto ipcThread = new Thread({ runIpcLoop(state); });
        ipcThread.start();
        hibiki.runVst3MainLoop();
        ipcThread.join();
    } else {
        runIpcLoop(state);
    }

    hibiki.setStateQuit(state, true);
    notifThread.join();
    audioThread.join();

    return 0;
}
