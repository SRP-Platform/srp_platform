/**
 * Copyright (c) 2024
 *
 * @file daemon_ipc.cc
 */
#include "ara/com/mavlink/daemon_ipc.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <array>
#include <cstring>
#include <utility>
#include <vector>

#include "ara/com/com_error_domain.h"

namespace ara {
namespace com {
namespace mavlink {
namespace daemon {
namespace {

ara::core::ErrorCode LinkError(const std::string& what) {
  return ara::com::MakeErrorCode(ara::com::ComErrc::kCommunicationLinkError, what);
}

bool FillAddress(const std::string& path, sockaddr_un* addr) {
  std::memset(addr, 0, sizeof(*addr));
  addr->sun_family = AF_UNIX;
  if (path.empty() || path.size() >= sizeof(addr->sun_path)) {
    return false;
  }
  std::memcpy(addr->sun_path, path.c_str(), path.size() + 1);
  return true;
}

/// poll() z wznawianiem po EINTR; zwraca >0 gotowe, 0 timeout, <0 błąd.
int PollFd(int fd, short events, std::uint32_t timeout_ms) {  // NOLINT(runtime/int)
  pollfd p{fd, events, 0};
  for (;;) {
    const int r = poll(&p, 1, static_cast<int>(timeout_ms));
    if (r < 0 && errno == EINTR) {
      continue;
    }
    if (r > 0 && (p.revents & (POLLERR | POLLNVAL)) != 0 && (p.revents & events) == 0) {
      return -1;
    }
    return r;
  }
}

}  // namespace

struct MessageSocket::Impl {
  explicit Impl(int f) : fd{f} {}
  ~Impl() { ::close(fd); }
  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;
  const int fd;
  std::mutex send_mutex;
};

MessageSocket::MessageSocket(int fd) : impl_{std::make_shared<Impl>(fd)} {}

void MessageSocket::Shutdown() {
  if (impl_) {
    // shutdown zamiast close: fd nie może zostać ponownie użyty, gdy inny wątek jest w poll().
    ::shutdown(impl_->fd, SHUT_RDWR);
  }
}

ara::core::Result<MessageSocket> MessageSocket::Connect(const std::string& path) {
  sockaddr_un addr;
  if (!FillAddress(path, &addr)) {
    return LinkError("zła ścieżka gniazda daemona: '" + path + "'");
  }
  const int fd = ::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    return LinkError(std::string("socket: ") + std::strerror(errno));
  }
  if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    const std::string why = std::strerror(errno);
    ::close(fd);
    return LinkError("brak połączenia z daemonem (" + path + "): " + why);
  }
  return MessageSocket{fd};
}

ara::core::Result<void> MessageSocket::Send(const Message& m, std::uint32_t timeout_ms) {
  auto enc = Encode(m);
  if (!enc.HasValue()) {
    return enc.Error();
  }
  const auto& pkt = enc.Value();
  if (!impl_) {
    return LinkError("gniazdo zamknięte");
  }
  const int fd = impl_->fd;
  std::lock_guard<std::mutex> lock{impl_->send_mutex};
  for (;;) {
    const ssize_t n = ::send(fd, pkt.data(), pkt.size(), MSG_NOSIGNAL | MSG_DONTWAIT);
    if (n == static_cast<ssize_t>(pkt.size())) {
      return {};
    }
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      const int r = PollFd(fd, POLLOUT, timeout_ms);
      if (r > 0) {
        continue;
      }
      return LinkError(r == 0 ? "bufor daemona pełny (timeout)" : "gniazdo zerwane");
    }
    return LinkError(std::string("send: ") + (n < 0 ? std::strerror(errno) : "zapis częściowy"));
  }
}

RecvResult MessageSocket::Receive(std::uint32_t timeout_ms) {
  RecvResult out;
  if (!impl_) {
    out.status = RecvStatus::kClosed;
    return out;
  }
  const int fd = impl_->fd;
  const int r = PollFd(fd, POLLIN, timeout_ms);
  if (r == 0) {
    out.status = RecvStatus::kTimeout;
    return out;
  }
  if (r < 0) {
    out.status = RecvStatus::kClosed;
    return out;
  }
  // MSG_TRUNC: dowiadujemy się o pakiecie większym niż bufor (kMaxMessageSize + 1).
  std::vector<std::uint8_t> buf(kMaxMessageSize + 1);
  ssize_t n;
  do {
    n = ::recv(fd, buf.data(), buf.size(), MSG_DONTWAIT);
  } while (n < 0 && errno == EINTR);
  if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    out.status = RecvStatus::kTimeout;
    return out;
  }
  if (n <= 0) {
    out.status = RecvStatus::kClosed;  // 0 = EOF drugiej strony
    return out;
  }
  auto dec = Decode(std::span<const std::uint8_t>(buf.data(), static_cast<std::size_t>(n)));
  if (!dec.HasValue()) {
    out.status = RecvStatus::kProtocolError;
    return out;
  }
  out.status = RecvStatus::kMessage;
  out.message = std::move(dec).Value();
  return out;
}

MessageServer::~MessageServer() { Close(); }

void MessageServer::Close() {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
    if (!path_.empty()) {
      ::unlink(path_.c_str());
    }
  }
}

ara::core::Result<void> MessageServer::Listen(const std::string& path, mode_t mode) {
  Close();
  sockaddr_un addr;
  if (!FillAddress(path, &addr)) {
    return LinkError("zła ścieżka gniazda daemona: '" + path + "'");
  }
  const int fd = ::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    return LinkError(std::string("socket: ") + std::strerror(errno));
  }
  // Żywy daemon pod tą ścieżką: nie przejmujemy jego gniazda (unlink zostawiłby go bez klientów, a dwa
  // daemony dzieliłyby łącza). Pozostałość po martwym procesie (connect odmawia) usuwamy.
  if (auto live = MessageSocket::Connect(path); live.HasValue()) {
    ::close(fd);
    return LinkError("daemon już działa: pod " + path + " ktoś nasłuchuje");
  }
  ::unlink(path.c_str());
  // umask: gniazdo powstaje od razu z prawami `mode` (bez okna z szerszymi).
  const mode_t old_umask = ::umask(static_cast<mode_t>(~mode & 0777));
  const int rc = ::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
  const int bind_errno = errno;
  ::umask(old_umask);
  if (rc != 0) {
    ::close(fd);
    return LinkError("bind " + path + ": " + std::strerror(bind_errno));
  }
  if (::listen(fd, 16) != 0) {
    const std::string why = std::strerror(errno);
    ::close(fd);
    ::unlink(path.c_str());
    return LinkError("listen " + path + ": " + why);
  }
  fd_ = fd;
  path_ = path;
  return {};
}

ara::core::Result<std::optional<MessageSocket>> MessageServer::Accept(std::uint32_t timeout_ms) {
  if (fd_ < 0) {
    return LinkError("serwer nie nasłuchuje");
  }
  const int r = PollFd(fd_, POLLIN, timeout_ms);
  if (r == 0) {
    return std::optional<MessageSocket>{};
  }
  if (r < 0) {
    return LinkError("poll na gnieździe nasłuchującym");
  }
  const int cfd = ::accept4(fd_, nullptr, nullptr, SOCK_CLOEXEC);
  if (cfd < 0) {
    if (errno == EAGAIN || errno == EINTR || errno == ECONNABORTED) {
      return std::optional<MessageSocket>{};
    }
    return LinkError(std::string("accept: ") + std::strerror(errno));
  }
  return std::optional<MessageSocket>{MessageSocket{cfd}};
}

}  // namespace daemon
}  // namespace mavlink
}  // namespace com
}  // namespace ara
