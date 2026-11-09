module pb.dproto;

/// Canonical Protocol Buffers wire types (proto3).
enum WireType : ubyte {
    varint = 0,
    fixed64 = 1,
    lengthDelimited = 2,
    fixed32 = 5,
}

// -----------------------------------------------------------------------------
// D type traits implemented without Phobos (zero-std dependency)
// -----------------------------------------------------------------------------

template Unqual(T) {
    static if (is(T U == const U)) {
        alias Unqual = Unqual!U;
    } else static if (is(T U == immutable U)) {
        alias Unqual = Unqual!U;
    } else static if (is(T U == inout U)) {
        alias Unqual = Unqual!U;
    } else static if (is(T U == shared U)) {
        alias Unqual = Unqual!U;
    } else {
        alias Unqual = T;
    }
}

template isIntegral(T) {
    alias U = Unqual!T;
    enum isIntegral = is(U == byte) || is(U == ubyte) ||
                      is(U == short) || is(U == ushort) ||
                      is(U == int) || is(U == uint) ||
                      is(U == long) || is(U == ulong);
}

template isFloatingPoint(T) {
    alias U = Unqual!T;
    enum isFloatingPoint = is(U == float) || is(U == double) || is(U == real);
}

template isBoolean(T) {
    alias U = Unqual!T;
    enum isBoolean = is(U == bool);
}

template isDynamicArray(T) {
    alias U = Unqual!T;
    static if (is(U : E[], E) && !is(U : E[N], E, size_t N)) {
        enum isDynamicArray = true;
    } else {
        enum isDynamicArray = false;
    }
}

template isArray(T) {
    alias U = Unqual!T;
    enum isArray = is(U : E[], E);
}

// -----------------------------------------------------------------------------
// Attribute and Optional type
// -----------------------------------------------------------------------------

struct ProtoField {
    uint number;
    bool packed = true;
    string oneofGroup = "";
}

struct ProtoOptional(T) {
    T value;
    bool hasValue = false;

    void set(T v) pure nothrow @nogc @safe {
        value = v;
        hasValue = true;
    }

    void clear() pure nothrow @nogc @safe {
        value = T.init;
        hasValue = false;
    }

    void opAssign(T v) pure nothrow @nogc @safe {
        set(v);
    }
}

template isProtoOptional(T) {
    alias U = Unqual!T;
    enum isProtoOptional = __traits(isSame, TemplateOf!U, ProtoOptional);
}

private template TemplateOf(alias T) {
    static if (__traits(compiles, { alias X = TemplateOfImpl!T; })) {
        alias TemplateOf = TemplateOfImpl!T;
    } else {
        alias TemplateOf = void;
    }
}

private template TemplateOfImpl(alias T : Base!Args, alias Base, Args...) {
    alias TemplateOfImpl = Base;
}

// -----------------------------------------------------------------------------
// Low-level Wire Primitives (pure nothrow @nogc)
// -----------------------------------------------------------------------------

pragma(inline, true)
ulong makeTag(uint fieldNumber, WireType wireType) pure nothrow @nogc @safe {
    return (cast(ulong)fieldNumber << 3) | cast(ulong)wireType;
}

pragma(inline, true)
uint tagFieldNumber(ulong tag) pure nothrow @nogc @safe {
    return cast(uint)(tag >> 3);
}

pragma(inline, true)
WireType tagWireType(ulong tag) pure nothrow @nogc @safe {
    return cast(WireType)(tag & 0x07);
}

size_t varintSize(ulong val) pure nothrow @nogc @safe {
    size_t n = 1;
    while (val >= 0x80) {
        val >>= 7;
        ++n;
    }
    return n;
}

size_t int32Size(int val) pure nothrow @nogc @safe {
    ulong uval = (val < 0) ? cast(ulong)(cast(long)val) : cast(ulong)val;
    return varintSize(uval);
}

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

size_t writeInt32(ubyte[] buf, int val) pure nothrow @nogc @safe {
    ulong uval = (val < 0) ? cast(ulong)(cast(long)val) : cast(ulong)val;
    return writeVarint(buf, uval);
}

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

bool readInt32(ref const(ubyte)[] buf, out int val) pure nothrow @nogc @safe {
    ulong uval;
    if (!readVarint(buf, uval)) {
        val = 0;
        return false;
    }
    val = cast(int)uval;
    return true;
}

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

