module dproto.proto;

import dproto.wire;
import std.traits : isArray, isDynamicArray, isIntegral, isFloatingPoint, isBoolean, OriginalType, Unqual;

/// Attribute marking a struct member as a Protocol Buffers field.
struct ProtoField {
    uint number;
    bool packed = true;
    string oneofGroup = "";
}

/// Wrapper for proto3 `optional` fields that require explicit presence tracking.
struct ProtoOptional(T) {
    T value;
    bool hasValue = false;

    void set(T v) pure nothrow @safe {
        value = v;
        hasValue = true;
    }

    void clear() pure nothrow @safe {
        value = T.init;
        hasValue = false;
    }

    void opAssign(T v) pure nothrow @safe {
        set(v);
    }
}

private template isProtoOptional(T) {
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

/// Appends a varint to a dynamic byte array.
void appendVarint(ref ubyte[] outBuf, ulong val) pure nothrow @safe {
    ubyte[10] tmp;
    size_t n = writeVarint(tmp[], val);
    outBuf ~= tmp[0 .. n];
}

/// Appends a tag (fieldNumber, wireType) to a dynamic byte array.
void appendTag(ref ubyte[] outBuf, uint fieldNumber, WireType wt) pure nothrow @safe {
    appendVarint(outBuf, makeTag(fieldNumber, wt));
}

/// Encodes a single scalar value (without tag) into outBuf.
void encodeScalarValue(T)(ref ubyte[] outBuf, const auto ref T val) pure nothrow @safe {
    alias U = Unqual!T;
    static if (is(U == enum)) {
        ulong uval = (cast(int)val < 0) ? cast(ulong)(cast(long)cast(int)val) : cast(ulong)cast(int)val;
        appendVarint(outBuf, uval);
    } else static if (isBoolean!U) {
        appendVarint(outBuf, val ? 1UL : 0UL);
    } else static if (is(U == int)) {
        ulong uval = (val < 0) ? cast(ulong)(cast(long)val) : cast(ulong)val;
        appendVarint(outBuf, uval);
    } else static if (is(U == uint) || is(U == ulong) || is(U == long)) {
        appendVarint(outBuf, cast(ulong)val);
    } else static if (is(U == float)) {
        ubyte[4] tmp;
        writeFixed32Float(tmp[], val);
        outBuf ~= tmp[];
    } else static if (is(U == double)) {
        ubyte[8] tmp;
        writeFixed64Double(tmp[], val);
        outBuf ~= tmp[];
    } else static if (is(U : const(char)[])) {
        appendVarint(outBuf, val.length);
        outBuf ~= cast(const(ubyte)[])val;
    } else static if (is(U : const(ubyte)[])) {
        appendVarint(outBuf, val.length);
        outBuf ~= val;
    } else static if (is(U == struct)) {
        ubyte[] sub = encodeProto(val);
        appendVarint(outBuf, sub.length);
        outBuf ~= sub;
    } else {
        static assert(0, "Unsupported scalar proto type: " ~ T.stringof);
    }
}

/// Returns the WireType for a scalar or message type T.
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

/// Checks whether a scalar value equals its proto3 default and should be omitted.
bool isDefaultProtoValue(T)(const auto ref T val) pure nothrow @safe {
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
        return encodeProto(val).length == 0;
    } else {
        return false;
    }
}

/// Serializes any @ProtoField-annotated D struct into Protobuf binary wire format.
ubyte[] encodeProto(T)(const auto ref T msg) pure nothrow @safe
if (is(T == struct)) {
    ubyte[] outBuf;
    static foreach (idx; 0 .. T.tupleof.length) {{
        alias MemberType = typeof(T.tupleof[idx]);
        static foreach (attr; __traits(getAttributes, T.tupleof[idx])) {
            static if (is(typeof(attr) == ProtoField)) {
                encodeField!(MemberType, attr.number, attr.packed)(
                    outBuf,
                    msg.tupleof[idx]
                );
            }
        }
    }}
    return outBuf;
}

