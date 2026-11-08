module engine.core.audio_file;

import engine.core.math : clampVal;

private ushort readLE16(const(ubyte)[] data, size_t offset) @nogc nothrow pure @safe {
    return cast(ushort)(cast(ushort)data[offset] | (cast(ushort)data[offset + 1] << 8));
}

private uint readLE32(const(ubyte)[] data, size_t offset) @nogc nothrow pure @safe {
    return cast(uint)data[offset]
        | (cast(uint)data[offset + 1] << 8)
        | (cast(uint)data[offset + 2] << 16)
        | (cast(uint)data[offset + 3] << 24);
}

private float int24ToFloat(const(ubyte)* bytes) @nogc nothrow pure {
    int val = bytes[0] | (bytes[1] << 8) | (bytes[2] << 16);
    if (val & 0x800000) {
        val |= 0xFF000000;
    }
    return cast(float)val / 8388608.0f;
}

extern (C++, "hibiki") {

enum int WAV_OK = 0;
enum int WAV_ERR_NOT_RIFF = 1;
enum int WAV_ERR_NOT_WAVE = 2;
enum int WAV_ERR_DATA_BEFORE_FMT = 3;
enum int WAV_ERR_UNSUPPORTED_PCM_DEPTH = 4;
enum int WAV_ERR_UNSUPPORTED_FLOAT_DEPTH = 5;
enum int WAV_ERR_UNSUPPORTED_FORMAT = 6;
enum int WAV_ERR_NO_DATA = 7;

/// Inspects a WAV byte buffer in @nogc D, validating headers and returning sample count and format info.
int d_inspect_wav_bytes(
    const(ubyte)* raw,
    size_t len,
    int* out_channels,
    int* out_sample_rate,
    int* out_bits_per_sample,
    int* out_audio_format,
    int* out_total_samples,
    size_t* out_data_offset,
    size_t* out_data_size
) @nogc nothrow pure {
    if (raw is null || len < 12) {
        return WAV_ERR_NOT_RIFF;
    }
    const(ubyte)[] data = raw[0 .. len];
    if (data[0 .. 4] != cast(const(ubyte)[])"RIFF") {
        return WAV_ERR_NOT_RIFF;
    }
    if (data[8 .. 12] != cast(const(ubyte)[])"WAVE") {
        return WAV_ERR_NOT_WAVE;
    }

    int sample_rate = 0;
    int channels = 0;
    int bits_per_sample = 0;
    ushort audio_format = 0;

    size_t pos = 12;
    while (pos + 8 <= data.length) {
        const(ubyte)[] id = data[pos .. pos + 4];
        uint chunk_size = readLE32(data, pos + 4);
        pos += 8;

        if (id == cast(const(ubyte)[])"fmt ") {
            if (pos + 16 > data.length) {
                return WAV_ERR_NO_DATA;
            }
            audio_format = readLE16(data, pos);
            channels = readLE16(data, pos + 2);
            sample_rate = cast(int)readLE32(data, pos + 4);
            bits_per_sample = readLE16(data, pos + 14);

            if (audio_format == 0xFFFE && chunk_size >= 40 && pos + 26 <= data.length) {
                audio_format = readLE16(data, pos + 24);
            }
            pos = (pos + chunk_size <= data.length) ? (pos + chunk_size) : data.length;
        } else if (id == cast(const(ubyte)[])"data") {
            if (channels <= 0 || sample_rate <= 0 || bits_per_sample <= 0) {
                return WAV_ERR_DATA_BEFORE_FMT;
            }
            if (audio_format == 1) {
                if (bits_per_sample != 16 && bits_per_sample != 24 && bits_per_sample != 32) {
                    *out_bits_per_sample = bits_per_sample;
                    return WAV_ERR_UNSUPPORTED_PCM_DEPTH;
                }
            } else if (audio_format == 3) {
                if (bits_per_sample != 32 && bits_per_sample != 64) {
                    *out_bits_per_sample = bits_per_sample;
                    return WAV_ERR_UNSUPPORTED_FLOAT_DEPTH;
                }
            } else {
                *out_audio_format = audio_format;
                return WAV_ERR_UNSUPPORTED_FORMAT;
            }

            int bytes_per_sample = bits_per_sample / 8;
            int total_samples = cast(int)(chunk_size / bytes_per_sample);
            *out_channels = channels;
            *out_sample_rate = sample_rate;
            *out_bits_per_sample = bits_per_sample;
            *out_audio_format = audio_format;
            *out_total_samples = total_samples;
            *out_data_offset = pos;
            *out_data_size = chunk_size;
            return WAV_OK;
        } else {
            size_t padded = (chunk_size + 1) & ~cast(uint)1;
            pos = (pos + padded <= data.length) ? (pos + padded) : data.length;
        }
    }
    return WAV_ERR_NO_DATA;
}

/// Decodes WAV sample payload (16/24/32-bit PCM or 32/64-bit IEEE float) into normalized floats in @nogc D.
void d_decode_wav_samples(
    const(ubyte)* data_ptr,
    size_t avail_bytes,
    int audio_format,
    int bits_per_sample,
    int total_samples,
    float* out_floats
) @nogc nothrow pure {
    for (int i = 0; i < total_samples; ++i) {
        out_floats[i] = 0.0f;
    }
    if (audio_format == 1) {
        if (bits_per_sample == 16) {
            const(short)* pcm = cast(const(short)*)data_ptr;
            size_t n = avail_bytes / 2;
            if (n > cast(size_t)total_samples) n = cast(size_t)total_samples;
            for (size_t i = 0; i < n; ++i) {
                out_floats[i] = cast(float)pcm[i] / 32768.0f;
            }
        } else if (bits_per_sample == 24) {
            size_t n = avail_bytes / 3;
            if (n > cast(size_t)total_samples) n = cast(size_t)total_samples;
            for (size_t i = 0; i < n; ++i) {
                out_floats[i] = int24ToFloat(data_ptr + i * 3);
            }
        } else if (bits_per_sample == 32) {
            const(int)* pcm = cast(const(int)*)data_ptr;
            size_t n = avail_bytes / 4;
            if (n > cast(size_t)total_samples) n = cast(size_t)total_samples;
            for (size_t i = 0; i < n; ++i) {
                out_floats[i] = cast(float)pcm[i] / 2147483648.0f;
            }
        }
    } else if (audio_format == 3) {
        if (bits_per_sample == 32) {
            const(float)* src = cast(const(float)*)data_ptr;
            size_t n = avail_bytes / 4;
            if (n > cast(size_t)total_samples) n = cast(size_t)total_samples;
            for (size_t i = 0; i < n; ++i) {
                out_floats[i] = src[i];
            }
        } else if (bits_per_sample == 64) {
            const(double)* src = cast(const(double)*)data_ptr;
            size_t n = avail_bytes / 8;
            if (n > cast(size_t)total_samples) n = cast(size_t)total_samples;
            for (size_t i = 0; i < n; ++i) {
                out_floats[i] = cast(float)src[i];
            }
        }
    }
}

/// Encodes a 44-byte 16-bit PCM WAV header and converts float samples to 16-bit PCM in @nogc D.
void d_encode_wav_pcm16(
    const(float)* interleaved,
    int total_samples,
    int channels,
    int sample_rate,
    ubyte* out_header44,
    short* out_pcm16
) @nogc nothrow pure {
    void putLE16(size_t off, ushort v) {
        out_header44[off] = cast(ubyte)(v & 0xFF);
        out_header44[off + 1] = cast(ubyte)((v >> 8) & 0xFF);
    }
    void putLE32(size_t off, uint v) {
        out_header44[off] = cast(ubyte)(v & 0xFF);
        out_header44[off + 1] = cast(ubyte)((v >> 8) & 0xFF);
        out_header44[off + 2] = cast(ubyte)((v >> 16) & 0xFF);
        out_header44[off + 3] = cast(ubyte)((v >> 24) & 0xFF);
    }

    int bits_per_sample = 16;
    uint byte_rate = cast(uint)(sample_rate * channels * bits_per_sample / 8);
    ushort block_align = cast(ushort)(channels * bits_per_sample / 8);
    uint data_size = cast(uint)(total_samples * bits_per_sample / 8);
    uint chunk_size = 36 + data_size;

    out_header44[0 .. 4] = cast(const(ubyte)[])"RIFF";
    putLE32(4, chunk_size);
    out_header44[8 .. 12] = cast(const(ubyte)[])"WAVE";
    out_header44[12 .. 16] = cast(const(ubyte)[])"fmt ";
    putLE32(16, 16);
    putLE16(20, 1);
    putLE16(22, cast(ushort)channels);
    putLE32(24, cast(uint)sample_rate);
    putLE32(28, byte_rate);
    putLE16(32, block_align);
    putLE16(34, cast(ushort)bits_per_sample);
    out_header44[36 .. 40] = cast(const(ubyte)[])"data";
    putLE32(40, data_size);

    for (int i = 0; i < total_samples; ++i) {
        float f = clampVal(interleaved[i], -1.0f, 1.0f);
        out_pcm16[i] = cast(short)(f * 32767.0f);
    }
}

} // extern (C++, "hibiki")