bool skipField(ref const(ubyte)[] buf, WireType wireType) pure nothrow @nogc @safe {
    switch (wireType) {
        case WireType.varint:
            ulong dummy;
            return readVarint(buf, dummy);
        case WireType.fixed64:
            if (buf.length < 8) return false;
            buf = buf[8 .. $];
            return true;
        case WireType.lengthDelimited:
            ulong len;
            if (!readVarint(buf, len) || buf.length < len) return false;
            buf = buf[cast(size_t)len .. $];
            return true;
        case WireType.fixed32:
            if (buf.length < 4) return false;
            buf = buf[4 .. $];
            return true;
        default:
            return false;
    }
}

// -----------------------------------------------------------------------------
// Realtime Audio Packet Primitives
// -----------------------------------------------------------------------------

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

// -----------------------------------------------------------------------------
// Arena Allocator for @nogc Memory Management
// -----------------------------------------------------------------------------

struct Arena {
    ubyte[] buffer;
    size_t used;
    bool isHeapAllocated;

    this(ubyte[] buf) pure nothrow @nogc @safe {
        this.buffer = buf;
        this.used = 0;
        this.isHeapAllocated = false;
    }

    this(void* ptr, size_t size) pure nothrow @nogc @trusted {
        this.buffer = (ptr !is null && size > 0) ? (cast(ubyte*)ptr)[0 .. size] : null;
        this.used = 0;
        this.isHeapAllocated = false;
    }

    static Arena createHeap(size_t size = 65536) nothrow @nogc @trusted {
        import core.stdc.stdlib : malloc;
        void* p = malloc(size);
        Arena a = Arena(p, size);
        a.isHeapAllocated = true;
        return a;
    }

    void freeHeap() nothrow @nogc @trusted {
        if (isHeapAllocated && buffer.ptr !is null) {
            import core.stdc.stdlib : free;
            free(buffer.ptr);
            buffer = null;
            used = 0;
            isHeapAllocated = false;
        }
    }

    void reset() pure nothrow @nogc @safe {
        used = 0;
    }

    size_t capacity() const pure nothrow @nogc @safe {
        return buffer.length;
    }

    size_t bytesUsed() const pure nothrow @nogc @safe {
        return used;
    }

    size_t bytesRemaining() const pure nothrow @nogc @safe {
        return buffer.length - used;
    }

    void* alloc(size_t size, size_t alignment = 8) pure nothrow @nogc @trusted {
        if (size == 0) return null;
        size_t pad = (alignment - (used % alignment)) % alignment;
        if (used + pad + size > buffer.length) {
            return null;
        }
        used += pad;
        void* ptr = buffer.ptr + used;
        used += size;
        return ptr;
    }

    T[] allocSlice(T)(size_t count) pure nothrow @nogc @trusted {
        if (count == 0) return null;
        void* ptr = alloc(count * T.sizeof, T.alignof);
        if (ptr is null) return null;
        return (cast(T*)ptr)[0 .. count];
    }

    T[] reallocSlice(T)(T[] oldSlice, size_t newCount) pure nothrow @nogc @trusted {
        if (newCount == 0) return null;
        if (oldSlice.length == 0) return allocSlice!T(newCount);

        ubyte* oldBytes = cast(ubyte*)oldSlice.ptr;
        size_t oldByteSize = oldSlice.length * T.sizeof;
        if (oldBytes !is null && oldBytes + oldByteSize == buffer.ptr + used) {
            size_t newByteSize = newCount * T.sizeof;
            if (oldBytes + newByteSize <= buffer.ptr + buffer.length) {
                used = (oldBytes - buffer.ptr) + newByteSize;
                return (cast(T*)oldBytes)[0 .. newCount];
            }
        }

        T[] newSlice = allocSlice!T(newCount);
        if (newSlice is null) return null;
        size_t copyCount = (oldSlice.length < newCount) ? oldSlice.length : newCount;
        foreach (i; 0 .. copyCount) {
            newSlice[i] = oldSlice[i];
        }
        return newSlice;
    }
}

// -----------------------------------------------------------------------------
// Protobuf Serialization / Deserialization (Zero-Phobos, @nogc with Arena)
// -----------------------------------------------------------------------------

template wireTypeFor(T) {
    alias U = Unqual!T;
    static if (is(U == enum) || isBoolean!U || isIntegral!U) {
        enum wireTypeFor = WireType.varint;
    } else static if (is(U == float)) {
        enum wireTypeFor = WireType.fixed32;
    } else static if (is(U == double)) {
        enum wireTypeFor = WireType.fixed64;
    } else static if (is(U : const(char)[]) || is(U : const(ubyte)[]) || is(U == struct)) {
        enum wireTypeFor = WireType.lengthDelimited;
    } else {
        static assert(0, "No WireType mapping for " ~ T.stringof);
    }
}