private void encodeField(FieldT, uint fieldNum, bool isPacked)(
    ref ubyte[] outBuf,
    const auto ref FieldT fieldVal
) pure nothrow @safe {
    static if (isProtoOptional!FieldT) {
        if (fieldVal.hasValue) {
            alias ValT = typeof(fieldVal.value);
            appendTag(outBuf, fieldNum, wireTypeFor!ValT);
            encodeScalarValue(outBuf, fieldVal.value);
        }
    } else static if (isDynamicArray!FieldT && !is(FieldT == string) && !is(FieldT == const(ubyte)[]) && !is(FieldT == ubyte[])) {
        alias ElemT = typeof(fieldVal[0]);
        if (fieldVal.length > 0) {
            static if (isPacked && (isIntegral!ElemT || isFloatingPoint!ElemT || isBoolean!ElemT || is(ElemT == enum))) {
                ubyte[] packedBuf;
                foreach (ref elem; fieldVal) {
                    encodeScalarValue(packedBuf, elem);
                }
                appendTag(outBuf, fieldNum, WireType.lengthDelimited);
                appendVarint(outBuf, packedBuf.length);
                outBuf ~= packedBuf;
            } else {
                foreach (ref elem; fieldVal) {
                    appendTag(outBuf, fieldNum, wireTypeFor!ElemT);
                    encodeScalarValue(outBuf, elem);
                }
            }
        }
    } else {
        if (!isDefaultProtoValue(fieldVal)) {
            appendTag(outBuf, fieldNum, wireTypeFor!FieldT);
            encodeScalarValue(outBuf, fieldVal);
        }
    }
}

/// Decodes a single scalar value from buf.
bool decodeScalarValue(T)(ref const(ubyte)[] buf, WireType wt, ref T target) pure nothrow @safe {
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
        target = cast(string)(buf[0 .. slen].idup);
        buf = buf[slen .. $];
        return true;
    } else static if (is(T == ubyte[])) {
        if (wt != WireType.lengthDelimited) return false;
        ulong len;
        if (!readVarint(buf, len) || buf.length < len) return false;
        size_t slen = cast(size_t)len;
        target = buf[0 .. slen].dup;
        buf = buf[slen .. $];
        return true;
    } else static if (is(T == struct)) {
        if (wt != WireType.lengthDelimited) return false;
        ulong len;
        if (!readVarint(buf, len) || buf.length < len) return false;
        size_t slen = cast(size_t)len;
        const(ubyte)[] subSlice = buf[0 .. slen];
        buf = buf[slen .. $];
        return decodeProto(subSlice, target);
    } else {
        static assert(0, "Unsupported decode scalar type: " ~ T.stringof);
    }
}

/// Deserializes Protobuf binary wire format into a @ProtoField-annotated D struct.
bool decodeProto(T)(const(ubyte)[] data, ref T msg) pure nothrow @safe
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
                        if (!decodeField!(MemberType)(buf, wt, msg.tupleof[idx])) {
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

private void clearOneofGroup(T, string groupName, uint keepFieldNum)(ref T msg) pure nothrow @safe {
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
    ref FieldT fieldVal
) pure nothrow @safe {
    static if (isProtoOptional!FieldT) {
        alias ValT = typeof(fieldVal.value);
        ValT tmp = fieldVal.value;
        if (!decodeScalarValue!ValT(buf, wt, tmp)) {
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
                    if (!decodeScalarValue!ElemT(sub, wireTypeFor!ElemT, elem)) {
                        return false;
                    }
                    fieldVal ~= elem;
                }
                return true;
            }
        }
        ElemT singleElem;
        if (!decodeScalarValue!ElemT(buf, wt, singleElem)) {
            return false;
        }
        fieldVal ~= singleElem;
        return true;
    } else {
        return decodeScalarValue!FieldT(buf, wt, fieldVal);
    }
}
