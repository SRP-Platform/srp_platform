/**
 * Copyright (c) 2024
 *
 * @file test_daemon_ipc.cc
 */
#include <gtest/gtest.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <chrono>  // NOLINT
#include <cstdio>
#include <string>
#include <thread>  // NOLINT

#include "ara/com/mavlink/daemon_ipc.h"

namespace ara {
namespace com {
namespace mavlink {
namespace daemon {
namespace {

class DaemonIpcTest : public ::testing::Test {
 protected:
  void SetUp() override {
    path_ = "/tmp/srp_mav_ipc_" + std::to_string(::getpid()) + ".sock";
    ASSERT_TRUE(server_.Listen(path_).HasValue());
  }
  MessageSocket Accept() {
    auto acc = server_.Accept(1000);
    EXPECT_TRUE(acc.HasValue());
    EXPECT_TRUE(acc.Value().has_value());
    return acc.Value().value_or(MessageSocket{});
  }
  std::string path_;
  MessageServer server_;
};

TEST_F(DaemonIpcTest, SecondServerOnALiveSocketFailsAndLeavesTheFirstWorking) {
  MessageServer second;
  const auto r = second.Listen(path_);
  ASSERT_FALSE(r.HasValue());
  EXPECT_NE(r.Error().SupportData().find("already running"), std::string::npos) << r.Error().SupportData();
  // the first server still accepts connections
  auto client = MessageSocket::Connect(path_);
  ASSERT_TRUE(client.HasValue());
  EXPECT_TRUE(Accept().IsOpen());
}

TEST_F(DaemonIpcTest, LiveDaemonDetectionDoesNotConnectToTheSocket) {
  MessageServer second;
  ASSERT_FALSE(second.Listen(path_).HasValue());
  // Detection by flock: no probe connection reached the first server.
  const auto none = server_.Accept(50);
  ASSERT_TRUE(none.HasValue());
  EXPECT_FALSE(none.Value().has_value());
}

TEST_F(DaemonIpcTest, StaleSocketFileOfADeadProcessIsReplaced) {
  server_.Close();
  // a socket file without a listening process (as after SIGKILL of the daemon)
  const int fd = ::socket(AF_UNIX, SOCK_SEQPACKET, 0);
  ASSERT_GE(fd, 0);
  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path_.c_str());
  ASSERT_EQ(::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
  ::close(fd);
  MessageServer fresh;
  EXPECT_TRUE(fresh.Listen(path_).HasValue());
}

TEST_F(DaemonIpcTest, ConnectSendReceive) {
  auto client = MessageSocket::Connect(path_);
  ASSERT_TRUE(client.HasValue());
  MessageSocket srv = Accept();
  ASSERT_TRUE(srv.IsOpen());

  ElementPayload ev;
  ev.handle = 3;
  ev.element_id = 0x8001;
  ev.payload = {1, 2, 3};
  MessageSocket c = client.Value();
  ASSERT_TRUE(c.Send(ev).HasValue());
  const auto r = srv.Receive(1000);
  ASSERT_EQ(r.status, RecvStatus::kMessage);
  EXPECT_EQ(std::get<ElementPayload>(*r.message).payload, ev.payload);

  // message boundaries: two packets = two Receive calls
  LinkStatus ls;
  ls.handle = 3;
  ls.link = "lora";
  ls.ready = true;
  ASSERT_TRUE(srv.Send(ls).HasValue());
  ASSERT_TRUE(srv.Send(Unregister{3}).HasValue());
  const auto a = c.Receive(1000);
  const auto b = c.Receive(1000);
  ASSERT_EQ(a.status, RecvStatus::kMessage);
  ASSERT_EQ(b.status, RecvStatus::kMessage);
  EXPECT_TRUE(std::holds_alternative<LinkStatus>(*a.message));
  EXPECT_TRUE(std::holds_alternative<Unregister>(*b.message));
}

TEST_F(DaemonIpcTest, ReceiveTimeoutThenClosedAfterPeerShutdown) {
  MessageSocket client = MessageSocket::Connect(path_).Value();
  MessageSocket srv = Accept();
  EXPECT_EQ(srv.Receive(20).status, RecvStatus::kTimeout);
  client.Shutdown();
  EXPECT_EQ(srv.Receive(1000).status, RecvStatus::kClosed);
  EXPECT_FALSE(srv.Send(Unregister{1}).HasValue());
}

TEST_F(DaemonIpcTest, PeerDestroyedIsSeenAsClosed) {
  MessageSocket srv;
  {
    MessageSocket client = MessageSocket::Connect(path_).Value();
    srv = Accept();
  }  // the last copy of the client is gone = close(fd) (e.g. an application crash)
  EXPECT_EQ(srv.Receive(1000).status, RecvStatus::kClosed);
}

TEST_F(DaemonIpcTest, ShutdownWakesBlockedReceive) {
  MessageSocket client = MessageSocket::Connect(path_).Value();
  MessageSocket srv = Accept();
  RecvStatus st = RecvStatus::kMessage;
  std::thread t{[&] { st = srv.Receive(10000).status; }};
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  srv.Shutdown();
  t.join();
  EXPECT_EQ(st, RecvStatus::kClosed);
}

TEST_F(DaemonIpcTest, GarbagePacketIsProtocolError) {
  const int fd = ::socket(AF_UNIX, SOCK_SEQPACKET, 0);
  ASSERT_GE(fd, 0);
  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path_.c_str());
  ASSERT_EQ(::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
  MessageSocket srv = Accept();
  const unsigned char junk[] = {0xEE, 1, 2, 3};
  ASSERT_EQ(::send(fd, junk, sizeof(junk), 0), 4);
  EXPECT_EQ(srv.Receive(1000).status, RecvStatus::kProtocolError);
  ::close(fd);
}

TEST_F(DaemonIpcTest, AcceptTimeoutAndMissingDaemon) {
  const auto none = server_.Accept(20);
  ASSERT_TRUE(none.HasValue());
  EXPECT_FALSE(none.Value().has_value());
  EXPECT_FALSE(MessageSocket::Connect("/tmp/srp_mav_no_such_socket.sock").HasValue());
  EXPECT_FALSE(MessageSocket::Connect("").HasValue());
}

TEST_F(DaemonIpcTest, SocketFileHasRequestedModeAndIsRemovedOnClose) {
  struct stat st {};
  ASSERT_EQ(::stat(path_.c_str(), &st), 0);
  EXPECT_EQ(st.st_mode & 0777, 0660U);
  server_.Close();
  EXPECT_NE(::stat(path_.c_str(), &st), 0);
}

}  // namespace
}  // namespace daemon
}  // namespace mavlink
}  // namespace com
}  // namespace ara
