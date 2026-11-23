module engine.audio.ring_buffer;

import core.atomic : atomicLoad, atomicStore, MemoryOrder;
import core.stdc.stdlib : calloc, free;

/// Lock-free Single-Producer Single-Consumer (SPSC) audio ring buffer (@nogc nothrow).
/// Safe for real-time audio callbacks when one thread writes and one thread reads.
struct AudioRingBuffer {
    private float* buffer_ = null;
    private size_t capacity_ = 0;
    private shared size_t readPtr_ = 0;
    private shared size_t writePtr_ = 0;

    /// Allocates backing storage for `capacity` samples (minimum 2048).
    bool initialize(size_t capacity) @nogc nothrow @trusted {
        destroy();
        if (capacity < 2048) {
            capacity = 2048;
        }
        buffer_ = cast(float*)calloc(capacity, float.sizeof);
        if (buffer_ is null) {
            return false;
        }
        capacity_ = capacity;
        atomicStore!(MemoryOrder.rel)(readPtr_, 0);
        atomicStore!(MemoryOrder.rel)(writePtr_, 0);
        return true;
    }

    /// Frees the backing storage.
    void destroy() @nogc nothrow @trusted {
        if (buffer_ !is null) {
            free(buffer_);
            buffer_ = null;
        }
        capacity_ = 0;
        atomicStore!(MemoryOrder.rel)(readPtr_, 0);
        atomicStore!(MemoryOrder.rel)(writePtr_, 0);
    }

    /// Returns the total capacity in samples.
    size_t capacity() const @nogc nothrow pure @safe {
        return capacity_;
    }

    /// Returns the number of samples available to read.
    size_t availableRead() const @nogc nothrow @trusted {
        if (capacity_ == 0) return 0;
        const size_t r = atomicLoad!(MemoryOrder.acq)(readPtr_);
        const size_t w = atomicLoad!(MemoryOrder.acq)(writePtr_);
        return (w >= r) ? (w - r) : (capacity_ - r + w);
    }

    /// Returns the number of samples that can be written without overflowing.
    size_t availableWrite() const @nogc nothrow @trusted {
        if (capacity_ == 0) return 0;
        return capacity_ - availableRead() - 1;
    }

    /// Writes up to `data.length` samples into the ring buffer. Returns samples written.
    size_t write(const(float)[] data) @nogc nothrow @trusted {
        if (buffer_ is null || capacity_ == 0 || data.length == 0) return 0;
        const size_t avail = availableWrite();
        const size_t count = data.length < avail ? data.length : avail;
        size_t w = atomicLoad!(MemoryOrder.raw)(writePtr_);
        foreach (i; 0 .. count) {
            buffer_[w] = data[i];
            w = (w + 1 == capacity_) ? 0 : (w + 1);
        }
        atomicStore!(MemoryOrder.rel)(writePtr_, w);
        return count;
    }

    /// Reads up to `outData.length` samples from the ring buffer. Returns samples read.
    size_t read(float[] outData) @nogc nothrow @trusted {
        if (buffer_ is null || capacity_ == 0 || outData.length == 0) return 0;
        const size_t avail = availableRead();
        const size_t count = outData.length < avail ? outData.length : avail;
        size_t r = atomicLoad!(MemoryOrder.raw)(readPtr_);
        foreach (i; 0 .. count) {
            outData[i] = buffer_[r];
            r = (r + 1 == capacity_) ? 0 : (r + 1);
        }
        atomicStore!(MemoryOrder.rel)(readPtr_, r);
        return count;
    }

    /// Reads `numFrames` interleaved stereo frames into separate `outL` and `outR` buffers.
    /// Zero-fills `outL` / `outR` on underflow and returns false.
    bool readDeinterleavedStereo(float[] outL, float[] outR) @nogc nothrow @trusted {
        const size_t frames = outL.length < outR.length ? outL.length : outR.length;
        const size_t needed = frames * 2;
        if (buffer_ is null || availableRead() < needed) {
            outL[0 .. frames] = 0.0f;
            outR[0 .. frames] = 0.0f;
            return false;
        }
        size_t r = atomicLoad!(MemoryOrder.raw)(readPtr_);
        foreach (i; 0 .. frames) {
            outL[i] = buffer_[r];
            r = (r + 1 == capacity_) ? 0 : (r + 1);
            outR[i] = buffer_[r];
            r = (r + 1 == capacity_) ? 0 : (r + 1);
        }
        atomicStore!(MemoryOrder.rel)(readPtr_, r);
        return true;
    }
}

extern (C) {
    /// Creates a heap-allocated `AudioRingBuffer` for C++ backends.
    AudioRingBuffer* d_ring_buffer_create(size_t capacity) @nogc nothrow {
        auto rb = cast(AudioRingBuffer*)calloc(1, AudioRingBuffer.sizeof);
        if (rb is null) return null;
        if (!rb.initialize(capacity)) {
            free(rb);
            return null;
        }
        return rb;
    }

    /// Destroys a `AudioRingBuffer` created by `d_ring_buffer_create`.
    void d_ring_buffer_destroy(AudioRingBuffer* rb) @nogc nothrow {
        if (rb !is null) {
            rb.destroy();
            free(rb);
        }
    }

    size_t d_ring_buffer_available_read(const(AudioRingBuffer)* rb) @nogc nothrow {
        return rb ? rb.availableRead() : 0;
    }

    size_t d_ring_buffer_available_write(const(AudioRingBuffer)* rb) @nogc nothrow {
        return rb ? rb.availableWrite() : 0;
    }

    size_t d_ring_buffer_write(AudioRingBuffer* rb, const(float)* data, size_t count) @nogc nothrow {
        if (!rb || !data || count == 0) return 0;
        return rb.write(data[0 .. count]);
    }

    size_t d_ring_buffer_read(AudioRingBuffer* rb, float* outData, size_t count) @nogc nothrow {
        if (!rb || !outData || count == 0) return 0;
        return rb.read(outData[0 .. count]);
    }

    bool d_ring_buffer_read_stereo(AudioRingBuffer* rb, float* outL, float* outR, size_t numFrames) @nogc nothrow {
        if (!rb || !outL || !outR || numFrames == 0) return false;
        return rb.readDeinterleavedStereo(outL[0 .. numFrames], outR[0 .. numFrames]);
    }
}

unittest {
    AudioRingBuffer rb;
    assert(rb.initialize(2048));
    scope (exit) rb.destroy();

    assert(rb.capacity() == 2048);
    assert(rb.availableRead() == 0);
    assert(rb.availableWrite() == 2047);

    float[1024] src;
    foreach (i; 0 .. 1024) {
        src[i] = cast(float)i;
    }
    assert(rb.write(src[]) == 1024);
    assert(rb.availableRead() == 1024);

    float[512] dst;
    assert(rb.read(dst[]) == 512);
    assert(dst[0] == 0.0f && dst[511] == 511.0f);

    // Force wrap-around past 2048 boundary
    assert(rb.write(src[]) == 1024);
    assert(rb.availableRead() == 1536);

    float[512] outL, outR;
    assert(rb.readDeinterleavedStereo(outL[], outR[]));
    assert(outL[0] == 512.0f && outR[0] == 513.0f);
    assert(outL[511] == 510.0f && outR[511] == 511.0f);

    // Underflow zero-fills outputs
    assert(!rb.readDeinterleavedStereo(outL[], outR[]));
    assert(outL[0] == 0.0f && outR[0] == 0.0f);
}
