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
#include <chrono>  // NOLINT
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

 private:
  int fd_;
};

using Clock = std::chrono::steady_clock;

int RemainingMs(const Clock::time_point& deadline) {
  const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                        deadline - Clock::now())
                        .count();
  return left < 0 ? 0 : static_cast<int>(std::min<long long>(left, 1 << 30));  // NOLINT
}

}  // namespace

struct SerialPort::State {
  int fd{-1};
  EventFd wake;  // sygnał Close(); nigdy nie jest czyszczony
  std::atomic<bool> closed{false};
  // Zwalniamy wyłącznie to, co ta instancja zdobyła (dla roota TIOCNXCL/LOCK_UN
  // zdjęłyby wyłączność innemu właścicielowi portu).
  std::atomic<bool> excl_set{false};
  std::atomic<bool> locked{false};

  /// Idempotentne; wołane od razu z Close(), żeby kolejny Open() nie czekał na
  /// wątek zablokowany w Read()/Write() trzymający jeszcze fd.
  void ReleaseExclusivity() {
    if (fd < 0) {
      return;
    }
    if (excl_set.exchange(false)) {
      ::ioctl(fd, TIOCNXCL);  // flaga EXCL przeżywa close na pty (master żyje)
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
    res.message = "baudrate " + std::to_string(baudrate) + " nieobsługiwany";
    return res;
  }
  auto st = std::make_shared<State>();
  if (st->wake.fd() < 0) {
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
    res.message = res.busy ? "port " + device + " jest zajęty (flock)"
                           : "flock(" + device + "): " + std::strerror(res.err);
    return res;
  }
  termios tio{};
  if (::tcgetattr(st->fd, &tio) < 0) {
    res.err = errno;
    res.message = device + " nie jest portem szeregowym: " + std::strerror(res.err);
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
    // Wyłączność oddajemy od razu: nowy Open() nie może zależeć od tego, kiedy
    // zablokowany Read()/Write() zwolni swoją referencję (fd zamknie ~State).
    st->ReleaseExclusivity();
  }
}

IoResult SerialPort::Read(std::uint8_t* buf, std::size_t len,
                          std::uint32_t timeout_ms, std::stop_token token) {
  const auto st = Acquire();
  if (!st) {
    return {IoStatus::kClosed, 0, 0};
  }
  if (len == 0) {
    return {IoStatus::kOk, 0, 0};  // pusty bufor: nic do odczytu (to nie jest "zamknięty")
  }
  EventFd stop_ev;
  if (stop_ev.fd() < 0) {
    return {IoStatus::kError, 0, errno};
  }
  std::stop_callback cb{token, [&stop_ev] { stop_ev.Signal(); }};
  const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
  for (;;) {
    if (st->closed.load() || token.stop_requested()) {
      return {IoStatus::kClosed, 0, 0};
    }
    const ssize_t n = ::read(st->fd, buf, len);
    if (n > 0) {
      return {IoStatus::kOk, static_cast<std::size_t>(n), 0};
    }
    // n == 0: dla tty z VMIN=0 oznacza "brak danych"; zniknięcie urządzenia
    // wykrywa POLLHUP/EIO poniżej.
    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
      return {IoStatus::kError, 0, errno};
    }
    if (timeout_ms == 0 || Clock::now() >= deadline) {
      return {IoStatus::kTimeout, 0, 0};
    }
    const int left = RemainingMs(deadline);
    pollfd fds[3] = {{st->fd, POLLIN, 0},
                     {st->wake.fd(), POLLIN, 0},
                     {stop_ev.fd(), POLLIN, 0}};
    const int pr = ::poll(fds, 3, left);
    if (pr < 0 && errno != EINTR) {
      return {IoStatus::kError, 0, errno};
    }
    if (pr == 0) {
      return {IoStatus::kTimeout, 0, 0};
    }
    // read() nie dał danych, a poll zgłasza HUP/ERR: urządzenie zniknęło.
    if ((fds[0].revents & (POLLHUP | POLLERR | POLLNVAL)) != 0) {
      return {IoStatus::kError, 0, EIO};
    }
    // zdarzenia wake/stop obsłuży początek pętli
  }
}

IoResult SerialPort::Write(const std::uint8_t* data, std::size_t len,
                           std::uint32_t timeout_ms, std::stop_token token) {
  const auto st = Acquire();
  if (!st) {
    return {IoStatus::kClosed, 0, 0};
  }
  EventFd stop_ev;
  if (stop_ev.fd() < 0) {
    return {IoStatus::kError, 0, errno};
  }
  std::stop_callback cb{token, [&stop_ev] { stop_ev.Signal(); }};
  const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
  std::size_t done = 0;
  while (done < len) {
    if (st->closed.load() || token.stop_requested()) {
      return {IoStatus::kClosed, done, 0};
    }
    const ssize_t n = ::write(st->fd, data + done, len - done);
    if (n > 0) {
      done += static_cast<std::size_t>(n);
      continue;
    }
    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
      return {IoStatus::kError, done, errno};
    }
    if (timeout_ms == 0 || Clock::now() >= deadline) {
      return {IoStatus::kTimeout, done, 0};
    }
    pollfd fds[3] = {{st->fd, POLLOUT, 0},
                     {st->wake.fd(), POLLIN, 0},
                     {stop_ev.fd(), POLLIN, 0}};
    const int pr = ::poll(fds, 3, RemainingMs(deadline));
    if (pr < 0 && errno != EINTR) {
      return {IoStatus::kError, done, errno};
    }
    if (pr > 0 && (fds[0].revents & (POLLERR | POLLHUP)) != 0 &&
        (fds[0].revents & POLLOUT) == 0) {
      return {IoStatus::kError, done, EIO};
    }
  }
  return {IoStatus::kOk, done, 0};
}

}  // namespace serial
}  // namespace core
}  // namespace srp
