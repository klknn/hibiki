module engine.commands.commands_d_test;

import dproto.proto : decodeProto, encodeProto;
import engine.commands.dispatcher : dispatchRequestD, handleAutomationCmdD,
                                    handleProjectCmdD, handleTrackCmdD,
                                    handleTransportCmdD;
import engine.core.history : HistoryManagerD;
import engine.core.project : ClipD, ProjectStateD, TimelineClipD, TrackD;
import pb.commands : AutomationCmd, MidiCmd, PluginCmd, ProjectCmd, Request, TrackCmd, TransportCmd;
import pb.core : Clip, Project, Track, TransportState;
import pb.notifications : Acknowledge, Notification, PlayheadInfo;

@safe unittest {
    // 1. Test decoding a TransportCmd Request (ACTION_PLAY)
    Request req_play;
    TransportCmd t_play;
    t_play.action = TransportCmd.Action.ACTION_PLAY;
    req_play.transport.set(t_play);

    ubyte[] play_bytes = encodeProto(req_play);
    assert(play_bytes.length > 0);

    Request decoded_play;
    bool ok = decodeProto(play_bytes, decoded_play);
    assert(ok);
    assert(decoded_play.transport.hasValue);
    assert(decoded_play.transport.value.action == TransportCmd.Action.ACTION_PLAY);

    // 2. Test decoding a TransportCmd Seek Request
    Request req_seek;
    TransportCmd t_seek;
    t_seek.action = TransportCmd.Action.ACTION_SEEK;
    t_seek.seek_pos = 12.5f;
    req_seek.transport.set(t_seek);

    ubyte[] seek_bytes = encodeProto(req_seek);
    Request decoded_seek;
    assert(decodeProto(seek_bytes, decoded_seek));
    assert(decoded_seek.transport.hasValue);
    assert(decoded_seek.transport.value.action == TransportCmd.Action.ACTION_SEEK);
    assert(decoded_seek.transport.value.seek_pos == 12.5f);

    // 3. Test decoding a ProjectCmd Set BPM Request
    Request req_bpm;
    ProjectCmd p_bpm;
    p_bpm.action = ProjectCmd.Action.ACTION_SET_BPM;
    p_bpm.bpm = 140.0f;
    req_bpm.project.set(p_bpm);

    ubyte[] bpm_bytes = encodeProto(req_bpm);
    Request decoded_bpm;
    assert(decodeProto(bpm_bytes, decoded_bpm));
    assert(decoded_bpm.project.hasValue);
    assert(decoded_bpm.project.value.action == ProjectCmd.Action.ACTION_SET_BPM);
    assert(decoded_bpm.project.value.bpm == 140.0f);

    // 4. Test Notification Acknowledge encoding
    Notification notif;
    Acknowledge ack;
    ack.command_type = "PLAY";
    ack.success = true;
    notif.acknowledge.set(ack);

    ubyte[] notif_bytes = encodeProto(notif);
    assert(notif_bytes.length > 0);

    Notification decoded_notif;
    assert(decodeProto(notif_bytes, decoded_notif));
    assert(decoded_notif.acknowledge.hasValue);
    assert(decoded_notif.acknowledge.value.command_type == "PLAY");
    assert(decoded_notif.acknowledge.value.success == true);

    // 5. Test TrackCmd (Set Volume)
    Request req_trk;
    TrackCmd t_cmd;
    t_cmd.action = TrackCmd.Action.ACTION_SET_VOLUME;
    t_cmd.target.track_index = 2;
    t_cmd.value = 0.85f;
    req_trk.track.set(t_cmd);

    ubyte[] trk_bytes = encodeProto(req_trk);
    Request decoded_trk;
    assert(decodeProto(trk_bytes, decoded_trk));
    assert(decoded_trk.track.hasValue);
    assert(decoded_trk.track.value.action == TrackCmd.Action.ACTION_SET_VOLUME);
    assert(decoded_trk.track.value.target.track_index == 2);
    assert(decoded_trk.track.value.value == 0.85f);

    // 6. Test PluginCmd (Set Param)
    Request req_plg;
    PluginCmd p_cmd;
    p_cmd.action = PluginCmd.Action.ACTION_SET_PARAM;
    p_cmd.param_id = 7;
    p_cmd.param_value = 0.5f;
    req_plg.plugin.set(p_cmd);

    ubyte[] plg_bytes = encodeProto(req_plg);
    Request decoded_plg;
    assert(decodeProto(plg_bytes, decoded_plg));
    assert(decoded_plg.plugin.hasValue);
    assert(decoded_plg.plugin.value.action == PluginCmd.Action.ACTION_SET_PARAM);
    assert(decoded_plg.plugin.value.param_id == 7);
    assert(decoded_plg.plugin.value.param_value == 0.5f);

    // 7. Test PlayheadInfo notification
    Notification notif_playhead;
    PlayheadInfo phi;
    phi.position_sec = 10.5f;
    phi.bpm = 128.0f;
    phi.transport_state = TransportState.TRANSPORT_STATE_PLAYING;
    notif_playhead.playhead_info.set(phi);

    ubyte[] phi_bytes = encodeProto(notif_playhead);
    Notification decoded_phi;
    assert(decodeProto(phi_bytes, decoded_phi));
    assert(decoded_phi.playhead_info.hasValue);
    assert(decoded_phi.playhead_info.value.position_sec == 10.5f);
    assert(decoded_phi.playhead_info.value.bpm == 128.0f);
    assert(decoded_phi.playhead_info.value.transport_state == TransportState.TRANSPORT_STATE_PLAYING);

    // 8. Test dispatchRequestD: Transport commands
    auto state = new ProjectStateD();
    HistoryManagerD history;
    string lastAckType;
    bool lastAckSuccess;
    auto ackCb = (string type, bool succ) @safe {
        lastAckType = type;
        lastAckSuccess = succ;
    };

    Request req1;
    TransportCmd t1;
    t1.action = TransportCmd.Action.ACTION_PLAY;
    req1.transport.set(t1);
    assert(dispatchRequestD(req1, state, history, ackCb));
    assert(state.isTimelinePlaying == true);
    assert(lastAckType == "PLAY" && lastAckSuccess == true);

    Request req2;
    TransportCmd t2;
    t2.action = TransportCmd.Action.ACTION_SEEK;
    t2.seek_pos = 7.25f;
    req2.transport.set(t2);
    assert(dispatchRequestD(req2, state, history, ackCb));
    assert(state.playheadPosSec == 7.25);
    assert(lastAckType == "SEEK" && lastAckSuccess == true);

    Request req3;
    TransportCmd t3;
    t3.action = TransportCmd.Action.ACTION_SET_LOOP;
    t3.loop_enabled = true;
    t3.loop_start = 2.0f;
    t3.loop_end = 6.0f;
    req3.transport.set(t3);
    assert(dispatchRequestD(req3, state, history, ackCb));
    assert(state.loopEnabled == true);
    assert(state.loopStartSec == 2.0);
    assert(state.loopEndSec == 6.0);
    assert(lastAckType == "SET_LOOP" && lastAckSuccess == true);

    Request req4;
    TransportCmd t4;
    t4.action = TransportCmd.Action.ACTION_STOP;
    req4.transport.set(t4);
    assert(dispatchRequestD(req4, state, history, ackCb));
    assert(state.isTimelinePlaying == false);
    assert(lastAckType == "STOP" && lastAckSuccess == true);

    // 9. Test dispatchRequestD: Project commands (SET_BPM, QUIT, UNDO, REDO)
    state.bpm = 120.0;
    history.pushState(state.captureState());

    Request req5;
    ProjectCmd p1;
    p1.action = ProjectCmd.Action.ACTION_SET_BPM;
    p1.bpm = 140.0f;
    req5.project.set(p1);
    assert(dispatchRequestD(req5, state, history, ackCb));
    assert(state.bpm == 140.0);
    assert(lastAckType == "SET_BPM" && lastAckSuccess == true);

    Request req6;
    ProjectCmd p2;
    p2.action = ProjectCmd.Action.ACTION_UNDO;
    req6.project.set(p2);
    assert(dispatchRequestD(req6, state, history, ackCb));
    assert(state.bpm == 120.0);
    assert(lastAckType == "UNDO" && lastAckSuccess == true);

    Request req7;
    ProjectCmd p3;
    p3.action = ProjectCmd.Action.ACTION_REDO;
    req7.project.set(p3);
    assert(dispatchRequestD(req7, state, history, ackCb));
    assert(state.bpm == 140.0);
    assert(lastAckType == "REDO" && lastAckSuccess == true);

    Request req8;
    ProjectCmd p4;
    p4.action = ProjectCmd.Action.ACTION_QUIT;
    req8.project.set(p4);
    assert(dispatchRequestD(req8, state, history, ackCb));
    assert(state.quit == true);

    // 10. Test dispatchRequestD: Track commands (volume, pan, mute, solo)
    Request req9;
    TrackCmd trkCmd1;
    trkCmd1.action = TrackCmd.Action.ACTION_SET_VOLUME;
    trkCmd1.target.track_index = 1;
    trkCmd1.value = 0.75f;
    req9.track.set(trkCmd1);
    assert(dispatchRequestD(req9, state, history, ackCb));
    assert(1 in state.tracks);
    assert(state.tracks[1].volume == 0.75f);
    assert(lastAckType == "SET_VOLUME" && lastAckSuccess == true);

    Request req10;
    TrackCmd trkCmd2;
    trkCmd2.action = TrackCmd.Action.ACTION_SET_PAN;
    trkCmd2.target.track_index = 1;
    trkCmd2.value = -0.3f;
    req10.track.set(trkCmd2);
    assert(dispatchRequestD(req10, state, history, ackCb));
    assert(state.tracks[1].pan == -0.3f);
    assert(lastAckType == "SET_PAN" && lastAckSuccess == true);

    Request req11;
    TrackCmd trkCmd3;
    trkCmd3.action = TrackCmd.Action.ACTION_SET_MUTE;
    trkCmd3.target.track_index = 1;
    trkCmd3.flag = true;
    req11.track.set(trkCmd3);
    assert(dispatchRequestD(req11, state, history, ackCb));
    assert(state.tracks[1].muted == true);
    assert(lastAckType == "SET_MUTE" && lastAckSuccess == true);

    // 11. Test dispatchRequestD: Automation commands (ADD_LANE, ADD_CLIP, REMOVE_LANE)
    Request req12;
    AutomationCmd autoCmd1;
    autoCmd1.action = AutomationCmd.Action.ACTION_ADD_LANE;
    autoCmd1.target.track_index = 1;
    autoCmd1.target.plugin_index.set(0);
    autoCmd1.param_id = 42;
    req12.automation.set(autoCmd1);
    assert(dispatchRequestD(req12, state, history, ackCb));
    assert(state.tracks[1].automationLanes.length == 1);
    assert(state.tracks[1].automationLanes[0].paramId == 42);
    assert(lastAckType == "ADD_LANE" && lastAckSuccess == true);

    Request req13;
    AutomationCmd autoCmd2;
    autoCmd2.action = AutomationCmd.Action.ACTION_ADD_CLIP;
    autoCmd2.target.track_index = 1;
    autoCmd2.target.lane_index.set(0);
    autoCmd2.start_time_sec = 2.0f;
    autoCmd2.duration_beats = 4.0f;
    req13.automation.set(autoCmd2);
    assert(dispatchRequestD(req13, state, history, ackCb));
    assert(state.tracks[1].automationLanes[0].clips.length == 1);
    assert(state.tracks[1].automationLanes[0].clips[0].startTimeSec == 2.0);
    assert(state.tracks[1].automationLanes[0].clips[0].durationBeats == 4.0);
    assert(lastAckType == "ADD_CLIP" && lastAckSuccess == true);

    Request req14;
    AutomationCmd autoCmd3;
    autoCmd3.action = AutomationCmd.Action.ACTION_REMOVE_LANE;
    autoCmd3.target.track_index = 1;
    autoCmd3.target.lane_index.set(0);
    req14.automation.set(autoCmd3);
    assert(dispatchRequestD(req14, state, history, ackCb));
    assert(state.tracks[1].automationLanes.length == 0);
    assert(lastAckType == "REMOVE_LANE" && lastAckSuccess == true);
}
