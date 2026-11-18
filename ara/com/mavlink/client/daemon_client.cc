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

namespace ara {
namespace com {
namespace mavlink {
namespace {
using Clock = std::chrono::steady_clock;

const char* StatusName(daemon::RegisterStatus s) {
  switch (s) {
    case daemon::RegisterStatus::kOk: return "ok";
    case daemon::RegisterStatus::kBadVersion: return "zła wersja protokołu";
    case daemon::RegisterStatus::kInvalid: return "niepoprawny opis";
    case daemon::RegisterStatus::kUnknownLink: return "nieznane łącze";
    case daemon::RegisterStatus::kConflictInstance: return "konflikt instancji";
    case daemon::RegisterStatus::kConflictMsgId: return "konflikt msg_id";
    case daemon::RegisterStatus::kDuplicateHandle: return "powtórzony handle";
  }
  return "?";
}
}  // namespace

DaemonClient::DaemonClient() : DaemonClient(Options{}) {}

DaemonClient::DaemonClient(Options opts) : opts_{std::move(opts)} {}

DaemonClient::~DaemonClient() { Shutdown(); }

void DaemonClient::Log(ClientLogLevel l, const std::string& m) const {
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
    const ara::core::model::ModelMavlink& model, bool skeleton) {
  std::uint32_t handle = 0;
  std::shared_ptr<MavlinkEndpoint> ep;
  {
    std::lock_guard<std::mutex> lock{mutex_};
    handle = next_handle_++;
    ep.reset(new MavlinkEndpoint(this, handle, model, skeleton));
    Entry e;
    e.ep = ep;
    e.reg = ep->BuildRegistration();
    if (e.reg.has_value()) {
      e.reg->handle = handle;
      const std::string err = daemon::Validate(*e.reg);
      if (!err.empty()) {
        Log(ClientLogLevel::kError, "instancja '" + model.com_id_ + "' (" +
                                        std::to_string(model.instance_) +
                                        ") ma niepoprawny opis: " + err);
        e.reg.reset();
      }
    } else {
      Log(ClientLogLevel::kWarn, "instancja '" + model.com_id_ + "' (" +
                                     std::to_string(model.instance_) +
                                     ") nie ma elementów na MAVLinku — bez rejestracji");
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
  wake_cv_.notify_all();
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
                                     "brak połączenia z MavlinkDaemon");
    }
    sock = conn_;
  }
  const auto r = sock.Send(m, opts_.send_timeout_ms);
  if (!r.HasValue()) {
    sock.Shutdown();  // wątek IO zobaczy zamknięcie i połączy się od nowa
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
  connects_++;
  Log(ClientLogLevel::kInfo, "połączono z MavlinkDaemon (" + opts_.socket_path + ")");
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
    Log(ClientLogLevel::kWarn, "wysłanie rejestracji '" + reg.com_id + "' nie powiodło się");
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
  Log(ClientLogLevel::kWarn, "połączenie z MavlinkDaemon zerwane — ponowię rejestrację po "
                             "powrocie daemona");
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
    // Okno rejestracji: RegisterEntry ustawia kPending, a Send woła poza mutexem; rozłączenie i ponowne
    // połączenie w tym oknie daje 2 rejestracje na nowym połączeniu. Druga dostaje kDuplicateHandle, ale
    // instancja jest już zarejestrowana — nie wolno nadpisać jej stanu odrzuceniem.
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
    Log(ClientLogLevel::kInfo, "instancja '" + com_id + "' (" + std::to_string(ep->instance()) +
                                   ") zarejestrowana w MavlinkDaemon (system " +
                                   std::to_string(r.system_id) + ", komponent " +
                                   std::to_string(r.component_id) + ")");
    ep->OnRegistered();
  } else {
    Log(first_reject ? ClientLogLevel::kError : ClientLogLevel::kDebug,
        "MavlinkDaemon odrzucił instancję '" + com_id + "' (" + std::to_string(ep->instance()) +
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
      // Tylko konflikt instancji bywa przejściowy (stara rejestracja po restarcie apki jeszcze
      // sprzątana); reszta wymaga zmiany configu/restartu daemona = nowego połączenia.
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
        } else if constexpr (std::is_same_v<T, daemon::EventIn>) {
          if (auto ep = Find(v.handle)) {
            ep->OnDelivery(false, std::move(static_cast<daemon::Delivery&>(v)));
          }
        } else if constexpr (std::is_same_v<T, daemon::RequestIn>) {
          if (auto ep = Find(v.handle)) {
            ep->OnDelivery(true, std::move(static_cast<daemon::Delivery&>(v)));
          }
        } else if constexpr (std::is_same_v<T, daemon::ResponseIn>) {
          if (auto ep = Find(v.handle)) {
            ep->OnResponse(std::move(static_cast<daemon::Delivery&>(v)));
          }
        } else if constexpr (std::is_same_v<T, daemon::SendError>) {
          if (auto ep = Find(v.handle)) {
            ep->OnSendError(v);
          }
        } else {
          Log(ClientLogLevel::kWarn, "nieoczekiwany komunikat od daemona — ignoruję");
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
        std::unique_lock<std::mutex> lock{wake_mutex_};
        wake_cv_.wait_for(lock, st, opts_.reconnect_interval, [] { return false; });
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
        Log(ClientLogLevel::kError, "błędny pakiet od MavlinkDaemon — zamykam połączenie");
        sock.Shutdown();
        Disconnect();
        break;
    }
  }
}

}  // namespace mavlink
}  // namespace com
}  // namespace ara
