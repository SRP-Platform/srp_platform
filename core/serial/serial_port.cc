/**
 * @file serial_port.cc
 * @copyright Copyright (c) 2024
 */
#include "core/serial/serial_port.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <utility>

namespace srp {
namespace core {
namespace serial {
namespace {

speed_t ToSpeed(std::uint32_t baud) {
  switch (baud) {
    case 1200: return B1200;
    case 2400: return B2400;
    case 4800: return B4800;
    case 9600: return B9600;
    case 19200: return B19200;
    case 38400: return B38400;
    case 57600: return B57600;
    case 115200: return B115200;
    case 230400: return B230400;
    case 460800: return B460800;
    case 921600: return B921600;
    default: return B0;
  }
}

class EventFd {
 public:
  EventFd() : fd_{::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)} {}
  ~EventFd() {
    if (fd_ >= 0) {
      ::close(fd_);
    }
  }
  EventFd(const EventFd&) = delete;
  EventFd& operator=(const EventFd&) = delete;
  int fd() const { return fd_; }
  void Signal() const {
    const std::uint64_t one = 1;
    [[maybe_unused]] const auto r = ::write(fd_, &one, sizeof(one));
  }
  /// Returns true if it was signalled (resets the counter).
  bool Drain() const {
    std::uint64_t v = 0;
    return ::read(fd_, &v, sizeof(v)) == static_cast<ssize_t>(sizeof(v));
  }

 private:
  int fd_;
};

}  // namespace

struct SerialPort::State {
  int fd{-1};
  EventFd wake;  // Close() signal; never drained
  EventFd kick;  // Kick() signal; drained by Poll()
  EventFd stop;  // stop_token signal; drained by Poll()
  std::atomic<bool> closed{false};
  // Release only what this instance acquired (for root, TIOCNXCL/LOCK_UN would
  // drop the exclusivity of another owner of the port).
  std::atomic<bool> excl_set{false};
  std::atomic<bool> locked{false};

