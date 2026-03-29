package hibiki.ui;

import hibiki.BackendManager;
import hibiki.pb.commands.*;
import hibiki.pb.notifications.*;
import hibiki.pb.core.*;
import hibiki.pb.commands.Request;

/**
 * Encapsulates all IPC (Protobuf) request builders for SessionView operations.
 * Each method constructs a Protobuf message and sends it to the backend.
 */
class SessionViewIpc {
    private final SessionView view;

    SessionViewIpc(SessionView view) {
        this.view = view;
    }

    void sendLoadClip(int trackIdx, int slotIdx, String path, boolean isLoop) {
        view.slotPaths[trackIdx][slotIdx] = path;
        BackendManager.getInstance().sendRequest(Request.newBuilder()
                .setLoadClip(LoadClip.newBuilder()
                        .setTrackIndex(trackIdx)
                        .setSlotIndex(slotIdx)
                        .setPath(path)
                        .setIsLoop(isLoop))
                .build());
    }

    void sendSetClipLoop(int trackIdx, int slotIdx, boolean isLoop) {
        BackendManager.getInstance().sendRequest(Request.newBuilder()
                .setSetClipLoop(SetClipLoop.newBuilder()
                        .setTrackIndex(trackIdx)
                        .setSlotIndex(slotIdx)
                        .setIsLoop(isLoop))
                .build());
    }

    void sendPlayClip(int trackIdx, int slotIdx) {
        BackendManager.getInstance().sendRequest(Request.newBuilder()
                .setPlayClip(PlayClip.newBuilder()
                        .setTrackIndex(trackIdx)
                        .setSlotIndex(slotIdx))
                .build());
    }

    void sendStopTrack(int trackIdx) {
        BackendManager.getInstance().sendRequest(Request.newBuilder()
                .setStopTrack(StopTrack.newBuilder()
                        .setTrackIndex(trackIdx))
                .build());
    }

    void sendPlayScene(int slotIdx) {
        BackendManager.getInstance().sendRequest(Request.newBuilder()
                .setPlayScene(PlayScene.newBuilder()
                        .setSlotIndex(slotIdx))
                .build());
    }

    void sendDeleteClip(int trackIdx, int slotIdx) {
        BackendManager.getInstance().sendRequest(Request.newBuilder()
                .setDeleteClip(DeleteClip.newBuilder()
                        .setTrackIndex(trackIdx)
                        .setSlotIndex(slotIdx))
                .build());

        // Optimistically clear the UI
        view.slotPaths[trackIdx][slotIdx] = null;
        view.updateSlotLabel(trackIdx, slotIdx, "");
    }
}