bool isDefaultProtoValue(T)(const auto ref T val) pure nothrow @nogc @safe {
    alias U = Unqual!T;
    static if (is(U == enum)) {
        return cast(int)val == 0;
    } else static if (isBoolean!U) {
        return !val;
    } else static if (isIntegral!U) {
        return val == 0;
    } else static if (isFloatingPoint!U) {
        return val == 0 && (val == 0.0);
    } else static if (is(U : const(char)[]) || is(U : const(ubyte)[])) {
        return val.length == 0;
    } else static if (is(U == struct)) {
        static foreach (idx; 0 .. U.tupleof.length) {
            if (!isDefaultProtoValue(val.tupleof[idx])) return false;
        }
        return true;
    } else {
        return false;
    }
}

size_t computeScalarSize(T)(const auto ref T val) pure nothrow @nogc @safe {
    alias U = Unqual!T;
    static if (is(U == enum)) {
        ulong uval = (cast(int)val < 0) ? cast(ulong)(cast(long)cast(int)val) : cast(ulong)cast(int)val;
        return varintSize(uval);
    } else static if (isBoolean!U) {
        return 1;
    } else static if (is(U == int)) {
        ulong uval = (val < 0) ? cast(ulong)(cast(long)val) : cast(ulong)val;
        return varintSize(uval);
    } else static if (is(U == uint) || is(U == ulong) || is(U == long)) {
        return varintSize(cast(ulong)val);
    } else static if (is(U == float)) {
        return 4;
    } else static if (is(U == double)) {
        return 8;
    } else static if (is(U : const(char)[]) || is(U : const(ubyte)[])) {
        return varintSize(val.length) + val.length;
    } else static if (is(U == struct)) {
        size_t subSize = computeProtoSize(val);
        return varintSize(subSize) + subSize;
    } else {
        static assert(0, "Unsupported scalar type: " ~ T.stringof);
    }
}

size_t computeFieldSize(FieldT, uint fieldNum, bool isPacked)(const auto ref FieldT fieldVal) pure nothrow @nogc @safe {
    static if (isProtoOptional!FieldT) {
        if (!fieldVal.hasValue) return 0;
        alias ValT = typeof(fieldVal.value);
        return varintSize(makeTag(fieldNum, wireTypeFor!ValT)) + computeScalarSize(fieldVal.value);
    } else static if (isDynamicArray!FieldT && !is(FieldT == string) && !is(FieldT == const(ubyte)[]) && !is(FieldT == ubyte[])) {
        alias ElemT = typeof(fieldVal[0]);
        if (fieldVal.length == 0) return 0;
        static if (isPacked && (isIntegral!ElemT || isFloatingPoint!ElemT || isBoolean!ElemT || is(ElemT == enum))) {
            size_t payloadSize = 0;
            foreach (ref elem; fieldVal) {
                payloadSize += computeScalarSize(elem);
            }
            return varintSize(makeTag(fieldNum, WireType.lengthDelimited))
                 + varintSize(payloadSize)
                 + payloadSize;
        } else {
            size_t total = 0;
            foreach (ref elem; fieldVal) {
                total += varintSize(makeTag(fieldNum, wireTypeFor!ElemT)) + computeScalarSize(elem);
            }
            return total;
        }
    } else {
        if (isDefaultProtoValue(fieldVal)) return 0;
        return varintSize(makeTag(fieldNum, wireTypeFor!FieldT)) + computeScalarSize(fieldVal);
    }
}

/// Computes the exact encoded binary Protobuf size for a struct message without allocating.
size_t computeProtoSize(T)(const auto ref T msg) pure nothrow @nogc @safe
if (is(T == struct)) {
    size_t total = 0;
    static foreach (idx; 0 .. T.tupleof.length) {{
        alias MemberType = typeof(T.tupleof[idx]);
        static foreach (attr; __traits(getAttributes, T.tupleof[idx])) {
            static if (is(typeof(attr) == ProtoField)) {
                total += computeFieldSize!(MemberType, attr.number, attr.packed)(msg.tupleof[idx]);
            }
        }
    }}
    return total;
}

struct ProtoWriter {
    ubyte[] dest;
    size_t written;

    bool putBytes(const(ubyte)[] bytes) pure nothrow @nogc @safe {
        if (dest.length - written < bytes.length) return false;
        dest[written .. written + bytes.length] = bytes[];
        written += bytes.length;
        return true;
    }

