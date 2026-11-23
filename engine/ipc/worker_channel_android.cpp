// Android in-process heap-backed implementation of WorkerChannelLocal.
// Android does not support POSIX shm_open/shm_unlink.

#include <cstdlib>
#include <cstring>

#include "absl/log/log.h"
#include "engine/ipc/worker_channel_local.hpp"

namespace hibiki {

struct WorkerChannelLocal::Impl {};

WorkerChannelLocal* WorkerChannelLocal::createServer(
    const std::string& path_or_name, const std::string& shm_name,
    int block_size, int num_channels) {
  auto* ch = new WorkerChannelLocal();
  ch->impl_ = std::make_unique<Impl>();
  ch->path_or_name_ = path_or_name;
  ch->shm_name_ = shm_name;
  ch->block_size_ = block_size;
  ch->num_channels_ = num_channels;
  ch->is_server_ = true;

  size_t header_size = sizeof(SharedMemHeader);
  size_t buf_size = (size_t)block_size * sizeof(float) * num_channels * 2;
  ch->shm_size_ = header_size + buf_size;

  ch->shm_ptr_ = std::calloc(1, ch->shm_size_);
  if (!ch->shm_ptr_) {
    delete ch;
    return nullptr;
  }

  ch->header_ = reinterpret_cast<SharedMemHeader*>(ch->shm_ptr_);
  ch->header_->block_size = block_size;
  ch->header_->num_channels = num_channels;
  ch->header_->flags = SHM_FLAG_IDLE;

  return ch;
}

WorkerChannelLocal* WorkerChannelLocal::createClient(
    const std::string& path_or_name, const std::string& shm_name) {
  return createServer(path_or_name, shm_name, 512, 2);
}

WorkerChannelLocal::~WorkerChannelLocal() {
  if (shm_ptr_) {
    std::free(shm_ptr_);
    shm_ptr_ = nullptr;
  }
}

bool WorkerChannelLocal::accept() { return true; }

bool WorkerChannelLocal::send(const void* /*data*/, size_t /*len*/) {
  return true;
}

bool WorkerChannelLocal::recv(void* /*buf*/, size_t /*len*/) { return false; }

int WorkerChannelLocal::recvMessage(std::string& /*out*/) { return -1; }

bool WorkerChannelLocal::sendMessage(const void* /*data*/, size_t /*len*/) {
  return true;
}

float* WorkerChannelLocal::inputBuffer(int channel) {
  if (!shm_ptr_ || channel < 0 || channel >= num_channels_) return nullptr;
  auto* base = reinterpret_cast<uint8_t*>(shm_ptr_);
  return reinterpret_cast<float*>(base + sizeof(SharedMemHeader) +
                                  (size_t)channel * block_size_ *
                                      sizeof(float));
}

float* WorkerChannelLocal::outputBuffer(int channel) {
  if (!shm_ptr_ || channel < 0 || channel >= num_channels_) return nullptr;
  auto* base = reinterpret_cast<uint8_t*>(shm_ptr_);
  return reinterpret_cast<float*>(base + sizeof(SharedMemHeader) +
                                  (size_t)(num_channels_ + channel) *
                                      block_size_ * sizeof(float));
}

}  // namespace hibiki