  /// Idempotent; called straight from Close() so that the next Open() does not
  /// wait for a thread still holding the fd.
  void ReleaseExclusivity() {
    if (fd < 0) {
      return;
    }
    if (excl_set.exchange(false)) {
      ::ioctl(fd, TIOCNXCL);  // the EXCL flag survives close on a pty (the master stays alive)
    }
    if (locked.exchange(false)) {
      ::flock(fd, LOCK_UN);
    }
  }
  ~State() {
    ReleaseExclusivity();
    if (fd >= 0) {
      ::close(fd);
    }
  }
};

SerialPort::~SerialPort() { Close(); }

bool SerialPort::IsBaudrateSupported(std::uint32_t baudrate) {
  return ToSpeed(baudrate) != B0;
}

std::shared_ptr<SerialPort::State> SerialPort::Acquire() const {
  std::lock_guard<std::mutex> lock{mutex_};
  return state_;
}

bool SerialPort::IsOpen() const {
  const auto st = Acquire();
  return st != nullptr && !st->closed.load();
}

OpenResult SerialPort::Open(const std::string& device, std::uint32_t baudrate) {
  Close();
  OpenResult res;
  const speed_t speed = ToSpeed(baudrate);
  if (speed == B0) {
    res.err = EINVAL;
    res.message = "baudrate " + std::to_string(baudrate) + " is not supported";
    return res;
  }
  auto st = std::make_shared<State>();
  if (st->wake.fd() < 0 || st->kick.fd() < 0 || st->stop.fd() < 0) {
    res.err = errno;
    res.message = "eventfd: " + std::string{std::strerror(res.err)};
    return res;
  }
  st->fd = ::open(device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
  if (st->fd < 0) {
    res.err = errno;
    res.busy = res.err == EBUSY;
    res.message = "open(" + device + "): " + std::strerror(res.err);
    return res;
  }
  if (::ioctl(st->fd, TIOCEXCL) == 0) {
    st->excl_set.store(true);
  } else if (errno != ENOTTY) {
    res.err = errno;
    res.message = "TIOCEXCL(" + device + "): " + std::strerror(res.err);
    return res;
  }
  if (::flock(st->fd, LOCK_EX | LOCK_NB) == 0) {
    st->locked.store(true);
  } else {
    res.err = errno;
    res.busy = res.err == EWOULDBLOCK;
    res.message = res.busy ? "port " + device + " is in use (flock)"
                           : "flock(" + device + "): " + std::strerror(res.err);
    return res;
  }
  termios tio{};
  if (::tcgetattr(st->fd, &tio) < 0) {
    res.err = errno;
    res.message = device + " is not a serial port: " + std::strerror(res.err);
    return res;
  }
  ::cfmakeraw(&tio);
  tio.c_cflag |= (CLOCAL | CREAD);
  tio.c_cflag &= ~static_cast<tcflag_t>(CRTSCTS);
  tio.c_cflag &= ~static_cast<tcflag_t>(CSTOPB);
  tio.c_cc[VMIN] = 0;
  tio.c_cc[VTIME] = 0;
  ::cfsetispeed(&tio, speed);
  ::cfsetospeed(&tio, speed);
  if (::tcsetattr(st->fd, TCSANOW, &tio) < 0) {
    res.err = errno;
    res.message = "tcsetattr(" + device + "): " + std::strerror(res.err);
    return res;
  }
  ::tcflush(st->fd, TCIOFLUSH);
  {
    std::lock_guard<std::mutex> lock{mutex_};
    state_ = std::move(st);
  }
  res.ok = true;
  return res;
}

void SerialPort::Close() {
  std::shared_ptr<State> st;
  {
    std::lock_guard<std::mutex> lock{mutex_};
    st = std::move(state_);
  }
  if (st) {
    st->closed.store(true);
    st->wake.Signal();
    // Exclusivity is released at once: a new Open() must not depend on when a
    // blocked Poll() drops its reference (~State closes the fd).
    st->ReleaseExclusivity();
  }
}

IoResult SerialPort::TryRead(std::uint8_t* buf, std::size_t len) {
  const auto st = Acquire();
  if (!st || st->closed.load()) {
    return {IoStatus::kClosed, 0, 0};
  }
  if (len == 0) {
    return {IoStatus::kOk, 0, 0};  // empty buffer: nothing to read (not "closed")
  }
  for (;;) {
    const ssize_t n = ::read(st->fd, buf, len);
    if (n > 0) {
      return {IoStatus::kOk, static_cast<std::size_t>(n), 0};
    }
    if (n < 0 && errno == EINTR) {
      continue;
    }
    // n == 0 on a tty with VMIN=0 means "no data"; device loss is reported by
    // Poll() (POLLHUP/EIO).
    if (n == 0 || errno == EAGAIN || errno == EWOULDBLOCK) {
      return {IoStatus::kTimeout, 0, 0};
    }
    return {IoStatus::kError, 0, errno};
  }
}

IoResult SerialPort::TryWrite(const std::uint8_t* data, std::size_t len) {
  const auto st = Acquire();
  if (!st || st->closed.load()) {
    return {IoStatus::kClosed, 0, 0};
  }
  if (len == 0) {
    return {IoStatus::kOk, 0, 0};
  }
  for (;;) {
    const ssize_t n = ::write(st->fd, data, len);
    if (n > 0) {
      return {IoStatus::kOk, static_cast<std::size_t>(n), 0};
    }
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      return {IoStatus::kTimeout, 0, 0};
    }
    return {IoStatus::kError, 0, n < 0 ? errno : EIO};
  }
}

PollResult SerialPort::Poll(bool want_write, std::uint32_t timeout_ms, std::stop_token token) {
  PollResult res;
  const auto st = Acquire();
  if (!st || st->closed.load()) {
    res.status = IoStatus::kClosed;
    return res;
  }
  st->stop.Drain();  // stale signal from a previous call
  std::stop_callback cb{token, [&st] { st->stop.Signal(); }};
  if (token.stop_requested()) {
    res.status = IoStatus::kClosed;
    return res;
  }
  pollfd fds[4] = {{st->fd, static_cast<short>(POLLIN | (want_write ? POLLOUT : 0)), 0},  // NOLINT
                   {st->wake.fd(), POLLIN, 0},
                   {st->kick.fd(), POLLIN, 0},
                   {st->stop.fd(), POLLIN, 0}};
  int pr;
  do {
    pr = ::poll(fds, 4, static_cast<int>(std::min<std::uint32_t>(timeout_ms, 1U << 30)));
  } while (pr < 0 && errno == EINTR);
  if (pr < 0) {
    res.err = errno;
    return res;
  }
  st->stop.Drain();
  if (st->closed.load() || token.stop_requested()) {
    res.status = IoStatus::kClosed;
    return res;
  }
  res.kicked = (fds[2].revents & POLLIN) != 0 && st->kick.Drain();
  res.readable = (fds[0].revents & POLLIN) != 0;
  res.writable = want_write && (fds[0].revents & POLLOUT) != 0;
  // Device loss: HUP/ERR with no data left to read (data from before the HUP is returned
  // first; a tty also reports POLLIN on HUP, so the number of queued bytes decides).
  if ((fds[0].revents & (POLLHUP | POLLERR | POLLNVAL)) != 0) {
    int avail = 0;
    if (::ioctl(st->fd, FIONREAD, &avail) != 0 || avail <= 0) {
      res.status = IoStatus::kError;
      res.err = EIO;
      res.readable = false;
      res.writable = false;
      return res;
    }
  }
  res.status = pr == 0 ? IoStatus::kTimeout : IoStatus::kOk;
  return res;
}

void SerialPort::Kick() {
  const auto st = Acquire();
  if (st) {
    st->kick.Signal();
  }
}

}  // namespace serial
}  // namespace core
}  // namespace srp
