module engine.plugin.plugin_proxy_d;

import core.stdc.stdlib : free, malloc;
import dproto.proto : Arena, computeProtoSize, decodeProto, encodeProto;
import pb.plugin_worker;

/// C-compatible MIDI event passed from C++ PluginProxy into D.
extern (C) struct HbkWorkerMidiEvent {
    int sampleOffset;
    uint channel;
    uint pitch;
    float velocity;
    bool isNoteOn;
}

private string sliceStr(const(char)* ptr, int len) pure nothrow @nogc @trusted {
    return (ptr !is null && len > 0) ? cast(string)ptr[0 .. len] : "";
}

private const(ubyte)[] sliceBytes(const(ubyte)* ptr, int len) pure nothrow @nogc @trusted {
    return (ptr !is null && len > 0) ? ptr[0 .. len] : null;
}

private int encodeRequest(const ref WorkerRequest req, ubyte* outBuf, int cap) pure nothrow @nogc @trusted {
    size_t needed = computeProtoSize(req);
    if (needed == 0) return 0;
    if (outBuf is null || cap < cast(int)needed) {
        return -cast(int)needed;
    }
    size_t written = encodeProto(req, outBuf[0 .. cap]);
    return (written > 0) ? cast(int)written : -1;
}

private bool decodeResponse(const(ubyte)* data, int len, ref WorkerResponse resp) pure nothrow @nogc @trusted {
    if (data is null || len < 0) return false;
    ubyte[512] scratch;
    Arena arena = Arena(scratch[]);
    return decodeProto(data[0 .. len], resp, arena);
}

private void assignStringOut(string s, const(char)** outPtr, int* outLen) pure nothrow @nogc @trusted {
    if (outPtr !is null) *outPtr = s.length > 0 ? s.ptr : null;
    if (outLen !is null) *outLen = cast(int)s.length;
}

private void assignBytesOut(const(ubyte)[] b, const(ubyte)** outPtr, int* outLen) pure nothrow @nogc @trusted {
    if (outPtr !is null) *outPtr = b.length > 0 ? b.ptr : null;
    if (outLen !is null) *outLen = cast(int)b.length;
}

extern (C) nothrow @nogc:

/// Encodes a WorkerRequest.shutdown command.
int hbk_worker_encode_shutdown(ubyte* outBuf, int cap) {
    WorkerRequest req;
    Shutdown s;
    req.shutdown.set(s);
    return encodeRequest(req, outBuf, cap);
}

/// Encodes a WorkerRequest.config command.
int hbk_worker_encode_config(int blockSize, int numChannels, bool useSharedMemory,
                             ubyte* outBuf, int cap) {
    WorkerRequest req;
    WorkerConfig cfg;
    cfg.block_size = blockSize;
    cfg.num_channels = numChannels;
    cfg.use_shared_memory = useSharedMemory;
    req.config.set(cfg);
    return encodeRequest(req, outBuf, cap);
}

/// Encodes a WorkerRequest.load command.
int hbk_worker_encode_load(const(char)* path, int pathLen, int pluginIndex,
                           double sampleRate, ubyte* outBuf, int cap) {
    WorkerRequest req;
    LoadPlugin cmd;
    cmd.path = sliceStr(path, pathLen);
    cmd.plugin_index = pluginIndex;
    cmd.sample_rate = sampleRate;
    req.load.set(cmd);
    return encodeRequest(req, outBuf, cap);
}

