module engine.main;

import core.stdc.stdio : feof, fread, stdin, stdout;
import core.thread : Thread, dur, msecs, thread_detachThis;
import dproto.proto : decodeProto;
import pb.commands : Request;
import std.stdio : stderr, writefln, writeln;
import std.string : startsWith;

version (Windows) {
    import core.sys.windows.fcntl : _O_BINARY;
    import core.sys.windows.io : _setmode;
    import core.stdc.stdio : _fileno;
}

extern (C++, hibiki) {
    struct ProjectState;
    struct HistoryManager;

    ProjectState* createProjectState();
    void destroyProjectState(ProjectState* state);

    HistoryManager* createHistoryManager();
    void destroyHistoryManager(HistoryManager* history);

    bool isStateQuit(const(ProjectState)* state);
    void setStateQuit(ProjectState* state, bool quit);

    void listVst3Plugins(const(char)* path_ptr, size_t path_len);
    void runVst3MainLoop();

    void loadConfig(ref ProjectState state);
    void playback_thread(ref ProjectState state);
    void sendNotificationTick(ref ProjectState state);
    void sendInitialConfig(ref const(ProjectState) state);

    bool dispatchIpcCommand(
        const(ubyte)* data,
        size_t size,
        ref ProjectState state,
        ref HistoryManager history
    );
}

void runIpcLoop(hibiki.ProjectState* state) {
    auto history = hibiki.createHistoryManager();
    scope (exit) hibiki.destroyHistoryManager(history);

    hibiki.sendInitialConfig(*state);

    while (!hibiki.isStateQuit(state)) {
        uint msg_size = 0;
        size_t nread = fread(&msg_size, 4, 1, stdin);
        if (nread != 1) {
            if (feof(stdin)) break;
            break;
        }

        if (msg_size > 1024 * 1024) { // 1MB safety limit
            break;
        }

        auto buffer = new ubyte[msg_size];
        size_t payload_read = fread(buffer.ptr, 1, msg_size, stdin);
        if (payload_read != msg_size) {
            break;
        }

        Request req;
        bool ok = decodeProto(buffer, req);
        if (!ok) {
            stderr.writeln("Failed to decode protobuf request in D");
        }

        hibiki.dispatchIpcCommand(buffer.ptr, msg_size, *state, *history);
    }

    hibiki.setStateQuit(state, true);
}

int main(string[] args) {
    // Check for --list flag
    foreach (i, arg; args) {
        if (arg == "--list" && i + 1 < args.length) {
            string listPath = args[i + 1];
            hibiki.listVst3Plugins(listPath.ptr, listPath.length);
            return 0;
        } else if (arg.startsWith("--list=")) {
            string listPath = arg[7 .. $];
            hibiki.listVst3Plugins(listPath.ptr, listPath.length);
            return 0;
        }
    }

    version (Windows) {
        _setmode(_fileno(stdin), _O_BINARY);
        _setmode(_fileno(stdout), _O_BINARY);
    }

    auto state = hibiki.createProjectState();
    scope (exit) hibiki.destroyProjectState(state);

    hibiki.loadConfig(*state);

    // Audio thread: runs at high priority, detached from GC
    auto audioThread = new Thread({
        thread_detachThis();
        hibiki.playback_thread(*state);
    });
    audioThread.start();

    // Notification thread: sends playhead, levels, and plugin metering at ~30Hz
    auto notifThread = new Thread({
        while (!hibiki.isStateQuit(state)) {
            Thread.sleep(33.msecs);
            hibiki.sendNotificationTick(*state);
        }
    });
    notifThread.start();

    version (OSX) {
        auto ipcThread = new Thread({
            runIpcLoop(state);
        });
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