    bool putVarint(ulong val) pure nothrow @nogc @safe {
        ubyte[10] tmp;
        size_t n = writeVarint(tmp[], val);
        return putBytes(tmp[0 .. n]);
    }

    bool putTag(uint fieldNum, WireType wt) pure nothrow @nogc @safe {
        return putVarint(makeTag(fieldNum, wt));
    }
}

private bool encodeScalarValue(T)(ref ProtoWriter writer, const auto ref T val) pure nothrow @nogc @safe {
    alias U = Unqual!T;
    static if (is(U == enum)) {
        ulong uval = (cast(int)val < 0) ? cast(ulong)(cast(long)cast(int)val) : cast(ulong)cast(int)val;
        return writer.putVarint(uval);
    } else static if (isBoolean!U) {
        return writer.putVarint(val ? 1UL : 0UL);
    } else static if (is(U == int)) {
        ulong uval = (val < 0) ? cast(ulong)(cast(long)val) : cast(ulong)val;
        return writer.putVarint(uval);
    } else static if (is(U == uint) || is(U == ulong) || is(U == long)) {
        return writer.putVarint(cast(ulong)val);
    } else static if (is(U == float)) {
        ubyte[4] tmp;
        writeFixed32Float(tmp[], val);
        return writer.putBytes(tmp[]);
    } else static if (is(U == double)) {
        ubyte[8] tmp;
        writeFixed64Double(tmp[], val);
        return writer.putBytes(tmp[]);
    } else static if (is(U : const(char)[]) || is(U : const(ubyte)[])) {
        if (!writer.putVarint(val.length)) return false;
        return writer.putBytes(cast(const(ubyte)[])val);
    } else static if (is(U == struct)) {
        size_t subSize = computeProtoSize(val);
        if (!writer.putVarint(subSize)) return false;
        return encodeStructFields(writer, val);
    } else {
        static assert(0, "Unsupported scalar type: " ~ T.stringof);
    }
}

private bool encodeField(FieldT, uint fieldNum, bool isPacked)(
    ref ProtoWriter writer,
    const auto ref FieldT fieldVal
) pure nothrow @nogc @safe {
    static if (isProtoOptional!FieldT) {
        if (fieldVal.hasValue) {
            alias ValT = typeof(fieldVal.value);
            if (!writer.putTag(fieldNum, wireTypeFor!ValT)) return false;
            return encodeScalarValue(writer, fieldVal.value);
        }
        return true;
    } else static if (isDynamicArray!FieldT && !is(FieldT == string) && !is(FieldT == const(ubyte)[]) && !is(FieldT == ubyte[])) {
        alias ElemT = typeof(fieldVal[0]);
        if (fieldVal.length > 0) {
            static if (isPacked && (isIntegral!ElemT || isFloatingPoint!ElemT || isBoolean!ElemT || is(ElemT == enum))) {
                size_t payloadSize = 0;
                foreach (ref elem; fieldVal) {
                    payloadSize += computeScalarSize(elem);
                }
                if (!writer.putTag(fieldNum, WireType.lengthDelimited)) return false;
                if (!writer.putVarint(payloadSize)) return false;
                foreach (ref elem; fieldVal) {
                    if (!encodeScalarValue(writer, elem)) return false;
                }
                return true;
            } else {
                foreach (ref elem; fieldVal) {
                    if (!writer.putTag(fieldNum, wireTypeFor!ElemT)) return false;
                    if (!encodeScalarValue(writer, elem)) return false;
                }
                return true;
            }
        }
        return true;
    } else {
        if (!isDefaultProtoValue(fieldVal)) {
            if (!writer.putTag(fieldNum, wireTypeFor!FieldT)) return false;
            return encodeScalarValue(writer, fieldVal);
        }
        return true;
    }
}

private bool encodeStructFields(T)(ref ProtoWriter writer, const auto ref T msg) pure nothrow @nogc @safe
if (is(T == struct)) {
    static foreach (idx; 0 .. T.tupleof.length) {{
        alias MemberType = typeof(T.tupleof[idx]);
        static foreach (attr; __traits(getAttributes, T.tupleof[idx])) {
            static if (is(typeof(attr) == ProtoField)) {
                if (!encodeField!(MemberType, attr.number, attr.packed)(writer, msg.tupleof[idx])) {
                    return false;
                }
            }
        }
    }}
    return true;
}

