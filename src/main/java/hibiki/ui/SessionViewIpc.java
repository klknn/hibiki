package hibiki.ui;

import hibiki.BackendManager;
import hibiki.pb.HibikiProto;
import hibiki.pb.HibikiProto.Request;

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
                .setLoadClip(HibikiProto.LoadClip.newBuilder()
                        .setTrackIndex(trackIdx)
                        .setSlotIndex(slotIdx)
                        .setPath(path)
                        .setIsLoop(isLoop))
                .build());
    }

    void sendSetClipLoop(int trackIdx, int slotIdx, boolean isLoop) {
        BackendManager.getInstance().sendRequest(Request.newBuilder()
                .setSetClipLoop(HibikiProto.SetClipLoop.newBuilder()
                        .setTrackIndex(trackIdx)
                        .setSlotIndex(slotIdx)
                        .setIsLoop(isLoop))
                .build());
    }

    void sendPlayClip(int trackIdx, int slotIdx) {
        BackendManager.getInstance().sendRequest(Request.newBuilder()
                .setPlayClip(HibikiProto.PlayClip.newBuilder()
                        .setTrackIndex(trackIdx)
                        .setSlotIndex(slotIdx))
                .build());
    }

    void sendStopTrack(int trackIdx) {
        BackendManager.getInstance().sendRequest(Request.newBuilder()
                .setStopTrack(HibikiProto.StopTrack.newBuilder()
                        .setTrackIndex(trackIdx))
                .build());
    }

    void sendPlayScene(int slotIdx) {
        BackendManager.getInstance().sendRequest(Request.newBuilder()
                .setPlayScene(HibikiProto.PlayScene.newBuilder()
                        .setSlotIndex(slotIdx))
                .build());
    }

    void sendDeleteClip(int trackIdx, int slotIdx) {
        BackendManager.getInstance().sendRequest(Request.newBuilder()
                .setDeleteClip(HibikiProto.DeleteClip.newBuilder()
                        .setTrackIndex(trackIdx)
                        .setSlotIndex(slotIdx))
                .build());

        // Optimistically clear the UI
        view.slotPaths[trackIdx][slotIdx] = null;
        view.updateSlotLabel(trackIdx, slotIdx, "");
    }
}
