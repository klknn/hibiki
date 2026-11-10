module engine.commands.commands_d_test;

import dproto.proto : decodeProto, encodeProto;
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
}
