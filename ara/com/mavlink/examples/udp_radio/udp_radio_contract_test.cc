/**
 * Copyright (c) 2024
 *
 * @file udp_radio_contract_test.cc
 * @brief The IRadioDriver contract suite (radio_driver_contract_test.h) on `example_udp_radio`,
 * created through the global registry from a driver config file, as the daemon does it. The
 * "other side of the air" is a UDP socket of the test.
 *
 * overflow_bytes = 0: a loopback UDP socket is never full (a datagram is delivered or dropped at
 * once), so the kBusy -> OnTxReady tests are skipped for this driver. A deterministic kBusy needs
 * a send-buffer limit (SO_SNDBUF) that the driver owns, so it would take a test-only seam in an
 * example meant to be copied; not added.
 */
#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>  // NOLINT
#include <cstdint>
#include <cstdio>
#include <deque>
#include <fstream>
#include <memory>
#include <mutex>  // NOLINT
#include <span>  // NOLINT
#include <string>
#include <thread>  // NOLINT
#include <tuple>
#include <vector>

#include "ara/com/mavlink/examples/udp_radio/udp_radio.h"
#include "ara/com/mavlink/radio_driver_registry.h"
#include "ara/com/mavlink/testing/radio_driver_contract_test.h"

namespace ara {
namespace com {
namespace mavlink {
namespace examples {
namespace {
using testing::RadioDriverContractTest;

sockaddr_in Loopback(std::uint16_t port) {
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(port);
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  return a;
}

/// The peer: a UDP socket on an ephemeral port; keeps the unread rest of a datagram.
class UdpPeer {
 public:
  UdpPeer() {
    fd_ = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    sockaddr_in a = Loopback(0);
    ::bind(fd_, reinterpret_cast<sockaddr*>(&a), sizeof(a));
    socklen_t len = sizeof(a);
    ::getsockname(fd_, reinterpret_cast<sockaddr*>(&a), &len);
    port_ = ntohs(a.sin_port);
  }
  ~UdpPeer() { ::close(fd_); }
  UdpPeer(const UdpPeer&) = delete;
  UdpPeer& operator=(const UdpPeer&) = delete;

  std::uint16_t port() const { return port_; }

  void Write(std::uint16_t to_port, const std::vector<std::uint8_t>& bytes) {
    const sockaddr_in to = Loopback(to_port);
    ::sendto(fd_, bytes.data(), bytes.size(), 0, reinterpret_cast<const sockaddr*>(&to), sizeof(to));
  }

  std::vector<std::uint8_t> Read(std::size_t want, std::uint32_t timeout_ms) {
    if (pending_.empty()) {
      pollfd p{fd_, POLLIN, 0};
      if (::poll(&p, 1, static_cast<int>(timeout_ms)) > 0) {
        std::uint8_t buf[2048];
        const auto n = ::recv(fd_, buf, sizeof(buf), MSG_DONTWAIT);
        if (n > 0) {
          pending_.insert(pending_.end(), buf, buf + n);
        }
      }
    }
    std::vector<std::uint8_t> out;
    while (out.size() < want && !pending_.empty()) {
      out.push_back(pending_.front());
      pending_.pop_front();
    }
    return out;
  }

