/**
 * Copyright (c) 2024
 *
 * @file daemon_ipc.cc
 */
#include "ara/com/mavlink/daemon_ipc.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/file.h>
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

ara::core::ErrorCode TimeoutError(const std::string& what) {
  return ara::com::MakeErrorCode(ara::com::ComErrc::kPeerIsUnreachable, what);
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

/// poll() restarted after EINTR; returns >0 ready, 0 timeout, <0 error.
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

bool IsSendTimeout(const ara::core::ErrorCode& e) {
  return e.Domain() == ara::com::GetComDomain() &&
         e.Value() == static_cast<ara::core::ErrorDomain::CodeType>(ara::com::ComErrc::kPeerIsUnreachable);
}

struct MessageSocket::Impl {
  explicit Impl(int f) : fd{f} {}
  ~Impl() { ::close(fd); }
  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;
  const int fd;
  std::mutex send_mutex;
  /// Receive buffer (kMaxMessageSize + 1: MSG_TRUNC detection); Receive() runs on one thread only.
  std::vector<std::uint8_t> rx_buf = std::vector<std::uint8_t>(kMaxMessageSize + 1);
};

MessageSocket::MessageSocket(int fd) : impl_{std::make_shared<Impl>(fd)} {}

void MessageSocket::Shutdown() {
  if (impl_) {
    // shutdown instead of close: the fd must not be reused while another thread is in poll().
    ::shutdown(impl_->fd, SHUT_RDWR);
  }
}

ara::core::Result<MessageSocket> MessageSocket::Connect(const std::string& path) {
  sockaddr_un addr;
  if (!FillAddress(path, &addr)) {
    return LinkError("bad daemon socket path: '" + path + "'");
  }
  const int fd = ::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    return LinkError(std::string("socket: ") + std::strerror(errno));
  }
  if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    const std::string why = std::strerror(errno);
    ::close(fd);
    return LinkError("cannot connect to the daemon (" + path + "): " + why);
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
    return LinkError("socket closed");
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
      return r == 0 ? TimeoutError("peer buffer full (timeout)") : LinkError("socket broken");
    }
    return LinkError(std::string("send: ") + (n < 0 ? std::strerror(errno) : "partial write"));
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
  // The buffer is one byte larger than the biggest legal packet: a longer packet fills it and
  // fails Decode.
  auto& buf = impl_->rx_buf;
  ssize_t n;
  do {
    n = ::recv(fd, buf.data(), buf.size(), MSG_DONTWAIT);
  } while (n < 0 && errno == EINTR);
  if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    out.status = RecvStatus::kTimeout;
    return out;
  }
  if (n <= 0) {
    out.status = RecvStatus::kClosed;  // 0 = EOF from the peer
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
  if (lock_fd_ >= 0) {
    ::close(lock_fd_);  // releases the flock; the lock file itself stays (see Listen)
    lock_fd_ = -1;
  }
}

ara::core::Result<void> MessageServer::Listen(const std::string& path, mode_t mode) {
  Close();
  sockaddr_un addr;
  if (!FillAddress(path, &addr)) {
    return LinkError("bad daemon socket path: '" + path + "'");
  }
  // A live daemon is detected by an exclusive flock on `<path>.lock`, not by connecting to the
  // socket (a probe connection would show up as a phantom session in the live daemon's log).
  // The kernel drops the lock when its owner dies, so a leftover socket file of a dead process
  // is replaced. The lock file is never unlinked: removing it while locked would let a second
  // daemon lock a new inode while the first one is still running.
  const std::string lock_path = path + ".lock";
  const int lock_fd = ::open(lock_path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
  if (lock_fd < 0) {
    return LinkError("open " + lock_path + ": " + std::strerror(errno));
  }
  if (::flock(lock_fd, LOCK_EX | LOCK_NB) != 0) {
    const bool live = errno == EWOULDBLOCK;
    const std::string why = std::strerror(errno);
    ::close(lock_fd);
    return LinkError(live ? "daemon already running: " + lock_path + " is locked"
                          : "flock " + lock_path + ": " + why);
  }
  const int fd = ::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    const std::string why = std::strerror(errno);
    ::close(lock_fd);
    return LinkError("socket: " + why);
  }
  ::unlink(path.c_str());
  // umask: the socket is created with the rights `mode` right away (no window with wider ones).
  const mode_t old_umask = ::umask(static_cast<mode_t>(~mode & 0777));
  const int rc = ::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
  const int bind_errno = errno;
  ::umask(old_umask);
  if (rc != 0) {
    ::close(fd);
    ::close(lock_fd);
    return LinkError("bind " + path + ": " + std::strerror(bind_errno));
  }
  if (::listen(fd, 16) != 0) {
    const std::string why = std::strerror(errno);
    ::close(fd);
    ::close(lock_fd);
    ::unlink(path.c_str());
    return LinkError("listen " + path + ": " + why);
  }
  fd_ = fd;
  lock_fd_ = lock_fd;
  path_ = path;
  return {};
}

ara::core::Result<std::optional<MessageSocket>> MessageServer::Accept(std::uint32_t timeout_ms) {
  if (fd_ < 0) {
    return LinkError("server is not listening");
  }
  const int r = PollFd(fd_, POLLIN, timeout_ms);
  if (r == 0) {
    return std::optional<MessageSocket>{};
  }
  if (r < 0) {
    return LinkError("poll on the listening socket");
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