/// Encodes a struct message into a user-provided destination buffer without GC allocations.
/// Returns the number of bytes written, or 0 if dest buffer is insufficient.
size_t encodeProto(T)(const auto ref T msg, ubyte[] dest) pure nothrow @nogc @safe
if (is(T == struct)) {
    ProtoWriter writer = ProtoWriter(dest, 0);
    if (!encodeStructFields(writer, msg)) {
        return 0;
    }
    return writer.written;
}

/// Encodes a struct message allocating directly from the Arena without GC.
ubyte[] encodeProto(T)(const auto ref T msg, ref Arena arena) pure nothrow @nogc @safe
if (is(T == struct)) {
    size_t needed = computeProtoSize(msg);
    if (needed == 0) return null;
    ubyte[] slice = arena.allocSlice!ubyte(needed);
    if (slice.ptr is null) return null;
    size_t written = encodeProto(msg, slice);
    return slice[0 .. written];
}

/// Convenient @nogc encoder using a stack/scratch buffer or fallback arena.
ubyte[] encodeProto(T)(const auto ref T msg) nothrow @nogc
if (is(T == struct)) {
    size_t needed = computeProtoSize(msg);
    if (needed == 0) return null;
    static ubyte[131072] scratch;
    if (needed <= scratch.length) {
        size_t written = encodeProto(msg, scratch[]);
        return scratch[0 .. written];
    }
    Arena heapArena = Arena.createHeap(needed + 1024);
    return encodeProto(msg, heapArena);
}

// -----------------------------------------------------------------------------
// Deserializer (@nogc with Arena)
// -----------------------------------------------------------------------------

private bool decodeScalarValue(T)(
    ref const(ubyte)[] buf,
    WireType wt,
    ref T target,
    ref Arena arena
) pure nothrow @nogc @safe {
    static if (is(T == enum)) {
        if (wt != WireType.varint) return false;
        int v;
        if (!readInt32(buf, v)) return false;
        target = cast(T)v;
        return true;
    } else static if (isBoolean!T) {
        if (wt != WireType.varint) return false;
        ulong v;
        if (!readVarint(buf, v)) return false;
        target = (v != 0);
        return true;
    } else static if (is(T == int)) {
        if (wt != WireType.varint) return false;
        return readInt32(buf, target);
    } else static if (is(T == uint)) {
        if (wt != WireType.varint) return false;
        ulong v;
        if (!readVarint(buf, v)) return false;
        target = cast(uint)v;
        return true;
    } else static if (is(T == long)) {
        if (wt != WireType.varint) return false;
        ulong v;
        if (!readVarint(buf, v)) return false;
        target = cast(long)v;
        return true;
    } else static if (is(T == ulong)) {
        if (wt != WireType.varint) return false;
        return readVarint(buf, target);
    } else static if (is(T == float)) {
        if (wt != WireType.fixed32) return false;
        return readFixed32Float(buf, target);
    } else static if (is(T == double)) {
        if (wt != WireType.fixed64) return false;
        return readFixed64Double(buf, target);
    } else static if (is(T == string)) {
        if (wt != WireType.lengthDelimited) return false;
        ulong len;
        if (!readVarint(buf, len) || buf.length < len) return false;
        size_t slen = cast(size_t)len;
        if (slen == 0) {
            target = "";
            return true;
        }
        char[] copy = arena.allocSlice!char(slen);
        if (copy.ptr is null) return false;
        foreach (i; 0 .. slen) {
            copy[i] = cast(char)buf[i];
        }
        target = () @trusted { return cast(string)copy; }();
        buf = buf[slen .. $];
        return true;
    } else static if (is(T == ubyte[])) {
        if (wt != WireType.lengthDelimited) return false;
        ulong len;
        if (!readVarint(buf, len) || buf.length < len) return false;
        size_t slen = cast(size_t)len;
        if (slen == 0) {
            target = null;
            return true;
        }
        ubyte[] copy = arena.allocSlice!ubyte(slen);
        if (copy.ptr is null) return false;
        foreach (i; 0 .. slen) {
            copy[i] = buf[i];
        }
        target = copy;
        buf = buf[slen .. $];
        return true;
    } else static if (is(T == struct)) {
        if (wt != WireType.lengthDelimited) return false;
        ulong len;
        if (!readVarint(buf, len) || buf.length < len) return false;
        size_t slen = cast(size_t)len;
        const(ubyte)[] subSlice = buf[0 .. slen];
        buf = buf[slen .. $];
        return decodeProto(subSlice, target, arena);
    } else {
        static assert(0, "Unsupported decode scalar type: " ~ T.stringof);
    }
}

