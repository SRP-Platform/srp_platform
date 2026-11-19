/**
 * @file test_serial_port.cc
 * @brief Unit tests of SerialPort on a pseudo-terminal (no hardware).
 * @copyright Copyright (c) 2024
 */
#include <dirent.h>
#include <fcntl.h>
#include <gtest/gtest.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/file.h>
#include <termios.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>  // NOLINT
#include <memory>
#include <string>
#include <thread>  // NOLINT
#include <vector>

#include "core/serial/serial_port.h"

namespace srp {
namespace core {
namespace serial {
namespace {

/// Master pty; the slave path is opened by SerialPort.
class Pty {
 public:
  Pty() {
    master_ = ::posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (master_ >= 0) {
      ::grantpt(master_);
      ::unlockpt(master_);
      const char* n = ::ptsname(master_);
      if (n != nullptr) {
        slave_path_ = n;
      }
    }
  }
  ~Pty() {
    if (master_ >= 0) {
      ::close(master_);
    }
  }
  bool ok() const { return master_ >= 0 && !slave_path_.empty(); }
  const std::string& path() const { return slave_path_; }
  void Write(const std::vector<std::uint8_t>& v) {
    ASSERT_EQ(::write(master_, v.data(), v.size()),
              static_cast<ssize_t>(v.size()));
  }
  std::vector<std::uint8_t> Read(std::size_t want, int timeout_ms) {
    std::vector<std::uint8_t> out;
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (out.size() < want && std::chrono::steady_clock::now() < end) {
      pollfd p{master_, POLLIN, 0};
      if (::poll(&p, 1, 20) > 0) {
        std::uint8_t b[512];
        const ssize_t n = ::read(master_, b, sizeof(b));
        if (n > 0) {
          out.insert(out.end(), b, b + n);
        }
      }
    }
    return out;
  }

