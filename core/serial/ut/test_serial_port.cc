/**
 * @file test_serial_port.cc
 * @brief UT SerialPort na pseudoterminalu (bez sprzętu).
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

/// Master pty; ścieżka slave do otwarcia przez SerialPort.
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

TEST(SerialPort, BaudrateSupport) {
  for (std::uint32_t b : {1200U, 2400U, 4800U, 9600U, 19200U, 38400U, 57600U,
                          115200U, 230400U, 460800U, 921600U}) {
    EXPECT_TRUE(SerialPort::IsBaudrateSupported(b)) << b;
  }
  EXPECT_FALSE(SerialPort::IsBaudrateSupported(12345));
  EXPECT_FALSE(SerialPort::IsBaudrateSupported(0));
}

TEST(SerialPort, OpenMissingDeviceReportsError) {
  SerialPort p;
  const auto r = p.Open("/dev/nie_ma_takiego_portu", 57600);
  EXPECT_FALSE(r.ok);
  EXPECT_EQ(r.err, ENOENT);
  EXPECT_NE(r.message.find("/dev/nie_ma_takiego_portu"), std::string::npos);
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
  // Surowy tryb: bajty specjalne (np. 0x03, 0x0D, 0x0A) przechodzą bez zmian.
  pty.Write({0x03, 0x0D, 0x0A, 0x11, 0x13, 0x00, 0xFF});
  std::uint8_t buf[16];
  std::vector<std::uint8_t> got;
  for (int i = 0; i < 20 && got.size() < 7; ++i) {
    const auto r = p.Read(buf, sizeof(buf), 100, {});
    ASSERT_NE(r.status, IoStatus::kError);
    got.insert(got.end(), buf, buf + r.bytes);
  }
  EXPECT_EQ(got, (std::vector<std::uint8_t>{0x03, 0x0D, 0x0A, 0x11, 0x13, 0x00, 0xFF}));
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

TEST(SerialPort, ReadTimeoutAndNoBlockingMode) {
  Pty pty;
  ASSERT_TRUE(pty.ok());
  SerialPort p;
  ASSERT_TRUE(p.Open(pty.path(), 57600).ok);
  std::uint8_t b[8];
  const auto t0 = std::chrono::steady_clock::now();
  const auto r = p.Read(b, sizeof(b), 120, {});
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - t0).count();
  EXPECT_EQ(r.status, IoStatus::kTimeout);
  EXPECT_EQ(r.bytes, 0U);
  EXPECT_GE(ms, 110);
  EXPECT_LT(p.Read(b, sizeof(b), 0, {}).bytes, 1U);  // timeout 0 = natychmiast
}

TEST(SerialPort, WriteReachesPeer) {
  Pty pty;
  ASSERT_TRUE(pty.ok());
  SerialPort p;
  ASSERT_TRUE(p.Open(pty.path(), 921600).ok);
  const std::vector<std::uint8_t> data{1, 2, 3, 0xFD, 0xFE, 0, 9};
  const auto w = p.Write(data.data(), data.size(), 500, {});
  EXPECT_EQ(w.status, IoStatus::kOk);
  EXPECT_EQ(w.bytes, data.size());
  EXPECT_EQ(pty.Read(data.size(), 1000), data);
}

TEST(SerialPort, CloseWakesBlockedRead) {
  Pty pty;
  ASSERT_TRUE(pty.ok());
  SerialPort p;
  ASSERT_TRUE(p.Open(pty.path(), 57600).ok);
  std::atomic<int> status{-1};
  std::thread t{[&] {
    std::uint8_t b[8];
    status = static_cast<int>(p.Read(b, sizeof(b), 20000, {}).status);
  }};
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  p.Close();
  t.join();
  EXPECT_EQ(status.load(), static_cast<int>(IoStatus::kClosed));
}

TEST(SerialPort, StopTokenWakesBlockedRead) {
  Pty pty;
  ASSERT_TRUE(pty.ok());
  SerialPort p;
  ASSERT_TRUE(p.Open(pty.path(), 57600).ok);
  std::stop_source src;
  std::atomic<int> status{-1};
  std::thread t{[&] {
    std::uint8_t b[8];
    status = static_cast<int>(p.Read(b, sizeof(b), 20000, src.get_token()).status);
  }};
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  src.request_stop();
  t.join();
  EXPECT_EQ(status.load(), static_cast<int>(IoStatus::kClosed));
}

TEST(SerialPort, WriteTimesOutPartiallyWhenPeerDoesNotRead) {
  Pty pty;
  ASSERT_TRUE(pty.ok());
  SerialPort p;
  ASSERT_TRUE(p.Open(pty.path(), 57600).ok);
  const std::vector<std::uint8_t> big(4 * 1024 * 1024, 0x5A);
  const auto w = p.Write(big.data(), big.size(), 200, {});
  EXPECT_EQ(w.status, IoStatus::kTimeout);
  EXPECT_LT(w.bytes, big.size());
}

TEST(SerialPort, CloseWakesBlockedWrite) {
  Pty pty;
  ASSERT_TRUE(pty.ok());
  SerialPort p;
  ASSERT_TRUE(p.Open(pty.path(), 57600).ok);
  std::atomic<int> status{-1};
  std::thread t{[&] {
    const std::vector<std::uint8_t> big(4 * 1024 * 1024, 1);
    status = static_cast<int>(p.Write(big.data(), big.size(), 20000, {}).status);
  }};
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  p.Close();
  t.join();
  EXPECT_EQ(status.load(), static_cast<int>(IoStatus::kClosed));
}

TEST(SerialPort, ReadAfterPeerClosedReportsError) {
  auto pty = std::make_unique<Pty>();
  ASSERT_TRUE(pty->ok());
  SerialPort p;
  ASSERT_TRUE(p.Open(pty->path(), 57600).ok);
  pty.reset();  // zamknięcie mastera = utrata urządzenia
  std::uint8_t b[8];
  IoResult r{};
  for (int i = 0; i < 20; ++i) {
    r = p.Read(b, sizeof(b), 100, {});
    if (r.status != IoStatus::kTimeout) {
      break;
    }
  }
  EXPECT_EQ(r.status, IoStatus::kError);
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

TEST(SerialPort, OpenAfterCloseWorksWhileReadIsStillBlocked) {
  Pty pty;
  ASSERT_TRUE(pty.ok());
  SerialPort p;
  ASSERT_TRUE(p.Open(pty.path(), 57600).ok);
  std::atomic<bool> done{false};
  std::thread reader{[&] {
    std::uint8_t b[8];
    std::ignore = p.Read(b, sizeof(b), 20000, {});
    done = true;
  }};
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  SerialPort other;  // inny obiekt: wyłączność musi być oddana od razu po Close()
  p.Close();
  const auto r = other.Open(pty.path(), 57600);
  EXPECT_TRUE(r.ok) << r.message;
  reader.join();
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
  }  // destruktor nieudanego Open() nie może zdjąć flock/TIOCNXCL właścicielowi
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
    std::ignore = p.Read(b, sizeof(b), 0, {});
    const std::uint8_t out[2] = {1, 2};
    std::ignore = p.Write(out, sizeof(out), 50, {});
    p.Close();
  }
  EXPECT_EQ(CountOpenFds(), before);
}

TEST(SerialPort, StopTokenWakesBlockedWrite) {
  Pty pty;
  ASSERT_TRUE(pty.ok());
  SerialPort p;
  ASSERT_TRUE(p.Open(pty.path(), 57600).ok);
  std::stop_source src;
  std::atomic<int> status{-1};
  std::thread t{[&] {
    const std::vector<std::uint8_t> big(4 * 1024 * 1024, 1);
    status = static_cast<int>(p.Write(big.data(), big.size(), 20000, src.get_token()).status);
  }};
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  src.request_stop();
  t.join();
  EXPECT_EQ(status.load(), static_cast<int>(IoStatus::kClosed));
}

TEST(SerialPort, WriteClosedMidWayKeepsCountOfWrittenBytes) {
  Pty pty;
  ASSERT_TRUE(pty.ok());
  SerialPort p;
  ASSERT_TRUE(p.Open(pty.path(), 57600).ok);
  std::atomic<std::size_t> bytes{0};
  std::atomic<int> status{-1};
  std::thread t{[&] {
    const std::vector<std::uint8_t> big(4 * 1024 * 1024, 1);
    const auto r = p.Write(big.data(), big.size(), 20000, {});
    bytes = r.bytes;
    status = static_cast<int>(r.status);
  }};
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  p.Close();
  t.join();
  EXPECT_EQ(status.load(), static_cast<int>(IoStatus::kClosed));
  EXPECT_GT(bytes.load(), 0U);  // część poszła do bufora pty przed zablokowaniem
}

TEST(SerialPort, ReadWithEmptyBufferIsNotReportedAsClosed) {
  Pty pty;
  ASSERT_TRUE(pty.ok());
  SerialPort p;
  ASSERT_TRUE(p.Open(pty.path(), 57600).ok);
  std::uint8_t b[1];
  const auto r = p.Read(b, 0, 10, {});
  EXPECT_EQ(r.status, IoStatus::kOk);
  EXPECT_EQ(r.bytes, 0U);
}

TEST(SerialPort, OpeningADirectoryFailsWithEisdir) {
  SerialPort p;
  const auto r = p.Open("/tmp", 57600);
  EXPECT_FALSE(r.ok);
  EXPECT_EQ(r.err, EISDIR);
}

TEST(SerialPort, ReadOnClosedPortReturnsClosed) {
  SerialPort p;
  std::uint8_t b[4];
  EXPECT_EQ(p.Read(b, sizeof(b), 10, {}).status, IoStatus::kClosed);
  EXPECT_EQ(p.Write(b, sizeof(b), 10, {}).status, IoStatus::kClosed);
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
  // po odczycie po stronie peera znowu jest miejsce
  const auto got = pty.Read(total, 3000);
  EXPECT_EQ(got.size(), total);
  EXPECT_EQ(p.TryWrite(chunk.data(), 16).status, IoStatus::kOk);
}

TEST(SerialPort, TryWriteOnClosedPortReturnsClosed) {
  SerialPort p;
  const std::uint8_t b[2] = {1, 2};
  EXPECT_EQ(p.TryWrite(b, sizeof(b)).status, IoStatus::kClosed);
}

TEST(SerialPort, PollReportsReadableWritableKickTimeoutAndClose) {
  Pty pty;
  ASSERT_TRUE(pty.ok());
  SerialPort p;
  ASSERT_TRUE(p.Open(pty.path(), 115200).ok);
  // nic do odczytu: timeout
  auto r = p.Poll(false, 20, {});
  EXPECT_EQ(r.status, IoStatus::kTimeout);
  EXPECT_FALSE(r.readable);
  EXPECT_FALSE(r.writable);
  // miejsce do zapisu tylko na życzenie
  r = p.Poll(true, 100, {});
  EXPECT_EQ(r.status, IoStatus::kOk);
  EXPECT_TRUE(r.writable);
  // dane od peera
  pty.Write({1, 2, 3});
  r = p.Poll(false, 1000, {});
  EXPECT_TRUE(r.readable);
  std::uint8_t b[8];
  EXPECT_EQ(p.Read(b, sizeof(b), 0, {}).bytes, 3U);
  // Kick budzi czekający Poll i jest czyszczony
  std::thread t{[&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    p.Kick();
  }};
  r = p.Poll(false, 5000, {});
  t.join();
  EXPECT_TRUE(r.kicked);
  EXPECT_EQ(p.Poll(false, 20, {}).status, IoStatus::kTimeout);
  // stop_token i Close()
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
  pty.reset();  // master zamknięty = urządzenie zniknęło
  EXPECT_EQ(p.Poll(false, 1000, {}).status, IoStatus::kError);
  EXPECT_EQ(p.Poll(true, 1000, {}).status, IoStatus::kError);
}

}  // namespace
}  // namespace serial
}  // namespace core
}  // namespace srp
