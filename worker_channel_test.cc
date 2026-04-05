#include <gtest/gtest.h>
#include <sys/socket.h>

#include <cstring>
#include <string>
#include <thread>

#include "worker_channel_local.hpp"


namespace hibiki {

class WorkerChannelTest : public ::testing::Test {
 protected:
  std::string socket_path_;
  std::string shm_name_;

  void SetUp() override {
    // Use test-unique names
    auto tid = std::to_string(getpid());
    socket_path_ = "/tmp/hbk-test-" + tid + ".sock";
    shm_name_ = "/hbk-test-" + tid;
  }

  void TearDown() override {
    // Cleanup is handled by destructors, but be safe
    unlink(socket_path_.c_str());
  }
};

TEST_F(WorkerChannelTest, ServerClientConnect) {
  auto* server =
      WorkerChannelLocal::createServer(socket_path_, shm_name_, 512, 2);
  ASSERT_NE(server, nullptr);

  // Connect client in a thread
  WorkerChannelLocal* client = nullptr;
  std::thread t([&]() {
    client = WorkerChannelLocal::createClient(socket_path_, shm_name_);
  });

  ASSERT_TRUE(server->accept());
  t.join();
  ASSERT_NE(client, nullptr);

  delete client;
  delete server;
}

TEST_F(WorkerChannelTest, SendRecvMessage) {
  auto* server =
      WorkerChannelLocal::createServer(socket_path_, shm_name_, 512, 2);
  ASSERT_NE(server, nullptr);

  WorkerChannelLocal* client = nullptr;
  std::thread t([&]() {
    client = WorkerChannelLocal::createClient(socket_path_, shm_name_);
  });
  ASSERT_TRUE(server->accept());
  t.join();
  ASSERT_NE(client, nullptr);

  // Server → Client
  std::string msg = "hello worker";
  ASSERT_TRUE(server->sendMessage(msg.data(), msg.size()));

  std::string received;
  ASSERT_GT(client->recvMessage(received), 0);
  EXPECT_EQ(received, msg);

  // Client → Server
  std::string reply = "ack from worker";
  ASSERT_TRUE(client->sendMessage(reply.data(), reply.size()));

  std::string got;
  ASSERT_GT(server->recvMessage(got), 0);
  EXPECT_EQ(got, reply);

  delete client;
  delete server;
}

TEST_F(WorkerChannelTest, SharedMemoryBuffers) {
  auto* server =
      WorkerChannelLocal::createServer(socket_path_, shm_name_, 256, 2);
  ASSERT_NE(server, nullptr);

  WorkerChannelLocal* client = nullptr;
  std::thread t([&]() {
    client = WorkerChannelLocal::createClient(socket_path_, shm_name_);
  });
  ASSERT_TRUE(server->accept());
  t.join();
  ASSERT_NE(client, nullptr);

  // Verify header
  EXPECT_EQ(server->header()->block_size, 256);
  EXPECT_EQ(server->header()->num_channels, 2);
  EXPECT_EQ(client->blockSize(), 256);

  // Write input audio on server side, read on client side
  float* server_inL = server->inputBuffer(0);
  float* server_inR = server->inputBuffer(1);
  ASSERT_NE(server_inL, nullptr);
  ASSERT_NE(server_inR, nullptr);

  for (int i = 0; i < 256; ++i) {
    server_inL[i] = (float)i / 256.0f;
    server_inR[i] = -(float)i / 256.0f;
  }

  // Client sees the same data (shared memory)
  float* client_inL = client->inputBuffer(0);
  float* client_inR = client->inputBuffer(1);
  ASSERT_NE(client_inL, nullptr);
  ASSERT_NE(client_inR, nullptr);

  for (int i = 0; i < 256; ++i) {
    EXPECT_FLOAT_EQ(client_inL[i], (float)i / 256.0f);
    EXPECT_FLOAT_EQ(client_inR[i], -(float)i / 256.0f);
  }

  // Write output audio on client side, read on server side
  float* client_outL = client->outputBuffer(0);
  float* client_outR = client->outputBuffer(1);
  ASSERT_NE(client_outL, nullptr);
  ASSERT_NE(client_outR, nullptr);

  for (int i = 0; i < 256; ++i) {
    client_outL[i] = 1.0f;
    client_outR[i] = -1.0f;
  }

  float* server_outL = server->outputBuffer(0);
  float* server_outR = server->outputBuffer(1);
  for (int i = 0; i < 256; ++i) {
    EXPECT_FLOAT_EQ(server_outL[i], 1.0f);
    EXPECT_FLOAT_EQ(server_outR[i], -1.0f);
  }

  delete client;
  delete server;
}

TEST_F(WorkerChannelTest, InvalidChannel) {
  auto* server =
      WorkerChannelLocal::createServer(socket_path_, shm_name_, 512, 2);
  ASSERT_NE(server, nullptr);
  EXPECT_EQ(server->inputBuffer(-1), nullptr);
  EXPECT_EQ(server->inputBuffer(5), nullptr);
  EXPECT_EQ(server->outputBuffer(-1), nullptr);
  EXPECT_EQ(server->outputBuffer(5), nullptr);
  delete server;
}

}  // namespace hibiki