/// Encodes a WorkerRequest.process command.
int hbk_worker_encode_process(int numSamples, bool hasInputs, double sampleRate,
                              double tempo, int timeSigNum, int timeSigDenom,
                              long continuousTimeSamples, double projectTimeMusic,
                              const(HbkWorkerMidiEvent)* events, int numEvents,
                              const(ubyte)* inputAudio, int inputAudioLen,
                              ubyte* outBuf, int cap) {
    WorkerRequest req;
    ProcessAudio cmd;
    cmd.num_samples = numSamples;
    cmd.has_inputs = hasInputs;
    cmd.sample_rate = sampleRate;
    cmd.tempo = tempo;
    cmd.time_sig_numerator = timeSigNum;
    cmd.time_sig_denominator = timeSigDenom;
    cmd.continuous_time_samples = continuousTimeSamples;
    cmd.project_time_music = projectTimeMusic;
    if (inputAudio !is null && inputAudioLen > 0) {
        cmd.input_audio = cast(ubyte[])inputAudio[0 .. inputAudioLen];
    }

    MidiEvent[64] stackEvents;
    MidiEvent* heapEvents = null;
    scope (exit) {
        if (heapEvents !is null) free(heapEvents);
    }

    if (events !is null && numEvents > 0) {
        MidiEvent[] target;
        if (numEvents <= cast(int)stackEvents.length) {
            target = stackEvents[0 .. numEvents];
        } else {
            heapEvents = cast(MidiEvent*)malloc(cast(size_t)numEvents * MidiEvent.sizeof);
            if (heapEvents is null) return -1;
            target = heapEvents[0 .. numEvents];
        }
        foreach (i; 0 .. numEvents) {
            target[i].sample_offset = events[i].sampleOffset;
            target[i].channel = events[i].channel;
            target[i].pitch = events[i].pitch;
            target[i].velocity = events[i].velocity;
            target[i].is_note_on = events[i].isNoteOn;
        }
        cmd.midi_events = target;
    }

    req.process.set(cmd);
    return encodeRequest(req, outBuf, cap);
}

/// Encodes a WorkerRequest.set_param command.
int hbk_worker_encode_set_param(uint paramId, double value, ubyte* outBuf, int cap) {
    WorkerRequest req;
    SetParam cmd;
    cmd.param_id = paramId;
    cmd.value = value;
    req.set_param.set(cmd);
    return encodeRequest(req, outBuf, cap);
}

/// Encodes a WorkerRequest.get_param command.
int hbk_worker_encode_get_param(uint paramId, ubyte* outBuf, int cap) {
    WorkerRequest req;
    GetParam cmd;
    cmd.param_id = paramId;
    req.get_param.set(cmd);
    return encodeRequest(req, outBuf, cap);
}

/// Encodes a WorkerRequest.get_param_count command.
int hbk_worker_encode_get_param_count(ubyte* outBuf, int cap) {
    WorkerRequest req;
    GetParamCount cmd;
    req.get_param_count.set(cmd);
    return encodeRequest(req, outBuf, cap);
}

/// Encodes a WorkerRequest.get_param_info command.
int hbk_worker_encode_get_param_info(int index, ubyte* outBuf, int cap) {
    WorkerRequest req;
    GetParamInfo cmd;
    cmd.index = index;
    req.get_param_info.set(cmd);
    return encodeRequest(req, outBuf, cap);
}

/// Encodes a WorkerRequest.show_editor command.
int hbk_worker_encode_show_editor(bool headless, ubyte* outBuf, int cap) {
    WorkerRequest req;
    ShowEditor cmd;
    cmd.headless = headless;
    req.show_editor.set(cmd);
    return encodeRequest(req, outBuf, cap);
}

/// Encodes a WorkerRequest.stop_editor command.
int hbk_worker_encode_stop_editor(ubyte* outBuf, int cap) {
    WorkerRequest req;
    StopEditor cmd;
    req.stop_editor.set(cmd);
    return encodeRequest(req, outBuf, cap);
}

/// Encodes a WorkerRequest.get_editor_frame command.
int hbk_worker_encode_get_editor_frame(ubyte* outBuf, int cap) {
    WorkerRequest req;
    GetEditorFrame cmd;
    req.get_editor_frame.set(cmd);
    return encodeRequest(req, outBuf, cap);
}

/// Encodes a WorkerRequest.editor_input command.
int hbk_worker_encode_editor_input(int type, int x, int y, int button,
                                   int keyCode, int delta,
                                   ubyte* outBuf, int cap) {
    WorkerRequest req;
    EditorInput cmd;
    cmd.type = cast(EditorInput.Type)type;
    cmd.x = x;
    cmd.y = y;
    cmd.button = button;
    cmd.key_code = keyCode;
    cmd.delta = delta;
    req.editor_input.set(cmd);
    return encodeRequest(req, outBuf, cap);
}