 private:
  int master_{-1};
  std::string slave_path_;
};

/// Waits (via Poll) until `want` bytes arrived or `timeout_ms` elapsed.
std::vector<std::uint8_t> ReadAll(SerialPort* p, std::size_t want, int timeout_ms) {
  std::vector<std::uint8_t> out;
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  std::uint8_t buf[64];
  while (out.size() < want && std::chrono::steady_clock::now() < end) {
    if (p->Poll(false, 50, {}).readable) {
      const auto r = p->TryRead(buf, sizeof(buf));
      if (r.status == IoStatus::kOk) {
        out.insert(out.end(), buf, buf + r.bytes);
      }
    }
  }
  return out;
}

TEST(SerialPort, OpenMissingDeviceReportsError) {
  SerialPort p;
  const auto r = p.Open("/dev/no_such_port", 57600);
  EXPECT_FALSE(r.ok);
  EXPECT_EQ(r.err, ENOENT);
  EXPECT_NE(r.message.find("/dev/no_such_port"), std::string::npos);
}

TEST(SerialPort, OpenRejectsUnsupportedBaudrate) {
  Pty pty;
  ASSERT_TRUE(pty.ok());
  SerialPort p;
  const auto r = p.Open(pty.path(), 12345);
  EXPECT_FALSE(r.ok);
  EXPECT_EQ(r.err, EINVAL);
}

TEST(SerialPort, OpenSetsRaw8N1) {
  Pty pty;
  ASSERT_TRUE(pty.ok());
  SerialPort p;
  ASSERT_TRUE(p.Open(pty.path(), 115200).ok);
  EXPECT_TRUE(p.IsOpen());
  // Raw mode: special bytes (0x03, 0x0D, 0x0A, ...) pass through unchanged.
  const std::vector<std::uint8_t> bytes{0x03, 0x0D, 0x0A, 0x11, 0x13, 0x00, 0xFF};
  pty.Write(bytes);
  EXPECT_EQ(ReadAll(&p, bytes.size(), 2000), bytes);
}

TEST(SerialPort, SecondOpenOfSameDeviceIsBusy) {
  Pty pty;
  ASSERT_TRUE(pty.ok());
  SerialPort a;
  ASSERT_TRUE(a.Open(pty.path(), 9600).ok);
  SerialPort b;
  const auto r = b.Open(pty.path(), 9600);
  EXPECT_FALSE(r.ok);
  EXPECT_TRUE(r.busy || r.err == EBUSY || r.err == EWOULDBLOCK) << r.message;
}

TEST(SerialPort, ReopenAfterCloseSucceeds) {
  Pty pty;
  ASSERT_TRUE(pty.ok());
  SerialPort p;
  ASSERT_TRUE(p.Open(pty.path(), 57600).ok);
  p.Close();
  EXPECT_FALSE(p.IsOpen());
  ASSERT_TRUE(p.Open(pty.path(), 57600).ok);
  EXPECT_TRUE(p.IsOpen());
}

TEST(SerialPort, TryReadWithoutDataReturnsImmediatelyAsTimeout) {
  Pty pty;
  ASSERT_TRUE(pty.ok());
  SerialPort p;
  ASSERT_TRUE(p.Open(pty.path(), 57600).ok);
  std::uint8_t b[8];
  const auto t0 = std::chrono::steady_clock::now();
  const auto r = p.TryRead(b, sizeof(b));
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - t0).count();
  EXPECT_EQ(r.status, IoStatus::kTimeout);
  EXPECT_EQ(r.bytes, 0U);
  EXPECT_LT(ms, 50);
}

TEST(SerialPort, TryReadWithEmptyBufferIsNotReportedAsClosed) {
  Pty pty;
  ASSERT_TRUE(pty.ok());
  SerialPort p;
  ASSERT_TRUE(p.Open(pty.path(), 57600).ok);
  std::uint8_t b[1];
  EXPECT_EQ(p.TryRead(b, 0).status, IoStatus::kOk);
}

TEST(SerialPort, OpeningADirectoryFailsWithEisdir) {
  SerialPort p;
  const auto r = p.Open("/tmp", 57600);
  EXPECT_FALSE(r.ok);
  EXPECT_EQ(r.err, EISDIR);
}

TEST(SerialPort, IoOnClosedPortReturnsClosed) {
  SerialPort p;
  std::uint8_t b[4] = {};
  EXPECT_EQ(p.TryRead(b, sizeof(b)).status, IoStatus::kClosed);
  EXPECT_EQ(p.TryWrite(b, sizeof(b)).status, IoStatus::kClosed);
  EXPECT_EQ(p.Poll(false, 10, {}).status, IoStatus::kClosed);
}

int CountOpenFds() {
  int n = 0;
  if (DIR* d = ::opendir("/proc/self/fd")) {
    while (::readdir(d) != nullptr) {
      ++n;
    }
    ::closedir(d);
  }
  return n;
}

TEST(SerialPort, OpenAfterCloseWorksWhilePollIsStillBlocked) {
  Pty pty;
  ASSERT_TRUE(pty.ok());
  SerialPort p;
  ASSERT_TRUE(p.Open(pty.path(), 57600).ok);
  std::atomic<bool> done{false};
  std::thread poller{[&] {
    std::ignore = p.Poll(false, 20000, {});
    done = true;
  }};
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  SerialPort other;  // another object: exclusivity must be released right after Close()
  p.Close();
  const auto r = other.Open(pty.path(), 57600);
  EXPECT_TRUE(r.ok) << r.message;
  poller.join();
  EXPECT_TRUE(done.load());
}

TEST(SerialPort, FailedOpenDoesNotReleaseOwnersExclusivity) {
  Pty pty;
  ASSERT_TRUE(pty.ok());
  SerialPort owner;
  ASSERT_TRUE(owner.Open(pty.path(), 57600).ok);
  {
    SerialPort intruder;
    EXPECT_FALSE(intruder.Open(pty.path(), 57600).ok);
  }  // the destructor of a failed Open() must not drop the owner's flock/TIOCNXCL
  SerialPort second_intruder;
  EXPECT_FALSE(second_intruder.Open(pty.path(), 57600).ok);
}

TEST(SerialPort, NoFileDescriptorLeakOverManyOpenCloseCycles) {
  Pty pty;
  ASSERT_TRUE(pty.ok());
  const int before = CountOpenFds();
  for (int i = 0; i < 200; ++i) {
    SerialPort p;
    ASSERT_TRUE(p.Open(pty.path(), 115200).ok) << i;
    std::uint8_t b[4];
    std::ignore = p.TryRead(b, sizeof(b));
    const std::uint8_t out[2] = {1, 2};
    std::ignore = p.TryWrite(out, sizeof(out));
    std::ignore = p.Poll(false, 0, {});
  }
  EXPECT_EQ(CountOpenFds(), before);
}

TEST(SerialPort, TryWriteReachesPeer) {
  Pty pty;
  ASSERT_TRUE(pty.ok());
  SerialPort p;
  ASSERT_TRUE(p.Open(pty.path(), 115200).ok);
  const std::vector<std::uint8_t> data{9, 8, 7, 6};
  const auto w = p.TryWrite(data.data(), data.size());
  EXPECT_EQ(w.status, IoStatus::kOk);
  EXPECT_EQ(w.bytes, data.size());
  EXPECT_EQ(pty.Read(data.size(), 1000), data);
}

TEST(SerialPort, TryWriteReturnsPartialThenWouldBlockWhenBufferIsFull) {
  Pty pty;
  ASSERT_TRUE(pty.ok());
  SerialPort p;
  ASSERT_TRUE(p.Open(pty.path(), 115200).ok);
  const std::vector<std::uint8_t> chunk(1024, 0x5A);
  std::size_t total = 0;
  bool would_block = false;
  for (int i = 0; i < 4096 && !would_block; ++i) {
    const auto r = p.TryWrite(chunk.data(), chunk.size());
    if (r.status == IoStatus::kOk) {
      EXPECT_GT(r.bytes, 0U);
      EXPECT_LE(r.bytes, chunk.size());
      total += r.bytes;
    } else {
      EXPECT_EQ(r.status, IoStatus::kTimeout);
      EXPECT_EQ(r.bytes, 0U);
      would_block = true;
    }
  }
  EXPECT_TRUE(would_block);
  // once the peer reads, there is room again
  const auto got = pty.Read(total, 3000);
  EXPECT_EQ(got.size(), total);
  EXPECT_EQ(p.TryWrite(chunk.data(), 16).status, IoStatus::kOk);
}

TEST(SerialPort, PollReportsReadableWritableKickTimeoutAndClose) {
  Pty pty;
  ASSERT_TRUE(pty.ok());
  SerialPort p;
  ASSERT_TRUE(p.Open(pty.path(), 115200).ok);
  // nothing to read: timeout
  auto r = p.Poll(false, 20, {});
  EXPECT_EQ(r.status, IoStatus::kTimeout);
  EXPECT_FALSE(r.readable);
  EXPECT_FALSE(r.writable);
  // room to write only on request
  r = p.Poll(true, 100, {});
  EXPECT_EQ(r.status, IoStatus::kOk);
  EXPECT_TRUE(r.writable);
  // data from the peer
  pty.Write({1, 2, 3});
  r = p.Poll(false, 1000, {});
  EXPECT_TRUE(r.readable);
  std::uint8_t b[8];
  EXPECT_EQ(p.TryRead(b, sizeof(b)).bytes, 3U);
  // Kick wakes a waiting Poll and is cleared
  std::thread t{[&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    p.Kick();
  }};
  r = p.Poll(false, 5000, {});
  t.join();
  EXPECT_TRUE(r.kicked);
  EXPECT_EQ(p.Poll(false, 20, {}).status, IoStatus::kTimeout);
  // stop_token and Close()
  std::stop_source src;
  std::thread t2{[&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    src.request_stop();
  }};
  EXPECT_EQ(p.Poll(false, 5000, src.get_token()).status, IoStatus::kClosed);
  t2.join();
  std::thread t3{[&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    p.Close();
  }};
  EXPECT_EQ(p.Poll(false, 5000, {}).status, IoStatus::kClosed);
  t3.join();
}

TEST(SerialPort, PollReportsDeviceLossAsError) {
  auto pty = std::make_unique<Pty>();
  ASSERT_TRUE(pty->ok());
  SerialPort p;
  ASSERT_TRUE(p.Open(pty->path(), 115200).ok);
  pty.reset();  // master closed = device vanished
  EXPECT_EQ(p.Poll(false, 1000, {}).status, IoStatus::kError);
  EXPECT_EQ(p.Poll(true, 1000, {}).status, IoStatus::kError);
}

}  // namespace
}  // namespace serial
}  // namespace core
}  // namespace srp
