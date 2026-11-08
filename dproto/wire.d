module dproto.wire;

/// Canonical Protocol Buffers wire types (proto3).
enum WireType : ubyte {
    varint = 0,
    fixed64 = 1,
    lengthDelimited = 2,
    fixed32 = 5,
}

/// Encodes a field number and wire type into a tag integer.
pragma(inline, true)
ulong makeTag(uint fieldNumber, WireType wireType) pure nothrow @nogc @safe {
    return (cast(ulong)fieldNumber << 3) | cast(ulong)wireType;
}

/// Extracts the field number from a decoded tag.
pragma(inline, true)
uint tagFieldNumber(ulong tag) pure nothrow @nogc @safe {
    return cast(uint)(tag >> 3);
}

/// Extracts the wire type from a decoded tag.
pragma(inline, true)
WireType tagWireType(ulong tag) pure nothrow @nogc @safe {
    return cast(WireType)(tag & 0x07);
}

/// Computes the encoded byte length of an unsigned 64-bit varint.
size_t varintSize(ulong val) pure nothrow @nogc @safe {
    size_t n = 1;
    while (val >= 0x80) {
        val >>= 7;
        ++n;
    }
    return n;
}

/// Computes the encoded byte length of a proto3 int32 (10 bytes if negative).
size_t int32Size(int val) pure nothrow @nogc @safe {
    ulong uval = (val < 0) ? cast(ulong)(cast(long)val) : cast(ulong)val;
    return varintSize(uval);
}

/// Writes an unsigned 64-bit integer into a fixed byte slice without allocation.
/// Returns the number of bytes written, or 0 if the destination buffer is too small.
size_t writeVarint(ubyte[] buf, ulong val) pure nothrow @nogc @safe {
    size_t written = 0;
    while (val >= 0x80) {
        if (written >= buf.length) {
            return 0;
        }
        buf[written++] = cast(ubyte)((val & 0x7F) | 0x80);
        val >>= 7;
    }
    if (written >= buf.length) {
        return 0;
    }
    buf[written++] = cast(ubyte)val;
    return written;
}

/// Writes a signed 32-bit integer using Protobuf's 10-byte sign-extension rule for negative values.
size_t writeInt32(ubyte[] buf, int val) pure nothrow @nogc @safe {
    ulong uval = (val < 0) ? cast(ulong)(cast(long)val) : cast(ulong)val;
    return writeVarint(buf, uval);
}

/// Writes a 32-bit IEEE 754 float in little-endian wire format (WireType.fixed32).
size_t writeFixed32Float(ubyte[] buf, float val) pure nothrow @nogc @trusted {
    if (buf.length < 4) {
        return 0;
    }
    uint bits = *cast(const(uint)*)&val;
    buf[0] = cast(ubyte)(bits & 0xFF);
    buf[1] = cast(ubyte)((bits >> 8) & 0xFF);
    buf[2] = cast(ubyte)((bits >> 16) & 0xFF);
    buf[3] = cast(ubyte)((bits >> 24) & 0xFF);
    return 4;
}

/// Writes a 64-bit IEEE 754 double in little-endian wire format (WireType.fixed64).
size_t writeFixed64Double(ubyte[] buf, double val) pure nothrow @nogc @trusted {
    if (buf.length < 8) {
        return 0;
    }
    ulong bits = *cast(const(ulong)*)&val;
    foreach (i; 0 .. 8) {
        buf[i] = cast(ubyte)((bits >> (i * 8)) & 0xFF);
    }
    return 8;
}

/// Reads an unsigned 64-bit varint from a byte slice, advancing the slice in-place.
bool readVarint(ref const(ubyte)[] buf, out ulong val) pure nothrow @nogc @safe {
    val = 0;
    uint shift = 0;
    while (buf.length > 0) {
        if (shift >= 64) {
            return false;
        }
        ubyte b = buf[0];
        buf = buf[1 .. $];
        if (shift == 63 && b > 1) {
            return false;
        }
        val |= cast(ulong)(b & 0x7F) << shift;
        if ((b & 0x80) == 0) {
            return true;
        }
        shift += 7;
    }
    return false;
}

/// Reads a signed 32-bit integer from a varint.
bool readInt32(ref const(ubyte)[] buf, out int val) pure nothrow @nogc @safe {
    ulong uval;
    if (!readVarint(buf, uval)) {
        val = 0;
        return false;
    }
    val = cast(int)uval;
    return true;
}

/// Reads a 32-bit IEEE 754 float in little-endian wire format.
bool readFixed32Float(ref const(ubyte)[] buf, out float val) pure nothrow @nogc @trusted {
    if (buf.length < 4) {
        val = 0.0f;
        return false;
    }
    uint bits = cast(uint)buf[0]
        | (cast(uint)buf[1] << 8)
        | (cast(uint)buf[2] << 16)
        | (cast(uint)buf[3] << 24);
    buf = buf[4 .. $];
    val = *cast(const(float)*)&bits;
    return true;
}

