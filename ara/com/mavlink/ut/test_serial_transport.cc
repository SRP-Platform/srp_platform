/**
 * @file test_serial_transport.cc
 * @brief SerialTransport na pty: testy kontraktu IByteTransport + klasyfikacja błędów.
 * @copyright Copyright (c) 2024
 */
#include <fcntl.h>
#include <gtest/gtest.h>
#include <poll.h>
#include <stdlib.h>
#include <unistd.h>

#include <chrono>  // NOLINT
#include <memory>
#include <string>
#include <vector>

#include "ara/com/mavlink/serial_transport.h"
#include "ara/com/mavlink/testing/byte_transport_contract_test.h"
#include "ara/com/mavlink/transport_error.h"

namespace ara {
namespace com {
namespace mavlink {
namespace {
using testing::ByteTransportContractTest;

/// Wspólny stan pty: master zamykany razem z harnessem.
struct PtyMaster {
  PtyMaster() {
    fd = ::posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd >= 0) {
      ::grantpt(fd);
      ::unlockpt(fd);
      const char* n = ::ptsname(fd);
      path = n != nullptr ? n : "";
    }
  }
  ~PtyMaster() {
    if (fd >= 0) {
      ::close(fd);
    }
  }
  int fd{-1};
  std::string path;
};

testing::ByteTransportHarness MakeSerialHarness() {
  auto pty = std::make_shared<PtyMaster>();
  testing::ByteTransportHarness h;
  h.transport = std::make_shared<SerialTransport>("pty", pty->path, 115200);
  h.peer_write = [pty](const std::vector<std::uint8_t>& v) {
    std::size_t done = 0;
    while (done < v.size()) {
      const ssize_t n = ::write(pty->fd, v.data() + done, v.size() - done);
      if (n > 0) {
        done += static_cast<std::size_t>(n);
      } else {
        pollfd p{pty->fd, POLLOUT, 0};
        ::poll(&p, 1, 20);
      }
    }
  };
  h.peer_read = [pty](std::size_t want, std::uint32_t timeout_ms) {
    std::vector<std::uint8_t> out;
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (out.size() < want && std::chrono::steady_clock::now() < end) {
      pollfd p{pty->fd, POLLIN, 0};
      if (::poll(&p, 1, 20) > 0) {
        std::uint8_t b[4096];
        const ssize_t n = ::read(pty->fd, b, std::min<std::size_t>(sizeof(b), want - out.size()));
        if (n > 0) {
          out.insert(out.end(), b, b + n);
        }
      }
    }
    return out;
  };
  h.overflow_bytes = 4 * 1024 * 1024;
  return h;
}

INSTANTIATE_TEST_SUITE_P(
    SerialPty, ByteTransportContractTest,
    ::testing::Values(testing::ContractParam{"serial_pty", MakeSerialHarness}),
    testing::ContractParamName);

TEST(SerialTransport, NameContainsLinkName) {
  SerialTransport t{"radio1", "/dev/x", 9600};
  EXPECT_EQ(t.Name(), "serial:radio1");
}

TEST(SerialTransport, MissingDeviceIsPersistentOpenError) {
  SerialTransport t{"l", "/dev/nie_ma_takiego", 57600};
  const auto r = t.Open({});
  ASSERT_FALSE(r.HasValue());
  EXPECT_TRUE(IsTransportError(r.Error(), TransportErrc::kPersistent));
  EXPECT_NE(r.Error().SupportData().find("/dev/nie_ma_takiego"), std::string::npos);
}

TEST(SerialTransport, DirectoryAsDeviceIsPersistent) {
  SerialTransport t{"l", "/tmp", 57600};
  const auto r = t.Open({});
  ASSERT_FALSE(r.HasValue());
  EXPECT_TRUE(IsTransportError(r.Error(), TransportErrc::kPersistent));
}

TEST(SerialTransport, ReadWithEmptyBufferReturnsZeroNotClosed) {
  PtyMaster pty;
  SerialTransport t{"l", pty.path, 57600};
  ASSERT_TRUE(t.Open({}).HasValue());
  std::uint8_t b[1];
  const auto r = t.Read(std::span<std::uint8_t>{b, 0}, 10, {});
  ASSERT_TRUE(r.HasValue());
  EXPECT_EQ(r.Value(), 0U);
}

TEST(SerialTransport, BusyPortIsPersistentWithClearMessage) {
  PtyMaster pty;
  ASSERT_GE(pty.fd, 0);
  SerialTransport a{"a", pty.path, 57600};
  ASSERT_TRUE(a.Open({}).HasValue());
  SerialTransport b{"b", pty.path, 57600};
  const auto r = b.Open({});
  ASSERT_FALSE(r.HasValue());
  EXPECT_TRUE(IsTransportError(r.Error(), TransportErrc::kPersistent));
}

TEST(SerialTransport, OpenWithStopRequestedIsClosedError) {
  PtyMaster pty;
  SerialTransport t{"l", pty.path, 57600};
  std::stop_source s;
  s.request_stop();
  const auto r = t.Open(s.get_token());
  ASSERT_FALSE(r.HasValue());
  EXPECT_TRUE(IsTransportError(r.Error(), TransportErrc::kClosed));
}

TEST(SerialTransport, UnsupportedBaudrateIsPersistent) {
  PtyMaster pty;
  SerialTransport t{"l", pty.path, 12345};
  const auto r = t.Open({});
  ASSERT_FALSE(r.HasValue());
  EXPECT_TRUE(IsTransportError(r.Error(), TransportErrc::kPersistent));
}

TEST(SerialTransport, DeviceLossIsPersistentReadError) {
  auto pty = std::make_unique<PtyMaster>();
  SerialTransport t{"l", pty->path, 57600};
  ASSERT_TRUE(t.Open({}).HasValue());
  pty.reset();
  std::uint8_t b[4];
  bool saw_error = false;
  for (int i = 0; i < 20 && !saw_error; ++i) {
    const auto r = t.Read(std::span<std::uint8_t>{b, sizeof(b)}, 100, {});
    if (!r.HasValue()) {
      saw_error = true;
      EXPECT_TRUE(IsTransportError(r.Error(), TransportErrc::kPersistent));
    }
  }
  EXPECT_TRUE(saw_error);
}

}  // namespace
}  // namespace mavlink
}  // namespace com
}  // namespace ara