/// Encodes a WorkerRequest.get_state command.
int hbk_worker_encode_get_state(ubyte* outBuf, int cap) {
    WorkerRequest req;
    GetState cmd;
    req.get_state.set(cmd);
    return encodeRequest(req, outBuf, cap);
}

/// Encodes a WorkerRequest.set_state command.
int hbk_worker_encode_set_state(const(ubyte)* state, int stateLen,
                                ubyte* outBuf, int cap) {
    WorkerRequest req;
    SetState cmd;
    cmd.state = cast(ubyte[])sliceBytes(state, stateLen);
    req.set_state.set(cmd);
    return encodeRequest(req, outBuf, cap);
}

/// Validates that a raw buffer is a well-formed WorkerResponse.
bool hbk_worker_decode_ack(const(ubyte)* data, int len) {
    WorkerResponse resp;
    return decodeResponse(data, len, resp);
}

/// Decodes a WorkerResponse.load_result payload (zero-copy string slices).
bool hbk_worker_decode_load_result(const(ubyte)* data, int len,
                                   bool* outSuccess,
                                   const(char)** outName, int* outNameLen,
                                   bool* outIsInstrument,
                                   const(char)** outError, int* outErrorLen) {
    WorkerResponse resp;
    if (!decodeResponse(data, len, resp) || !resp.load_result.hasValue) {
        return false;
    }
    const r = resp.load_result.value;
    if (outSuccess !is null) *outSuccess = r.success;
    if (outIsInstrument !is null) *outIsInstrument = r.is_instrument;
    assignStringOut(r.name, outName, outNameLen);
    assignStringOut(r.error, outError, outErrorLen);
    return true;
}

/// Decodes a WorkerResponse.process_done payload (zero-copy audio slice).
bool hbk_worker_decode_process_done(const(ubyte)* data, int len,
                                    const(ubyte)** outAudio, int* outAudioLen) {
    WorkerResponse resp;
    if (!decodeResponse(data, len, resp) || !resp.process_done.hasValue) {
        return false;
    }
    assignBytesOut(resp.process_done.value.output_audio, outAudio, outAudioLen);
    return true;
}

/// Decodes a WorkerResponse.param_value payload.
bool hbk_worker_decode_param_value(const(ubyte)* data, int len, double* outValue) {
    WorkerResponse resp;
    if (!decodeResponse(data, len, resp) || !resp.param_value.hasValue) {
        return false;
    }
    if (outValue !is null) *outValue = resp.param_value.value.value;
    return true;
}

/// Decodes a WorkerResponse.param_count payload.
bool hbk_worker_decode_param_count(const(ubyte)* data, int len, int* outCount) {
    WorkerResponse resp;
    if (!decodeResponse(data, len, resp) || !resp.param_count.hasValue) {
        return false;
    }
    if (outCount !is null) *outCount = resp.param_count.value.count;
    return true;
}

/// Decodes a WorkerResponse.param_info payload.
bool hbk_worker_decode_param_info(const(ubyte)* data, int len,
                                  bool* outFound, uint* outId,
                                  const(char)** outName, int* outNameLen,
                                  double* outDefaultValue) {
    WorkerResponse resp;
    if (!decodeResponse(data, len, resp) || !resp.param_info.hasValue) {
        return false;
    }
    const r = resp.param_info.value;
    if (outFound !is null) *outFound = r.found;
    if (outId !is null) *outId = r.id;
    assignStringOut(r.name, outName, outNameLen);
    if (outDefaultValue !is null) *outDefaultValue = r.default_value;
    return true;
}

/// Decodes a WorkerResponse.editor_frame payload (zero-copy image slice).
bool hbk_worker_decode_editor_frame(const(ubyte)* data, int len,
                                    int* outWidth, int* outHeight,
                                    const(ubyte)** outImage, int* outImageLen) {
    WorkerResponse resp;
    if (!decodeResponse(data, len, resp) || !resp.editor_frame.hasValue) {
        return false;
    }
    const r = resp.editor_frame.value;
    if (outWidth !is null) *outWidth = r.width;
    if (outHeight !is null) *outHeight = r.height;
    assignBytesOut(r.image_data, outImage, outImageLen);
    return true;
}

