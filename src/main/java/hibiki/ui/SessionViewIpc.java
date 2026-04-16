package hibiki.ui;

import hibiki.BackendManager;
import hibiki.pb.commands.*;
import hibiki.pb.commands.Request;
import hibiki.pb.core.*;
import hibiki.pb.core.Clip;
import hibiki.pb.core.EntityRef;
import hibiki.pb.notifications.*;

/**
 * Encapsulates all IPC (Protobuf) request builders for SessionView operations. Each method
 * constructs a Protobuf message and sends it to the backend.
 */
class SessionViewIpc {
  private final SessionView view;

  SessionViewIpc(SessionView view) {
    this.view = view;
  }

  void sendLoadClip(int trackIdx, int slotIdx, String path, boolean isLoop) {
    if (trackIdx < view.slotPaths.size()) view.slotPaths.get(trackIdx)[slotIdx] = path;
    BackendManager.getInstance()
        .sendRequest(
            Request.newBuilder()
                .setTrack(
                    TrackCmd.newBuilder()
                        .setAction(TrackCmd.Action.ACTION_LOAD_CLIP)
                        .setTarget(
                            EntityRef.newBuilder().setTrackIndex(trackIdx).setSessionSlot(slotIdx))
                        .setClipData(Clip.newBuilder().setPath(path).setIsLoop(isLoop)))
                .build());
  }

  void sendSetClipLoop(int trackIdx, int slotIdx, boolean isLoop) {
    BackendManager.getInstance()
        .sendRequest(
            Request.newBuilder()
                .setTrack(
                    TrackCmd.newBuilder()
                        .setAction(TrackCmd.Action.ACTION_SET_CLIP_LOOP)
                        .setTarget(
                            EntityRef.newBuilder().setTrackIndex(trackIdx).setSessionSlot(slotIdx))
                        .setFlag(isLoop))
                .build());
  }

  void sendPlayClip(int trackIdx, int slotIdx) {
    BackendManager.getInstance()
        .sendRequest(
            Request.newBuilder()
                .setTrack(
                    TrackCmd.newBuilder()
                        .setAction(TrackCmd.Action.ACTION_PLAY_SLOT)
                        .setTarget(
                            EntityRef.newBuilder().setTrackIndex(trackIdx).setSessionSlot(slotIdx)))
                .build());
  }

  void sendStopTrack(int trackIdx) {
    BackendManager.getInstance()
        .sendRequest(
            Request.newBuilder()
                .setTrack(
                    TrackCmd.newBuilder()
                        .setAction(TrackCmd.Action.ACTION_STOP)
                        .setTarget(EntityRef.newBuilder().setTrackIndex(trackIdx)))
                .build());
  }

  void sendPlayScene(int slotIdx) {
    BackendManager.getInstance()
        .sendRequest(
            Request.newBuilder()
                .setTrack(
                    TrackCmd.newBuilder()
                        .setAction(TrackCmd.Action.ACTION_PLAY_SLOT)
                        .setTarget(
                            EntityRef.newBuilder().setTrackIndex(-1).setSessionSlot(slotIdx)))
                .build());
  }

  void sendDeleteClip(int trackIdx, int slotIdx) {
    BackendManager.getInstance()
        .sendRequest(
            Request.newBuilder()
                .setTrack(
                    TrackCmd.newBuilder()
                        .setAction(TrackCmd.Action.ACTION_DELETE_CLIP)
                        .setTarget(
                            EntityRef.newBuilder().setTrackIndex(trackIdx).setSessionSlot(slotIdx)))
                .build());

    // Optimistically clear the UI
    if (trackIdx < view.slotPaths.size()) view.slotPaths.get(trackIdx)[slotIdx] = null;
    view.updateSlotLabel(trackIdx, slotIdx, "");
  }
}
