package hibiki.ui;

import com.google.flatbuffers.FlatBufferBuilder;
import hibiki.BackendManager;
import hibiki.ipc.Command;
import hibiki.ipc.DeleteClip;
import hibiki.ipc.LoadClip;
import hibiki.ipc.PlayClip;
import hibiki.ipc.PlayScene;
import hibiki.ipc.Request;
import hibiki.ipc.SetClipLoop;
import hibiki.ipc.StopTrack;

/**
 * Encapsulates all IPC (FlatBuffer) request builders for SessionView operations.
 * Each method constructs a FlatBuffer message and sends it to the backend.
 */
class SessionViewIpc {
    private final SessionView view;

    SessionViewIpc(SessionView view) {
        this.view = view;
    }

    void sendLoadClip(int trackIdx, int slotIdx, String path, boolean isLoop) {
        view.slotPaths[trackIdx][slotIdx] = path;
        FlatBufferBuilder builder = new FlatBufferBuilder(512);
        int pathOff = builder.createString(path);
        LoadClip.startLoadClip(builder);
        LoadClip.addTrackIndex(builder, trackIdx);
        LoadClip.addSlotIndex(builder, slotIdx);
        LoadClip.addPath(builder, pathOff);
        LoadClip.addIsLoop(builder, isLoop);
        int loadOff = LoadClip.endLoadClip(builder);
        int requestOffset = Request.createRequest(builder, Command.LoadClip, loadOff);
        builder.finish(requestOffset);
        BackendManager.getInstance().sendRequest(builder);
    }

    void sendSetClipLoop(int trackIdx, int slotIdx, boolean isLoop) {
        FlatBufferBuilder builder = new FlatBufferBuilder(128);
        SetClipLoop.startSetClipLoop(builder);
        SetClipLoop.addTrackIndex(builder, trackIdx);
        SetClipLoop.addSlotIndex(builder, slotIdx);
        SetClipLoop.addIsLoop(builder, isLoop);
        int setOff = SetClipLoop.endSetClipLoop(builder);
        int requestOffset = Request.createRequest(builder, Command.SetClipLoop, setOff);
        builder.finish(requestOffset);
        BackendManager.getInstance().sendRequest(builder);
    }

    void sendPlayClip(int trackIdx, int slotIdx) {
        FlatBufferBuilder builder = new FlatBufferBuilder(128);
        PlayClip.startPlayClip(builder);
        PlayClip.addTrackIndex(builder, trackIdx);
        PlayClip.addSlotIndex(builder, slotIdx);
        int playClipOffset = PlayClip.endPlayClip(builder);
        int requestOffset = Request.createRequest(builder, Command.PlayClip, playClipOffset);
        builder.finish(requestOffset);
        BackendManager.getInstance().sendRequest(builder);
    }

    void sendStopTrack(int trackIdx) {
        FlatBufferBuilder builder = new FlatBufferBuilder(128);
        StopTrack.startStopTrack(builder);
        StopTrack.addTrackIndex(builder, trackIdx);
        int stopTrackOffset = StopTrack.endStopTrack(builder);
        int requestOffset = Request.createRequest(builder, Command.StopTrack, stopTrackOffset);
        builder.finish(requestOffset);
        BackendManager.getInstance().sendRequest(builder);
    }

    void sendPlayScene(int slotIdx) {
        FlatBufferBuilder builder = new FlatBufferBuilder(128);
        PlayScene.startPlayScene(builder);
        PlayScene.addSlotIndex(builder, slotIdx);
        int playSceneOff = PlayScene.endPlayScene(builder);
        int requestOffset = Request.createRequest(builder, Command.PlayScene, playSceneOff);
        builder.finish(requestOffset);
        BackendManager.getInstance().sendRequest(builder);
    }

    void sendDeleteClip(int trackIdx, int slotIdx) {
        FlatBufferBuilder builder = new FlatBufferBuilder(128);
        DeleteClip.startDeleteClip(builder);
        DeleteClip.addTrackIndex(builder, trackIdx);
        DeleteClip.addSlotIndex(builder, slotIdx);
        int deleteOff = DeleteClip.endDeleteClip(builder);
        int requestOffset = Request.createRequest(builder, Command.DeleteClip, deleteOff);
        builder.finish(requestOffset);
        BackendManager.getInstance().sendRequest(builder);

        // Optimistically clear the UI
        view.slotPaths[trackIdx][slotIdx] = null;
        view.updateSlotLabel(trackIdx, slotIdx, "");
    }
}