/// Decodes a WorkerResponse.state_result payload (zero-copy state slice).
bool hbk_worker_decode_state_result(const(ubyte)* data, int len,
                                    bool* outSuccess,
                                    const(ubyte)** outState, int* outStateLen) {
    WorkerResponse resp;
    if (!decodeResponse(data, len, resp) || !resp.state_result.hasValue) {
        return false;
    }
    const r = resp.state_result.value;
    if (outSuccess !is null) *outSuccess = r.success;
    assignBytesOut(r.state, outState, outStateLen);
    return true;
}

unittest {
    ubyte[512] buf;

    // 1. Encode/decode load request
    string pluginPath = "/Library/Audio/Plug-Ins/VST3/Synth.vst3";
    int n = hbk_worker_encode_load(pluginPath.ptr, cast(int)pluginPath.length, 2, 48000.0,
                                   buf.ptr, cast(int)buf.length);
    assert(n > 0);
    WorkerRequest decodedReq;
    assert(decodeProto(buf[0 .. n], decodedReq));
    assert(decodedReq.load.hasValue);
    assert(decodedReq.load.value.path == pluginPath);
    assert(decodedReq.load.value.plugin_index == 2);
    assert(decodedReq.load.value.sample_rate == 48000.0);

    // 2. Encode process with MIDI events & insufficient-buffer check
    HbkWorkerMidiEvent[2] evs = [
        HbkWorkerMidiEvent(10, 0, 60, 0.8f, true),
        HbkWorkerMidiEvent(120, 0, 60, 0.0f, false),
    ];
    ubyte[8] audio = [1, 2, 3, 4, 5, 6, 7, 8];
    int tooSmall = hbk_worker_encode_process(512, true, 44100.0, 128.0, 4, 4, 1024, 2.5,
                                             evs.ptr, 2, audio.ptr, cast(int)audio.length,
                                             buf.ptr, 4);
    assert(tooSmall < 0);
    n = hbk_worker_encode_process(512, true, 44100.0, 128.0, 4, 4, 1024, 2.5,
                                  evs.ptr, 2, audio.ptr, cast(int)audio.length,
                                  buf.ptr, cast(int)buf.length);
    assert(n == -tooSmall);
    WorkerRequest procReq;
    assert(decodeProto(buf[0 .. n], procReq));
    assert(procReq.process.hasValue);
    assert(procReq.process.value.midi_events.length == 2);
    assert(procReq.process.value.midi_events[0].pitch == 60);
    assert(procReq.process.value.input_audio == audio[]);

    // 3. Encode/decode WorkerResponse variants
    WorkerResponse resp;
    LoadResult lr;
    lr.success = true;
    lr.name = "Dexed";
    lr.is_instrument = true;
    resp.load_result.set(lr);
    auto rBytes = encodeProto(resp);
    bool ok = false, isInst = false;
    const(char)* namePtr = null;
    const(char)* errPtr = null;
    int nameLen = 0, errLen = 0;
    assert(hbk_worker_decode_load_result(rBytes.ptr, cast(int)rBytes.length,
                                         &ok, &namePtr, &nameLen, &isInst,
                                         &errPtr, &errLen));
    assert(ok && isInst);
    assert(namePtr[0 .. nameLen] == "Dexed");
    assert(errLen == 0);

    // 4. Decode param_info
    WorkerResponse pResp;
    ParamInfoResult pi;
    pi.found = true;
    pi.id = 42;
    pi.name = "Cutoff";
    pi.default_value = 0.75;
    pResp.param_info.set(pi);
    auto pBytes = encodeProto(pResp);
    bool found = false;
    uint pid = 0;
    const(char)* pNamePtr = null;
    int pNameLen = 0;
    double defVal = 0.0;
    assert(hbk_worker_decode_param_info(pBytes.ptr, cast(int)pBytes.length,
                                        &found, &pid, &pNamePtr, &pNameLen, &defVal));
    assert(found && pid == 42 && defVal == 0.75);
    assert(pNamePtr[0 .. pNameLen] == "Cutoff");
}