private void appendToSlice(T)(ref T[] slice, auto ref T elem, ref Arena arena) pure nothrow @nogc @safe {
    size_t oldLen = slice.length;
    slice = arena.reallocSlice!T(slice, oldLen + 1);
    if (slice.length > oldLen) {
        slice[oldLen] = elem;
    }
}

private void clearOneofGroup(T, string groupName, uint keepFieldNum)(ref T msg) pure nothrow @nogc @safe {
    static foreach (idx; 0 .. T.tupleof.length) {{
        static foreach (attr; __traits(getAttributes, T.tupleof[idx])) {
            static if (is(typeof(attr) == ProtoField)) {
                static if (attr.oneofGroup == groupName && attr.number != keepFieldNum) {
                    msg.tupleof[idx].clear();
                }
            }
        }
    }}
}

private bool decodeField(FieldT)(
    ref const(ubyte)[] buf,
    WireType wt,
    ref FieldT fieldVal,
    ref Arena arena
) pure nothrow @nogc @safe {
    static if (isProtoOptional!FieldT) {
        alias ValT = typeof(fieldVal.value);
        ValT tmp = fieldVal.value;
        if (!decodeScalarValue!ValT(buf, wt, tmp, arena)) {
            return false;
        }
        fieldVal.set(tmp);
        return true;
    } else static if (isDynamicArray!FieldT && !is(FieldT == string) && !is(FieldT == ubyte[])) {
        alias ElemT = typeof(fieldVal[0]);
        static if (isIntegral!ElemT || isFloatingPoint!ElemT || isBoolean!ElemT || is(ElemT == enum)) {
            if (wt == WireType.lengthDelimited) {
                ulong len;
                if (!readVarint(buf, len) || buf.length < len) {
                    return false;
                }
                size_t slen = cast(size_t)len;
                const(ubyte)[] sub = buf[0 .. slen];
                buf = buf[slen .. $];
                while (sub.length > 0) {
                    ElemT elem;
                    if (!decodeScalarValue!ElemT(sub, wireTypeFor!ElemT, elem, arena)) {
                        return false;
                    }
                    appendToSlice!ElemT(fieldVal, elem, arena);
                }
                return true;
            }
        }
        ElemT singleElem;
        if (!decodeScalarValue!ElemT(buf, wt, singleElem, arena)) {
            return false;
        }
        appendToSlice!ElemT(fieldVal, singleElem, arena);
        return true;
    } else {
        return decodeScalarValue!FieldT(buf, wt, fieldVal, arena);
    }
}

/// Deserializes binary Protobuf wire data into a struct using Arena for memory allocation (@nogc).
bool decodeProto(T)(const(ubyte)[] data, ref T msg, ref Arena arena) pure nothrow @nogc @safe
if (is(T == struct)) {
    const(ubyte)[] buf = data;
    while (buf.length > 0) {
        ulong tag;
        if (!readVarint(buf, tag)) {
            return false;
        }
        uint fieldNum = tagFieldNumber(tag);
        WireType wt = tagWireType(tag);
        if (fieldNum == 0) {
            return false;
        }

        bool matched = false;
        static foreach (idx; 0 .. T.tupleof.length) {{
            alias MemberType = typeof(T.tupleof[idx]);
            static foreach (attr; __traits(getAttributes, T.tupleof[idx])) {
                static if (is(typeof(attr) == ProtoField)) {
                    if (!matched && fieldNum == attr.number) {
                        matched = true;
                        static if (attr.oneofGroup.length > 0) {
                            clearOneofGroup!(T, attr.oneofGroup, attr.number)(msg);
                        }
                        if (!decodeField!(MemberType)(buf, wt, msg.tupleof[idx], arena)) {
                            return false;
                        }
                    }
                }
            }
        }}

        if (!matched) {
            if (!skipField(buf, wt)) {
                return false;
            }
        }
    }
    return true;
}

/// Convenient @nogc decoder using a scratch Arena buffer or heap fallback.
bool decodeProto(T)(const(ubyte)[] data, ref T msg) nothrow @nogc
if (is(T == struct)) {
    static ubyte[262144] scratch;
    Arena arena = Arena(scratch[]);
    if (decodeProto(data, msg, arena)) {
        return true;
    }
    // If scratch buffer exhausted, try dynamic heap arena
    Arena heapArena = Arena.createHeap(data.length * 8 + 65536);
    return decodeProto(data, msg, heapArena);
}
