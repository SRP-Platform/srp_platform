/**
 * Copyright (c) 2024
 *
 * @file udp_radio.cc
 * @brief `example_udp_radio`: the IRadioDriver contract on a UDP socket. The comments say which
 * rule of radio_driver.h each piece satisfies.
 */
#include "ara/com/mavlink/examples/udp_radio/udp_radio.h"

#include <arpa/inet.h>
#include <linux/errqueue.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>  // NOLINT
#include <cstring>
#include <memory>
#include <string>
#include <utility>

#include "ara/com/mavlink/radio_driver_registry.h"
#include "core/json/json_parser.h"

namespace ara {
namespace com {
namespace mavlink {
namespace examples {
namespace {
constexpr std::size_t kRxBuffer = 2048;  // larger than any datagram we expect
// An unreachable peer is reported to the daemon log (OnError, not fatal) at most this often.
constexpr std::chrono::seconds kReportEvery{10};

bool IsUnreachable(int err) {
  return err == ENETUNREACH || err == EHOSTUNREACH || err == ECONNREFUSED || err == ENETDOWN;
}

TransportErrc ClassifySendError(int err) {
  switch (err) {
    case EINTR:
    case ENETUNREACH:
    case EHOSTUNREACH:
    case ECONNREFUSED:
    case ENETDOWN:
    case EMSGSIZE:
      return TransportErrc::kRetryable;  // this frame is lost, the socket is fine
    default:
      return TransportErrc::kPersistent;  // EBADF, EPERM...: the socket is unusable
  }
}
}  // namespace

UdpRadio::UdpRadio(std::uint16_t bind_port, std::string peer_host, std::uint16_t peer_port)
    : bind_port_{bind_port}, peer_host_{std::move(peer_host)}, peer_port_{peer_port} {
  SetPeer();
}

UdpRadio::UdpRadio(const std::string& config_path) : name_{kUdpRadioId} {
  const auto json = srp::core::json::JsonParser::Parser(config_path);
  if (!json.has_value()) {
    config_error_ = std::string(kUdpRadioId) + ": cannot read the config file '" + config_path + "'";
    return;
  }
  // Like the daemon config (P9): an unknown key (a typo such as "peer_hots") is an error, not a default.
  const auto content = json->GetObject();  // a named copy: items() must not outlive it
  for (const auto& [key, value] : content.items()) {
    if (key != "bind_port" && key != "peer_host" && key != "peer_port") {
      config_error_ = std::string(kUdpRadioId) + ": file '" + config_path + "': unknown key \"" + key +
                      "\" (allowed: \"bind_port\", \"peer_host\", \"peer_port\")";
      return;
    }
  }
  const auto bind = json->GetNumber<std::uint16_t>("bind_port");
  const auto host = json->GetString("peer_host");
  const auto port = json->GetNumber<std::uint16_t>("peer_port");
  if (!bind.has_value() || !host.has_value() || !port.has_value()) {
    config_error_ = std::string(kUdpRadioId) + ": file '" + config_path +
                    "' must contain \"bind_port\", \"peer_host\" and \"peer_port\"";
    return;
  }
  bind_port_ = *bind;
  peer_host_ = *host;
  peer_port_ = *port;
  SetPeer();
}

void UdpRadio::SetPeer() {
  name_ = std::string(kUdpRadioId) + "(:" + std::to_string(bind_port_) + "->" + peer_host_ + ":" +
          std::to_string(peer_port_) + ")";
  peer_.sin_family = AF_INET;
  peer_.sin_port = htons(peer_port_);
  if (::inet_pton(AF_INET, peer_host_.c_str(), &peer_.sin_addr) != 1) {
    config_error_ = std::string(kUdpRadioId) + ": peer_host '" + peer_host_ + "' is not an IPv4 address";
  }
}

UdpRadio::~UdpRadio() { Stop(); }

// Start(): configures the radio and starts the driver thread. Called on the daemon link thread.
// An error makes the daemon retry after its backoff; kPersistent = a bad config (logged once).
ara::core::Result<void> UdpRadio::Start(IRadioListener& listener, std::stop_token token) noexcept {
  std::lock_guard<std::mutex> life{lifecycle_mutex_};
  if (!config_error_.empty()) {
    return MakeTransportError(TransportErrc::kPersistent, config_error_);
  }
  if (thread_.joinable()) {
    return MakeTransportError(TransportErrc::kInvalidArgument, name_ + ": Start() without Stop()");
  }
  if (token.stop_requested()) {
    return MakeTransportError(TransportErrc::kClosed, name_ + ": Start aborted");
  }
  // Non-blocking socket: Send() never waits.
  const int sock = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (sock < 0) {
    return MakeTransportError(TransportErrc::kRetryable, name_ + ": socket: " + std::strerror(errno));
  }
  // No SO_REUSEADDR: UDP needs no TIME_WAIT workaround, and a port that is already taken must fail Start().
  sockaddr_in self{};
  self.sin_family = AF_INET;
  self.sin_port = htons(bind_port_);
  self.sin_addr.s_addr = htonl(INADDR_ANY);
  if (::bind(sock, reinterpret_cast<sockaddr*>(&self), sizeof(self)) != 0) {
    const int err = errno;
    ::close(sock);
    // A busy port / missing permission will not fix itself soon: kPersistent (logged as Error once).
    return MakeTransportError(TransportErrc::kPersistent,
                              name_ + ": bind port " + std::to_string(bind_port_) + ": " + std::strerror(err));
  }
  // Errors of earlier datagrams (ICMP unreachable / port closed) are queued on the socket, not returned
  // by sendto(); the driver thread reads them from the error queue to report an unreachable peer.
  const int on = 1;
  ::setsockopt(sock, IPPROTO_IP, IP_RECVERR, &on, sizeof(on));
  const int wake = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (wake < 0) {
    const int err = errno;
    ::close(sock);
    return MakeTransportError(TransportErrc::kRetryable, name_ + ": eventfd: " + std::strerror(err));
  }
  {
    std::lock_guard<std::mutex> lock{mutex_};
    sock_ = sock;
    wake_fd_ = wake;
    tx_ready_owed_ = false;
    pending_report_.clear();
    running_ = true;  // Send() works from here ("Send() may be called after a successful Start()")
  }
  stop_ = std::stop_source{};
  try {
    thread_ = std::thread{[this, token, l = &listener] { Loop(token, l); }};
  } catch (...) {
    std::lock_guard<std::mutex> lock{mutex_};
    running_ = false;
    ::close(sock_);
    ::close(wake_fd_);
    sock_ = wake_fd_ = -1;
    return MakeTransportError(TransportErrc::kRetryable, name_ + ": could not start the thread");
  }
  return {};
}

// Stop(): joins the driver thread, so after it returns no callback is running or will follow.
// Idempotent; must not be called from a listener callback (it would join itself).
void UdpRadio::Stop() noexcept {
  std::lock_guard<std::mutex> life{lifecycle_mutex_};
  {
    std::lock_guard<std::mutex> lock{mutex_};
    running_ = false;  // from now on Send() returns kClosed
  }
  stop_.request_stop();
  Wake();
  if (thread_.joinable()) {
    thread_.join();
  }
  std::lock_guard<std::mutex> lock{mutex_};
  if (sock_ >= 0) {
    ::close(sock_);
    sock_ = -1;
  }
  if (wake_fd_ >= 0) {
    ::close(wake_fd_);
    wake_fd_ = -1;
  }
}

void UdpRadio::Wake() noexcept {
  std::lock_guard<std::mutex> lock{mutex_};
  if (wake_fd_ >= 0) {
    const std::uint64_t one = 1;
    [[maybe_unused]] const auto wr = ::write(wake_fd_, &one, sizeof(one));
  }
}

// Send(): NON-BLOCKING, whole frame or nothing (a datagram is atomic), never calls the listener.
ara::core::Result<void> UdpRadio::Send(std::span<const std::uint8_t> frame) noexcept {
  std::lock_guard<std::mutex> lock{mutex_};
  if (!running_) {
    return MakeTransportError(TransportErrc::kClosed, name_ + ": stopped");  // an error, not kBusy
  }
  if (frame.size() > kMaxFrame) {
    return MakeTransportError(TransportErrc::kInvalidArgument, name_ + ": frame larger than MaxFrameSize()");
  }
  if (frame.empty()) {
    return {};
  }
  for (;;) {
    const auto n = ::sendto(sock_, frame.data(), frame.size(), MSG_DONTWAIT,
                            reinterpret_cast<const sockaddr*>(&peer_), sizeof(peer_));
    if (n == static_cast<ssize_t>(frame.size())) {
      return {};  // accepted
    }
    const int err = errno;
    if (n < 0 && err == EINTR) {
      continue;
    }
    if (n < 0 && (err == EAGAIN || err == EWOULDBLOCK || err == ENOBUFS)) {
      // No room: nothing was accepted. The contract: OnTxReady() once when room appears. The
      // driver thread polls for POLLOUT while `tx_ready_owed_` is set; the wake-up makes it re-read
      // the flag, so it also works when room appeared just before this return.
      tx_ready_owed_ = true;
      const std::uint64_t one = 1;
      [[maybe_unused]] const auto wr = ::write(wake_fd_, &one, sizeof(one));
      return MakeTransportError(TransportErrc::kBusy, name_ + ": socket buffer full");
    }
    if (IsUnreachable(err)) {
      NoteUnreachable(err);
    }
    return MakeTransportError(ClassifySendError(err), name_ + ": sendto: " + std::strerror(err));
  }
}

// Called with mutex_ held, from Send() or the driver thread. The kRetryable result of Send() is not
// changed; this only makes sure that the daemon log gets one readable line per kReportEvery.
void UdpRadio::NoteUnreachable(int err) {
  const auto now = std::chrono::steady_clock::now();
  if (reported_ && now - last_report_ < kReportEvery) {
    return;
  }
  reported_ = true;
  last_report_ = now;
  pending_report_ = name_ + ": peer " + peer_host_ + ":" + std::to_string(peer_port_) + " unreachable (" +
                    std::strerror(err) + "), frames to it are lost; reported at most once per " +
                    std::to_string(kReportEvery.count()) + " s";
  if (wake_fd_ >= 0) {
    const std::uint64_t one = 1;
    [[maybe_unused]] const auto wr = ::write(wake_fd_, &one, sizeof(one));
  }
}

// Driver thread: reads the queued socket errors (IP_RECVERR) and notes the unreachable-peer ones.
void UdpRadio::DrainErrorQueue() {
  for (;;) {
    std::uint8_t data[kRxBuffer];
    alignas(cmsghdr) char control[256];
    iovec iov{data, sizeof(data)};
    msghdr msg{};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);
    if (::recvmsg(sock_, &msg, MSG_ERRQUEUE | MSG_DONTWAIT) < 0) {
      return;  // empty (EAGAIN) or not readable: nothing more to learn
    }
    for (cmsghdr* c = CMSG_FIRSTHDR(&msg); c != nullptr; c = CMSG_NXTHDR(&msg, c)) {
      if (c->cmsg_level == IPPROTO_IP && c->cmsg_type == IP_RECVERR) {
        sock_extended_err ee;
        std::memcpy(&ee, CMSG_DATA(c), sizeof(ee));
        if (IsUnreachable(static_cast<int>(ee.ee_errno))) {
          std::lock_guard<std::mutex> lock{mutex_};
          NoteUnreachable(static_cast<int>(ee.ee_errno));
        }
      }
    }
  }
}

// The driver thread: RX and POLLOUT in one poll(); all callbacks come from here.
void UdpRadio::Loop(std::stop_token caller, IRadioListener* listener) {
  const std::stop_token st = stop_.get_token();
  // A stop requested through the token passed to Start() (daemon shutdown) ends the thread too.
  std::stop_callback on_caller_stop{caller, [this] { Wake(); }};
  std::uint8_t buf[kRxBuffer];
  while (!st.stop_requested() && !caller.stop_requested()) {
    bool want_write;
    {
      std::lock_guard<std::mutex> lock{mutex_};
      want_write = tx_ready_owed_;
    }
    pollfd fds[2] = {{sock_, static_cast<std::int16_t>(POLLIN | (want_write ? POLLOUT : 0)), 0},
                     {wake_fd_, POLLIN, 0}};
    if (::poll(fds, 2, -1) < 0 && errno != EINTR) {
      // Fatal: the daemon calls Stop() and Start() again after its backoff.
      listener->OnError(MakeTransportError(TransportErrc::kPersistent, name_ + ": poll: " + std::strerror(errno)),
                        true);
      return;
    }
    if (fds[1].revents != 0) {
      std::uint64_t drain;
      [[maybe_unused]] const auto rd = ::read(wake_fd_, &drain, sizeof(drain));
    }
    if (st.stop_requested() || caller.stop_requested()) {
      break;
    }
    if ((fds[0].revents & POLLERR) != 0) {
      DrainErrorQueue();
    }
    std::string report;
    {
      std::lock_guard<std::mutex> lock{mutex_};
      report.swap(pending_report_);
    }
    if (!report.empty()) {
      listener->OnError(MakeTransportError(TransportErrc::kRetryable, report), false);  // informational: logged
      if (st.stop_requested()) {
        return;
      }
    }
    if ((fds[0].revents & POLLIN) != 0) {
      for (;;) {  // one datagram = one OnReceive (the daemon copies it: keep the callback short)
        const auto n = ::recv(sock_, buf, sizeof(buf), MSG_DONTWAIT);
        if (n > 0) {
          listener->OnReceive(std::span<const std::uint8_t>(buf, static_cast<std::size_t>(n)));
          if (st.stop_requested()) {
            return;  // no callback after a stop request
          }
          continue;
        }
        if (n < 0 && errno == EINTR) {
          continue;
        }
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != ECONNREFUSED) {
          listener->OnError(MakeTransportError(TransportErrc::kPersistent, name_ + ": recv: " + std::strerror(errno)),
                            true);
          return;
        }
        break;  // drained
      }
    }
    if ((fds[0].revents & POLLOUT) != 0) {
      bool ready;
      {
        std::lock_guard<std::mutex> lock{mutex_};
        ready = tx_ready_owed_;
        tx_ready_owed_ = false;
      }
      if (ready && !st.stop_requested()) {
        listener->OnTxReady();  // outside the mutex: the listener may call Send()
      }
    }
  }
}

namespace {
RadioDriverFactory MakeFactory() {
  return [](const std::string& config_path) {
    return std::shared_ptr<IRadioDriver>{std::make_shared<UdpRadio>(config_path)};
  };
}
}  // namespace

// Static registration: `links.<name>.driver` = "example_udp_radio" selects this factory. The library
// MUST have alwayslink = True, otherwise the linker drops this object file.
SRP_MAVLINK_REGISTER_DRIVER(kUdpRadioId, MakeFactory());

}  // namespace examples
}  // namespace mavlink
}  // namespace com
}  // namespace ara