 private:
  int fd_{-1};
  std::uint16_t port_{0};
  std::deque<std::uint8_t> pending_;
};

/// Free UDP port (bound and released; good enough for a test).
std::uint16_t FreePort() {
  UdpPeer p;
  return p.port();
}

/// The config file lives as long as the driver.
struct ConfigKeeper {
  ~ConfigKeeper() {
    driver.reset();
    ::unlink(config.c_str());
  }
  std::shared_ptr<IRadioDriver> driver;
  std::string config;
};

testing::RadioDriverHarness MakeHarness() {
  static int counter = 0;
  auto peer = std::make_shared<UdpPeer>();
  auto keeper = std::make_shared<ConfigKeeper>();
  const std::uint16_t bind_port = FreePort();
  keeper->config = "/tmp/srp_udp_radio_" + std::to_string(::getpid()) + "_" + std::to_string(counter++) + ".json";
  std::ofstream(keeper->config) << "{\"bind_port\": " << bind_port << ", \"peer_host\": \"127.0.0.1\", \"peer_port\": "
                                << peer->port() << "}";
  testing::RadioDriverHarness h;
  const auto created = RadioDriverRegistry::Global().Create(kUdpRadioId, keeper->config);
  if (!created.HasValue()) {
    ADD_FAILURE() << created.Error().SupportData();
    return h;
  }
  keeper->driver = created.Value();
  h.driver = std::shared_ptr<IRadioDriver>(keeper, keeper->driver.get());  // the keeper lives with the driver
  h.peer_write = [peer, bind_port](const std::vector<std::uint8_t>& v) { peer->Write(bind_port, v); };
  h.peer_read = [peer](std::size_t want, std::uint32_t timeout_ms) { return peer->Read(want, timeout_ms); };
  h.overflow_bytes = 0;
  return h;
}

INSTANTIATE_TEST_SUITE_P(ExampleUdpRadio, RadioDriverContractTest,
                         ::testing::Values(testing::RadioContractParam{"example_udp_radio", MakeHarness}),
                         testing::RadioContractParamName);

TEST(ExampleUdpRadio, MaxFrameSizeIs280) {
  UdpRadio r{14560, "127.0.0.1", 14550};
  EXPECT_EQ(static_cast<const IRadioDriver&>(r).MaxFrameSize(), 280U);
}

TEST(ExampleUdpRadio, BadConfigIsPersistentStartError) {
  const auto created = RadioDriverRegistry::Global().Create(kUdpRadioId, "/nonexistent/udp_radio.json");
  ASSERT_TRUE(created.HasValue());  // the error surfaces from Start()
  struct Null final : IRadioListener {
    void OnReceive(std::span<const std::uint8_t>) noexcept override {}
    void OnTxReady() noexcept override {}
    void OnError(ara::core::ErrorCode, bool) noexcept override {}
  } l;
  const auto r = created.Value()->Start(l, {});
  ASSERT_FALSE(r.HasValue());
  EXPECT_TRUE(IsTransportError(r.Error(), TransportErrc::kPersistent));
}

TEST(ExampleUdpRadio, BusyBindPortIsPersistentStartError) {
  struct Null final : IRadioListener {
    void OnReceive(std::span<const std::uint8_t>) noexcept override {}
    void OnTxReady() noexcept override {}
    void OnError(ara::core::ErrorCode, bool) noexcept override {}
  } l;
  const std::uint16_t port = FreePort();
  UdpRadio first{port, "127.0.0.1", FreePort()};
  ASSERT_TRUE(first.Start(l, {}).HasValue());
  UdpRadio second{port, "127.0.0.1", FreePort()};
  const auto r = second.Start(l, {});
  ASSERT_FALSE(r.HasValue());
  EXPECT_TRUE(IsTransportError(r.Error(), TransportErrc::kPersistent));
  first.Stop();
}

TEST(ExampleUdpRadio, UnknownConfigKeyIsPersistentStartError) {
  const std::string path = "/tmp/srp_udp_radio_typo_" + std::to_string(::getpid()) + ".json";
  std::ofstream(path) << "{\"bind_port\": " << FreePort() << ", \"peer_hots\": \"127.0.0.1\", \"peer_port\": "
                      << FreePort() << "}";
  const auto created = RadioDriverRegistry::Global().Create(kUdpRadioId, path);
  ASSERT_TRUE(created.HasValue());
  struct Null final : IRadioListener {
    void OnReceive(std::span<const std::uint8_t>) noexcept override {}
    void OnTxReady() noexcept override {}
    void OnError(ara::core::ErrorCode, bool) noexcept override {}
  } l;
  const auto r = created.Value()->Start(l, {});
  ::unlink(path.c_str());
  ASSERT_FALSE(r.HasValue());
  EXPECT_TRUE(IsTransportError(r.Error(), TransportErrc::kPersistent));
  EXPECT_NE(r.Error().SupportData().find("peer_hots"), std::string::npos) << r.Error().SupportData();
}

// A peer on a closed port (ICMP port unreachable): Send() keeps working, the driver reports it once.
TEST(ExampleUdpRadio, UnreachablePeerIsReportedOncePerPeriod) {
  struct Recorder final : IRadioListener {
    void OnReceive(std::span<const std::uint8_t>) noexcept override {}
    void OnTxReady() noexcept override {}
    void OnError(ara::core::ErrorCode e, bool fatal) noexcept override {
      std::lock_guard<std::mutex> lock{mutex};
      reports.push_back(e.SupportData());
      fatals += fatal ? 1 : 0;
    }
    std::mutex mutex;
    std::vector<std::string> reports;
    int fatals{0};
  } l;
  UdpRadio r{FreePort(), "127.0.0.1", FreePort()};  // nobody listens on peer_port
  ASSERT_TRUE(r.Start(l, {}).HasValue());
  const std::vector<std::uint8_t> frame(20, 1);
  for (int i = 0; i < 30; ++i) {
    std::ignore = r.Send(frame);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  r.Stop();
  std::lock_guard<std::mutex> lock{l.mutex};
  ASSERT_EQ(l.reports.size(), 1U) << "one line per period, not one per frame";
  EXPECT_NE(l.reports[0].find("unreachable"), std::string::npos) << l.reports[0];
  EXPECT_EQ(l.fatals, 0);
}

TEST(ExampleUdpRadio, OversizedFrameIsRejected) {
  UdpRadio r{FreePort(), "127.0.0.1", FreePort()};
  struct Null final : IRadioListener {
    void OnReceive(std::span<const std::uint8_t>) noexcept override {}
    void OnTxReady() noexcept override {}
    void OnError(ara::core::ErrorCode, bool) noexcept override {}
  } l;
  ASSERT_TRUE(r.Start(l, {}).HasValue());
  const std::vector<std::uint8_t> big(281, 0);
  const auto res = r.Send(big);
  ASSERT_FALSE(res.HasValue());
  EXPECT_TRUE(IsTransportError(res.Error(), TransportErrc::kInvalidArgument));
  r.Stop();
}

}  // namespace
}  // namespace examples
}  // namespace mavlink
}  // namespace com
}  // namespace ara
