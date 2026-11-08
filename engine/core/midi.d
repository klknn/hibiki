module engine.core.midi;

import core.stdc.stdio : FILE, fclose, fopen, fread, fseek, ftell, SEEK_END, SEEK_SET;
import core.stdc.stdlib : free, malloc;

private uint readBE32Slice(const(ubyte)[] data, ref size_t pos) @nogc nothrow pure @safe {
    if (pos + 4 > data.length) {
        pos = data.length;
        return 0;
    }
    uint val = (cast(uint)data[pos] << 24)
        | (cast(uint)data[pos + 1] << 16)
        | (cast(uint)data[pos + 2] << 8)
        | cast(uint)data[pos + 3];
    pos += 4;
    return val;
}

private ushort readBE16Slice(const(ubyte)[] data, ref size_t pos) @nogc nothrow pure @safe {
    if (pos + 2 > data.length) {
        pos = data.length;
        return 0;
    }
    ushort val = cast(ushort)((cast(ushort)data[pos] << 8) | cast(ushort)data[pos + 1]);
    pos += 2;
    return val;
}

private uint readVLQSlice(const(ubyte)[] data, ref size_t pos, size_t limit) @nogc nothrow pure @safe {
    uint val = 0;
    while (pos < limit && pos < data.length) {
        ubyte b = data[pos++];
        val = (val << 7) | (b & 0x7F);
        if ((b & 0x80) == 0) {
            break;
        }
    }
    return val;
}

extern (C++, "hibiki") {

struct MidiEvent {
    double beats;
    ubyte type;
    ubyte channel;
    ubyte note;
    ubyte velocity;
}

bool isNoteOn(ref const(MidiEvent) ev) @nogc nothrow pure @safe {
    return (ev.type >= 0x90 && ev.type <= 0x9F) && ev.velocity > 0;
}

bool isNoteOff(ref const(MidiEvent) ev) @nogc nothrow pure @safe {
    return (ev.type >= 0x80 && ev.type <= 0x8F) ||
        ((ev.type >= 0x90 && ev.type <= 0x9F) && ev.velocity == 0);
}

/// Parses an SMF (Standard MIDI File) from a memory buffer in @nogc D.
/// Writes up to maxEvents into outEvents (or returns total event count if outEvents is null).
int d_parse_midi_bytes(
    const(ubyte)* buf,
    size_t len,
    MidiEvent* outEvents,
    int maxEvents
) @nogc nothrow pure {
    if (buf is null || len < 14) {
        return 0;
    }
    const(ubyte)[] data = buf[0 .. len];
    if (data[0 .. 4] != cast(const(ubyte)[])"MThd") {
        return 0;
    }
    size_t pos = 4;
    uint headerSize = readBE32Slice(data, pos);
    /* ushort format = */ readBE16Slice(data, pos);
    ushort numTracks = readBE16Slice(data, pos);
    ushort ticksPerQuarter = readBE16Slice(data, pos);
    if (ticksPerQuarter == 0) {
        return 0;
    }
    if (headerSize > 6) {
        size_t extra = headerSize - 6;
        if (pos + extra <= data.length) {
            pos += extra;
        } else {
            return 0;
        }
    }

    int count = 0;
    for (int t = 0; t < numTracks && pos + 8 <= data.length; ++t) {
        const(ubyte)[] chunkId = data[pos .. pos + 4];
        pos += 4;
        uint trackSize = readBE32Slice(data, pos);
        size_t trackEnd = pos + trackSize;
        if (trackEnd > data.length) {
            trackEnd = data.length;
        }
        if (chunkId != cast(const(ubyte)[])"MTrk") {
            pos = trackEnd;
            continue;
        }

        uint currentTicks = 0;
        ubyte runningStatus = 0;

        while (pos < trackEnd) {
            currentTicks += readVLQSlice(data, pos, trackEnd);
            if (pos >= trackEnd) {
                break;
            }
            ubyte status = data[pos++];
            if (status < 0x80) {
                status = runningStatus;
                --pos;
            } else if (status < 0xF0) {
                runningStatus = status;
            }

            ubyte type = status & 0xF0;
            if (type == 0x80 || type == 0x90 || type == 0xA0 || type == 0xB0 || type == 0xE0) {
                if (pos + 2 > trackEnd) {
                    break;
                }
                ubyte data1 = data[pos++];
                ubyte data2 = data[pos++];
                if (type == 0x80 || type == 0x90) {
                    if (outEvents !is null && count < maxEvents) {
                        MidiEvent ev;
                        ev.beats = cast(double)currentTicks / cast(double)ticksPerQuarter;
                        ev.type = status;
                        ev.channel = cast(ubyte)(status & 0x0F);
                        ev.note = data1;
                        ev.velocity = data2;
                        outEvents[count] = ev;
                    }
                    ++count;
                }
            } else if (type == 0xC0 || type == 0xD0) {
                if (pos < trackEnd) {
                    ++pos;
                }
            } else if (status == 0xFF) {
                if (pos >= trackEnd) {
                    break;
                }
                /* ubyte metaType = */ pos++;
                uint metaLen = readVLQSlice(data, pos, trackEnd);
                pos = (pos + metaLen <= trackEnd) ? (pos + metaLen) : trackEnd;
            } else if (status == 0xF0 || status == 0xF7) {
                uint sysexLen = readVLQSlice(data, pos, trackEnd);
                pos = (pos + sysexLen <= trackEnd) ? (pos + sysexLen) : trackEnd;
            }
        }
        pos = trackEnd;
    }

    // Sort events by beats ascending (stable insertion sort)
    if (outEvents !is null && count > 1) {
        int n = (count < maxEvents) ? count : maxEvents;
        for (int i = 1; i < n; ++i) {
            MidiEvent key = outEvents[i];
            int j = i - 1;
            while (j >= 0 && outEvents[j].beats > key.beats) {
                outEvents[j + 1] = outEvents[j];
                --j;
            }
            outEvents[j + 1] = key;
        }
    }
    return count;
}

} // extern (C++, "hibiki")
