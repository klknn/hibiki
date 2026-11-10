module engine.bridge;

extern (C++, hibiki) {
    struct ProjectState;
    struct HistoryManager;

    ProjectState* createProjectState();
    void destroyProjectState(ProjectState* state);

    HistoryManager* createHistoryManager();
    void destroyHistoryManager(HistoryManager* history);

    bool isStateQuit(const(ProjectState)* state);
    void setStateQuit(ProjectState* state, bool quit);

    void setTimelinePlaying(ProjectState* state, bool playing);
    bool isTimelinePlaying(const(ProjectState)* state);
    void setPlayheadPos(ProjectState* state, double pos);
    double getPlayheadPos(const(ProjectState)* state);
    void setBpm(ProjectState* state, double bpm);
    double getBpm(const(ProjectState)* state);
    void setLoop(ProjectState* state, bool enabled, double start_sec, double end_sec);
    void stopPlaybackTracks(ProjectState* state);

    void listVst3Plugins(const(char)* path_ptr, size_t path_len);
    void runVst3MainLoop();

    void loadConfig(ref ProjectState state);
    void playback_thread(ref ProjectState state);
    void sendNotificationTick(ref ProjectState state);
    void sendInitialConfig(ref const(ProjectState) state);

    void sendAck(const(char)* cmd_type, bool success);
    void sendNotification(const(ubyte)* buf, size_t size);

    bool dispatchIpcCommand(
        const(ubyte)* data,
        size_t size,
        ref ProjectState state,
        ref HistoryManager history
    );
}
