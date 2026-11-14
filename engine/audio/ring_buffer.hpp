#pragma once

#include <cstddef>

namespace hibiki {

struct AudioRingBuffer;

extern "C" {
AudioRingBuffer* d_ring_buffer_create(size_t capacity);
void d_ring_buffer_destroy(AudioRingBuffer* rb);
size_t d_ring_buffer_available_read(const AudioRingBuffer* rb);
size_t d_ring_buffer_available_write(const AudioRingBuffer* rb);
size_t d_ring_buffer_write(AudioRingBuffer* rb, const float* data,
                           size_t count);
size_t d_ring_buffer_read(AudioRingBuffer* rb, float* out_data, size_t count);
bool d_ring_buffer_read_stereo(AudioRingBuffer* rb, float* out_l, float* out_r,
                               size_t num_frames);
}

/// RAII wrapper around the @nogc D SPSC audio ring buffer.
class AudioRingBufferHandle {
 public:
  explicit AudioRingBufferHandle(size_t capacity)
      : rb_(d_ring_buffer_create(capacity)) {}
  ~AudioRingBufferHandle() { d_ring_buffer_destroy(rb_); }

  AudioRingBufferHandle(const AudioRingBufferHandle&) = delete;
  AudioRingBufferHandle& operator=(const AudioRingBufferHandle&) = delete;

  size_t availableRead() const { return d_ring_buffer_available_read(rb_); }
  size_t availableWrite() const { return d_ring_buffer_available_write(rb_); }
  size_t write(const float* data, size_t count) {
    return d_ring_buffer_write(rb_, data, count);
  }
  size_t read(float* out_data, size_t count) {
    return d_ring_buffer_read(rb_, out_data, count);
  }
  bool readStereo(float* out_l, float* out_r, size_t num_frames) {
    return d_ring_buffer_read_stereo(rb_, out_l, out_r, num_frames);
  }

 private:
  AudioRingBuffer* rb_ = nullptr;
};

}  // namespace hibiki
