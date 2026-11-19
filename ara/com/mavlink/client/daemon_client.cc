/**
 * Copyright (c) 2024
 *
 * @file daemon_client.cc
 */
#include "ara/com/mavlink/client/daemon_client.h"

#include <algorithm>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "ara/com/com_error_domain.h"
#include "core/common/condition.h"

namespace ara {
namespace com {
namespace mavlink {
namespace {
using Clock = std::chrono::steady_clock;

const char* StatusName(daemon::RegisterStatus s) {
  switch (s) {
    case daemon::RegisterStatus::kOk: return "ok";
    case daemon::RegisterStatus::kBadVersion: return "bad protocol version";
    case daemon::RegisterStatus::kInvalid: return "invalid description";
    case daemon::RegisterStatus::kUnknownLink: return "unknown link";
    case daemon::RegisterStatus::kConflictInstance: return "instance conflict";
    case daemon::RegisterStatus::kConflictMsgId: return "msg_id conflict";
    case daemon::RegisterStatus::kDuplicateHandle: return "duplicate handle";
  }
  return "?";
}
}  // namespace

DaemonClient::DaemonClient() : DaemonClient(Options{}) {}

DaemonClient::DaemonClient(Options opts) : opts_{std::move(opts)} {}

DaemonClient::~DaemonClient() { Shutdown(); }

void DaemonClient::Log(LogLevel l, const std::string& m) const {
  if (opts_.log) {
    opts_.log(l, "[mavlink] " + m);
  }
}

bool DaemonClient::IsConnected() const {
  std::lock_guard<std::mutex> lock{mutex_};
  return connected_;
}

bool DaemonClient::connected() const { return IsConnected(); }

std::shared_ptr<MavlinkEndpoint> DaemonClient::AttachEndpoint(
    const ara::core::model::ModelMavlink& model, bool skeleton, MavlinkEndpoint::Handlers handlers) {
  std::uint32_t handle = 0;
  std::shared_ptr<MavlinkEndpoint> ep;
  {
    std::lock_guard<std::mutex> lock{mutex_};
    handle = next_handle_++;
    ep.reset(new MavlinkEndpoint(this, handle, model, skeleton, std::move(handlers)));
    Entry e;
    e.ep = ep;
    e.reg = ep->BuildRegistration();
    if (e.reg.has_value()) {
      e.reg->handle = handle;
      const std::string err = daemon::Validate(*e.reg);
      if (!err.empty()) {
        Log(LogLevel::kError, "instance '" + model.com_id_ + "' (" +
                                        std::to_string(model.instance_) +
                                        ") has an invalid description: " + err);
        e.reg.reset();
      }
    } else {
      Log(LogLevel::kWarn, "instance '" + model.com_id_ + "' (" +
                                     std::to_string(model.instance_) +
                                     ") has no MAVLink elements — not registered");
    }
    entries_[handle] = std::move(e);
  }
  ep->worker_ = std::jthread([raw = ep.get()](std::stop_token st) { raw->WorkerLoop(st); });
  EnsureStarted();
  RegisterEntry(handle);
  return ep;
}

void DaemonClient::DetachEndpoint(const std::shared_ptr<MavlinkEndpoint>& ep) {
  if (!ep) {
    return;
  }
  bool was_registered = false;
  std::uint32_t handle = 0;
  {
    std::lock_guard<std::mutex> lock{mutex_};
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
      if (it->second.ep == ep) {
        was_registered = it->second.state == RegState::kRegistered;
        handle = it->first;
        entries_.erase(it);
        break;
      }
    }
  }
  if (was_registered) {
    daemon::Unregister u;
    u.handle = handle;
    std::ignore = Send(u);
  }
  ep->StopWorker();
  ep->OnDisconnected();
}

void DaemonClient::Shutdown() {
  std::vector<std::shared_ptr<MavlinkEndpoint>> eps;
  {
    std::lock_guard<std::mutex> lock{mutex_};
    for (const auto& kv : entries_) {
      eps.push_back(kv.second.ep);
    }
  }
  for (const auto& ep : eps) {
    DetachEndpoint(ep);
  }
  io_.request_stop();
  {
    daemon::MessageSocket sock;
    {
      std::lock_guard<std::mutex> lock{mutex_};
      sock = conn_;
    }
    if (sock.IsOpen()) {
      sock.Shutdown();
    }
  }
  if (io_.joinable()) {
    io_.join();
  }
  std::lock_guard<std::mutex> lock{mutex_};
  conn_ = daemon::MessageSocket{};
  connected_ = false;
}

void DaemonClient::EnsureStarted() {
  std::lock_guard<std::mutex> lock{mutex_};
  if (io_.joinable()) {
    return;
  }
  io_ = std::jthread([this](std::stop_token st) { IoLoop(st); });
}

ara::core::Result<void> DaemonClient::Send(const daemon::Message& m) {
  daemon::MessageSocket sock;
  {
    std::lock_guard<std::mutex> lock{mutex_};
    if (!connected_) {
      return ara::com::MakeErrorCode(ara::com::ComErrc::kCommunicationLinkError,
                                     "not connected to the MavlinkDaemon");
    }
    sock = conn_;
  }
  const auto r = sock.Send(m, opts_.send_timeout_ms);
  if (!r.HasValue()) {
    // A full daemon buffer is not a broken connection: dropping it would lose every registration
    // of the process. Only a socket error closes it (the IO thread then reconnects).
    if (!daemon::IsSendTimeout(r.Error())) {
      sock.Shutdown();
    }
    return r.Error();
  }
  return {};
}

bool DaemonClient::ConnectOnce() {
  auto c = daemon::MessageSocket::Connect(opts_.socket_path);
  if (!c.HasValue()) {
    return false;
  }
  {
    std::lock_guard<std::mutex> lock{mutex_};
    conn_ = c.Value();
    connected_ = true;
    for (auto& kv : entries_) {
      kv.second.state = RegState::kIdle;
      kv.second.logged_reject = false;
    }
  }
  Log(LogLevel::kInfo, "connected to the MavlinkDaemon (" + opts_.socket_path + ")");
  RegisterPending();
  return true;
}

void DaemonClient::RegisterPending() {
  std::vector<std::uint32_t> handles;
  {
    std::lock_guard<std::mutex> lock{mutex_};
    for (const auto& kv : entries_) {
      handles.push_back(kv.first);
    }
  }
  for (const auto h : handles) {
    RegisterEntry(h);
  }
}

void DaemonClient::RegisterEntry(std::uint32_t handle) {
  daemon::RegisterInstance reg;
  {
    std::lock_guard<std::mutex> lock{mutex_};
    const auto it = entries_.find(handle);
    if (it == entries_.end() || !connected_ || !it->second.reg.has_value() ||
        it->second.state != RegState::kIdle) {
      return;
    }
    it->second.state = RegState::kPending;
    reg = *it->second.reg;
  }
  const auto r = Send(reg);
  if (!r.HasValue()) {
    Log(LogLevel::kWarn, "sending the registration of '" + reg.com_id + "' failed");
  }
}

void DaemonClient::Disconnect() {
  std::vector<std::shared_ptr<MavlinkEndpoint>> eps;
  {
    std::lock_guard<std::mutex> lock{mutex_};
    if (!connected_) {
      return;
    }
    connected_ = false;
    conn_ = daemon::MessageSocket{};
    for (auto& kv : entries_) {
      kv.second.state = RegState::kIdle;
      eps.push_back(kv.second.ep);
    }
  }
  Log(LogLevel::kWarn, "connection to the MavlinkDaemon lost — registering again when the daemon "
                             "is back");
  for (const auto& ep : eps) {
    ep->OnDisconnected();
  }
}

std::shared_ptr<MavlinkEndpoint> DaemonClient::Find(std::uint32_t handle) {
  std::lock_guard<std::mutex> lock{mutex_};
  const auto it = entries_.find(handle);
  return it == entries_.end() ? nullptr : it->second.ep;
}

void DaemonClient::OnRegisterReply(const daemon::RegisterReply& r) {
  std::shared_ptr<MavlinkEndpoint> ep;
  std::string com_id;
  bool first_reject = false;
  {
    std::lock_guard<std::mutex> lock{mutex_};
    const auto it = entries_.find(r.handle);
    if (it == entries_.end()) {
      return;
    }
    // Registration window: RegisterEntry sets kPending and calls Send outside the mutex; a
    // disconnect and reconnect inside that window yields 2 registrations on the new connection. The
    // second gets kDuplicateHandle although the instance is already registered — its state must
    // not be overwritten with a rejection.
    if (r.status == daemon::RegisterStatus::kDuplicateHandle &&
        it->second.state == RegState::kRegistered) {
      return;
    }
    ep = it->second.ep;
    com_id = ep->name();
    if (r.status == daemon::RegisterStatus::kOk) {
      it->second.state = RegState::kRegistered;
    } else {
      it->second.state = RegState::kRejected;
      it->second.reject = r.status;
      it->second.rejected_at = Clock::now();
      first_reject = !it->second.logged_reject;
      it->second.logged_reject = true;
    }
  }
  if (r.status == daemon::RegisterStatus::kOk) {
    Log(LogLevel::kInfo, "instance '" + com_id + "' (" + std::to_string(ep->instance()) +
                                   ") registered in the MavlinkDaemon (system " +
                                   std::to_string(r.system_id) + ", component " +
                                   std::to_string(r.component_id) + ")");
    ep->OnRegistered();
  } else {
    Log(first_reject ? LogLevel::kError : LogLevel::kDebug,
        "MavlinkDaemon rejected instance '" + com_id + "' (" + std::to_string(ep->instance()) +
            "): " + StatusName(r.status) + (r.text.empty() ? "" : " — " + r.text));
  }
}

void DaemonClient::RetryRejected() {
  std::vector<std::uint32_t> again;
  {
    std::lock_guard<std::mutex> lock{mutex_};
    if (!connected_) {
      return;
    }
    const auto now = Clock::now();
    for (auto& kv : entries_) {
      auto& e = kv.second;
      // Only an instance conflict can be transient (the old registration after an app restart is
      // still being cleaned up); everything else needs a config change / daemon restart = a new
      // connection.
      if (e.state == RegState::kRejected && e.reject == daemon::RegisterStatus::kConflictInstance &&
          now - e.rejected_at >= opts_.register_retry) {
        e.state = RegState::kIdle;
        again.push_back(kv.first);
      }
    }
  }
  for (const auto h : again) {
    RegisterEntry(h);
  }
}

void DaemonClient::Dispatch(daemon::Message m) {
  std::visit(
      [this](auto&& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, daemon::RegisterReply>) {
          OnRegisterReply(v);
        } else if constexpr (std::is_same_v<T, daemon::LinkStatus>) {
          if (auto ep = Find(v.handle)) {
            ep->OnLinkStatus(v.link, v.ready);
          }
        } else if constexpr (std::is_same_v<T, daemon::Delivery>) {
          if (auto ep = Find(v.handle)) {
            if (v.kind == daemon::ElementKind::kResponse) {
              ep->OnResponse(std::move(v));
            } else {
              ep->OnDelivery(std::move(v));
            }
          }
        } else if constexpr (std::is_same_v<T, daemon::SendError>) {
          if (auto ep = Find(v.handle)) {
            ep->OnSendError(v);
          }
        } else {
          Log(LogLevel::kWarn, "unexpected message from the daemon — ignored");
        }
      },
      m);
}

void DaemonClient::IoLoop(std::stop_token st) {
  while (!st.stop_requested()) {
    daemon::MessageSocket sock;
    {
      std::lock_guard<std::mutex> lock{mutex_};
      sock = connected_ ? conn_ : daemon::MessageSocket{};
    }
    if (!sock.IsOpen()) {
      if (!ConnectOnce()) {
        srp::core::condition::wait_for(opts_.reconnect_interval, st);
      }
      continue;
    }
    auto r = sock.Receive(opts_.poll_ms);
    switch (r.status) {
      case daemon::RecvStatus::kMessage:
        Dispatch(std::move(*r.message));
        break;
      case daemon::RecvStatus::kTimeout:
        RetryRejected();
        break;
      case daemon::RecvStatus::kClosed:
        Disconnect();
        break;
      case daemon::RecvStatus::kProtocolError:
        Log(LogLevel::kError, "malformed packet from the MavlinkDaemon — closing the connection");
        sock.Shutdown();
        Disconnect();
        break;
    }
  }
}

}  // namespace mavlink
}  // namespace com
}  // namespace ara
