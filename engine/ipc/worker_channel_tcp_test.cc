#include "engine/ipc/worker_channel_tcp.hpp"

#include <gtest/gtest.h>

#include <string>
#include <thread>

namespace hibiki {

class WorkerChannelTcpTest : public ::testing::Test {
 protected:
  int test_port_ = 0;  // Will use a dynamic port

  void SetUp() override {
    // Use a port in the ephemeral range
    test_port_ = 19100 + (getpid() % 1000);
  }
};

TEST_F(WorkerChannelTcpTest, ServerClientConnect) {
  auto* server = WorkerChannelTcp::createServer(test_port_);
  ASSERT_NE(server, nullptr);

  WorkerChannelTcp* client = nullptr;
  std::thread t([&]() {
    client = WorkerChannelTcp::createClient("127.0.0.1", test_port_, 512, 2);
  });

  ASSERT_TRUE(server->accept());
  t.join();
  ASSERT_NE(client, nullptr);

  delete client;
  delete server;
}

TEST_F(WorkerChannelTcpTest, SendRecvMessage) {
  auto* server = WorkerChannelTcp::createServer(test_port_);
  ASSERT_NE(server, nullptr);

  WorkerChannelTcp* client = nullptr;
  std::thread t([&]() {
    client = WorkerChannelTcp::createClient("127.0.0.1", test_port_, 512, 2);
  });
  ASSERT_TRUE(server->accept());
  t.join();
  ASSERT_NE(client, nullptr);

  // Client → Server
  std::string msg = "hello remote worker";
  ASSERT_TRUE(client->sendMessage(msg.data(), msg.size()));

  std::string received;
  ASSERT_GT(server->recvMessage(received), 0);
  EXPECT_EQ(received, msg);

  // Server → Client
  std::string reply = "ack from daemon";
  ASSERT_TRUE(server->sendMessage(reply.data(), reply.size()));

  std::string got;
  ASSERT_GT(client->recvMessage(got), 0);
  EXPECT_EQ(got, reply);

  delete client;
  delete server;
}

TEST_F(WorkerChannelTcpTest, HeapAudioBuffers) {
  auto* client = WorkerChannelTcp::createClient("127.0.0.1", 1, 256, 2);
  // Connection will fail, but we can still test buffer allocation
  // from a standalone instance. Let's test via a connected pair instead.

  auto* server = WorkerChannelTcp::createServer(test_port_);
  ASSERT_NE(server, nullptr);

  WorkerChannelTcp* c = nullptr;
  std::thread t([&]() {
    c = WorkerChannelTcp::createClient("127.0.0.1", test_port_, 256, 2);
  });
  ASSERT_TRUE(server->accept());
  t.join();
  ASSERT_NE(c, nullptr);

  // Client side has heap buffers
  float* inL = c->inputBuffer(0);
  float* inR = c->inputBuffer(1);
  ASSERT_NE(inL, nullptr);
  ASSERT_NE(inR, nullptr);

  float* outL = c->outputBuffer(0);
  float* outR = c->outputBuffer(1);
  ASSERT_NE(outL, nullptr);
  ASSERT_NE(outR, nullptr);

  // Write and verify
  for (int i = 0; i < 256; ++i) {
    inL[i] = (float)i / 256.0f;
    outR[i] = -1.0f;
  }
  EXPECT_FLOAT_EQ(inL[128], 128.0f / 256.0f);
  EXPECT_FLOAT_EQ(outR[0], -1.0f);

  // Invalid channels
  EXPECT_EQ(c->inputBuffer(-1), nullptr);
  EXPECT_EQ(c->inputBuffer(5), nullptr);

  delete c;
  delete server;
}

}  // namespace hibiki
