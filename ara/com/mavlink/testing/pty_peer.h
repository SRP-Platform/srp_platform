/**
 * Copyright (c) 2024
 *
 * @file pty_peer.h
 * @brief TESTONLY: a pty as the "other end of the wire" for serial drivers: the master is for the
 * test, the slave (`path`) is for the driver. Closed together with its last owner (shared_ptr).
 */
#ifndef ARA_COM_MAVLINK_TESTING_PTY_PEER_H_
#define ARA_COM_MAVLINK_TESTING_PTY_PEER_H_

#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>  // NOLINT
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace ara {
namespace com {
namespace mavlink {
namespace testing {

class PtyPeer {
 public:
  PtyPeer() {
    fd_ = ::posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ >= 0) {
      ::grantpt(fd_);
      ::unlockpt(fd_);
      const char* n = ::ptsname(fd_);
      path_ = n != nullptr ? n : "";
    }
  }
  ~PtyPeer() {
    if (fd_ >= 0) {
      ::close(fd_);
    }
  }
  PtyPeer(const PtyPeer&) = delete;
  PtyPeer& operator=(const PtyPeer&) = delete;

  const std::string& path() const { return path_; }
  bool ok() const { return fd_ >= 0; }

  /// Writes everything or gives up (error / 5 s timeout) — never hangs on a closed slave.
  void Write(const std::vector<std::uint8_t>& v) const {
    std::size_t done = 0;
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (done < v.size() && std::chrono::steady_clock::now() < end) {
      const ssize_t n = ::write(fd_, v.data() + done, v.size() - done);
      if (n > 0) {
        done += static_cast<std::size_t>(n);
      } else if (n < 0 && errno != EAGAIN && errno != EINTR) {
        return;
      } else {
        pollfd p{fd_, POLLOUT, 0};
        ::poll(&p, 1, 20);
      }
    }
  }

  /// Up to `want` bytes; waits at most `timeout_ms` for the first chunk and stops when nothing more arrives.
  std::vector<std::uint8_t> Read(std::size_t want, std::uint32_t timeout_ms) const {
    std::vector<std::uint8_t> out;
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (out.size() < want && std::chrono::steady_clock::now() < end) {
      pollfd p{fd_, POLLIN, 0};
      if (::poll(&p, 1, 20) > 0 && (p.revents & POLLIN) != 0) {
        std::uint8_t b[4096];
        const ssize_t n = ::read(fd_, b, std::min<std::size_t>(sizeof(b), want - out.size()));
        if (n > 0) {
          out.insert(out.end(), b, b + n);
        }
      }
    }
    return out;
  }

 private:
  int fd_{-1};
  std::string path_;
};

}  // namespace testing
}  // namespace mavlink
}  // namespace com
}  // namespace ara

#endif  // ARA_COM_MAVLINK_TESTING_PTY_PEER_H_