/// Reads a 64-bit IEEE 754 double in little-endian wire format.
bool readFixed64Double(ref const(ubyte)[] buf, out double val) pure nothrow @nogc @trusted {
    if (buf.length < 8) {
        val = 0.0;
        return false;
    }
    ulong bits = 0;
    foreach (i; 0 .. 8) {
        bits |= cast(ulong)buf[i] << (i * 8);
    }
    buf = buf[8 .. $];
    val = *cast(const(double)*)&bits;
    return true;
}

/// Skips an unknown field in the byte stream based on its wire type.
bool skipField(ref const(ubyte)[] buf, WireType wireType) pure nothrow @nogc @safe {
    switch (wireType) {
        case WireType.varint:
            ulong dummy;
            return readVarint(buf, dummy);
        case WireType.fixed64:
            if (buf.length < 8) {
                return false;
            }
            buf = buf[8 .. $];
            return true;
        case WireType.lengthDelimited:
            ulong len;
            if (!readVarint(buf, len) || buf.length < len) {
                return false;
            }
            buf = buf[cast(size_t)len .. $];
            return true;
        case WireType.fixed32:
            if (buf.length < 4) {
                return false;
            }
            buf = buf[4 .. $];
            return true;
        default:
            return false;
    }
}

/// Real-time @nogc audio packet for zero-heap IPC serialization.
struct RealtimeAudioPacket {
    int numSamples;
    double sampleRate = 48000.0;
    double tempo = 120.0;
    int timeSigNumerator = 4;
    int timeSigDenominator = 4;
    long continuousTimeSamples;
    double projectTimeMusic;
    bool hasInputs;
}

/// Encodes a real-time ProcessAudio header and inline audio slice into a fixed buffer without GC.
/// Returns total bytes written, or 0 if buffer capacity is insufficient.
size_t encodeRealtimeProcessAudio(
    const ref RealtimeAudioPacket pkt,
    const(float)[] inlineSamples,
    ubyte[] dest
) pure nothrow @nogc @safe {
    size_t pos = 0;

    bool putTagVarint(uint fieldNum, ulong val) {
        size_t n1 = writeVarint(dest[pos .. $], makeTag(fieldNum, WireType.varint));
        if (n1 == 0) return false;
        pos += n1;
        size_t n2 = writeVarint(dest[pos .. $], val);
        if (n2 == 0) return false;
        pos += n2;
        return true;
    }

    bool putTagDouble(uint fieldNum, double val) {
        size_t n1 = writeVarint(dest[pos .. $], makeTag(fieldNum, WireType.fixed64));
        if (n1 == 0) return false;
        pos += n1;
        size_t n2 = writeFixed64Double(dest[pos .. $], val);
        if (n2 == 0) return false;
        pos += n2;
        return true;
    }

    if (pkt.numSamples != 0) {
        ulong uval = (pkt.numSamples < 0) ? cast(ulong)(cast(long)pkt.numSamples) : cast(ulong)pkt.numSamples;
        if (!putTagVarint(1, uval)) return 0;
    }
    if (pkt.sampleRate != 0.0) {
        if (!putTagDouble(3, pkt.sampleRate)) return 0;
    }
    if (pkt.tempo != 0.0) {
        if (!putTagDouble(4, pkt.tempo)) return 0;
    }
    if (pkt.timeSigNumerator != 0) {
        if (!putTagVarint(5, cast(ulong)(cast(long)pkt.timeSigNumerator))) return 0;
    }
    if (pkt.timeSigDenominator != 0) {
        if (!putTagVarint(6, cast(ulong)(cast(long)pkt.timeSigDenominator))) return 0;
    }
    if (pkt.continuousTimeSamples != 0) {
        if (!putTagVarint(7, cast(ulong)pkt.continuousTimeSamples)) return 0;
    }
    if (pkt.projectTimeMusic != 0.0) {
        if (!putTagDouble(8, pkt.projectTimeMusic)) return 0;
    }
    if (pkt.hasInputs) {
        if (!putTagVarint(9, 1)) return 0;
    }
    if (inlineSamples.length > 0) {
        size_t byteLen = inlineSamples.length * 4;
        size_t n1 = writeVarint(dest[pos .. $], makeTag(10, WireType.lengthDelimited));
        if (n1 == 0) return 0;
        pos += n1;
        size_t n2 = writeVarint(dest[pos .. $], byteLen);
        if (n2 == 0) return 0;
        pos += n2;
        if (dest.length - pos < byteLen) return 0;
        foreach (sample; inlineSamples) {
            writeFixed32Float(dest[pos .. pos + 4], sample);
            pos += 4;
        }
    }
    return pos;
}
