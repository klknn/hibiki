package hibiki.ui;

import hibiki.ipc.Notification;
import hibiki.ipc.ParamList;
import hibiki.ipc.Response;
import hibiki.ipc.TimelineClipInfo;

/**
 * Handles all IPC notification dispatch for TimelineView.
 * Processes PlayheadInfo, TimelineClipInfo, ParamList, ClearProject, and TrackInfo.
 */
class TimelineNotificationHandler {
    private final TimelineView view;

    TimelineNotificationHandler(TimelineView view) {
        this.view = view;
    }

    void handleNotification(Notification n) {
        if (n.responseType() == hibiki.ipc.Response.PlayheadInfo) {
            handlePlayhead(n);
        } else if (n.responseType() == hibiki.ipc.Response.TimelineClipInfo) {
            handleTimelineClip(n);
        } else if (n.responseType() == Response.ParamList) {
            handleParamList(n);
        } else if (n.responseType() == hibiki.ipc.Response.ClearProject) {
            handleClearProject();
        } else if (n.responseType() == hibiki.ipc.Response.TrackInfo) {
            handleTrackInfo(n);
        }
    }

    private void handlePlayhead(Notification n) {
        hibiki.ipc.PlayheadInfo info = (hibiki.ipc.PlayheadInfo) n.response(new hibiki.ipc.PlayheadInfo());
        view.playheadPos = info.positionSec();
        view.bpm = info.bpm();
        boolean wasPlaying = view.isPlaying;
        view.isPlaying = info.isPlaying();

        if (view.isPlaying && !wasPlaying && view.autoScroll) {
            int playheadX = (int) (view.playheadPos * view.getPixelsPerSecond());
            int scrollX = view.scrollPane.getHorizontalScrollBar().getValue();
            view.playheadScreenOffset = playheadX - scrollX;
        }
    }

    private void handleTimelineClip(Notification n) {
        TimelineClipInfo info = (TimelineClipInfo) n.response(new TimelineClipInfo());
        int tidx = info.trackIndex();

        while (view.tracks.size() <= tidx) {
            view.tracks.add(new TimelineView.TrackTimeline(view.tracks.size()));
        }
        view.tracks.get(tidx).addOrUpdateClip(info);
        view.updateContentSize();
    }

    private void handleParamList(Notification n) {
        ParamList paramList = (ParamList) n.response(new ParamList());
        int tidx = paramList.trackIndex();
        while (view.tracks.size() <= tidx) {
            view.tracks.add(new TimelineView.TrackTimeline(view.tracks.size()));
        }
        if (paramList.pluginName() != null && !paramList.pluginName().isEmpty()) {
            view.tracks.get(tidx).pluginName = paramList.pluginName();
            view.tracks.get(tidx).isInstrument = paramList.isInstrument();
        }
    }

    private void handleClearProject() {
        for (TimelineView.TrackTimeline t : view.tracks) {
            t.clips.clear();
            t.clipMap.clear();
            t.pluginName = null;
            t.isInstrument = false;
            t.customName = null;
        }
    }

    private void handleTrackInfo(Notification n) {
        hibiki.ipc.TrackInfo info = (hibiki.ipc.TrackInfo) n.response(new hibiki.ipc.TrackInfo());
        int tidx = info.trackIndex();
        while (view.tracks.size() <= tidx) {
            view.tracks.add(new TimelineView.TrackTimeline(view.tracks.size()));
        }
        String name = info.name();
        view.tracks.get(tidx).customName = (name == null || name.isEmpty()) ? null : name;
        view.repaint();
        // Sync with SessionView
        if (SessionView.getInstance() != null && SessionView.getInstance().trackHeaders.length > tidx) {
            javax.swing.JLabel header = SessionView.getInstance().trackHeaders[tidx];
            if (header != null) {
                String displayName = view.tracks.get(tidx).getDisplayName();
                header.setText(tidx + " " + displayName);
            }
        }
    }
}
