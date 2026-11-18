/**
 * Copyright (c) 2024
 *
 * @file daemon_client.h
 * @brief IPC client of the MavlinkDaemon inside an application process: one connection (unix
 * SOCK_SEQPACKET socket, `daemon_ipc.h`), instance registration (`RegisterInstance`), dispatch
 * of daemon messages to instances, reconnection and re-registration after a daemon restart.
 *
 * Model: an IO thread connects to the daemon (a try every `reconnect_interval`), registers ALL
 * attached instances and receives messages. A broken connection makes instances lose their
 * registration (method calls fail at once) and register again after reconnecting. An application
 * restart is seen by the daemon as a closed socket, and it cleans up the registrations.
 *
 * A send that times out because the daemon buffer is full does NOT drop the connection (that
 * would lose every registration of the process); only a socket error does.
 */
#ifndef ARA_COM_MAVLINK_CLIENT_DAEMON_CLIENT_H_
#define ARA_COM_MAVLINK_CLIENT_DAEMON_CLIENT_H_

#include <atomic>
#include <chrono>  // NOLINT
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>  // NOLINT
#include <optional>
#include <string>
#include <thread>  // NOLINT

#include "ara/com/mavlink/client/mavlink_endpoint.h"
#include "ara/com/mavlink/daemon_ipc.h"
#include "ara/com/mavlink/daemon_protocol.h"
#include "ara/com/mavlink/mavlink_log.h"
#include "ara/core/model/mavlink_model_item.h"
#include "ara/core/result.h"

namespace ara {
namespace com {
namespace mavlink {

class DaemonClient {
 public:
  struct Options {
    /// Daemon socket path (default `daemon::kDefaultSocketPath`).
    std::string socket_path{daemon::kDefaultSocketPath};
    /// Interval of connection attempts.
    std::chrono::milliseconds reconnect_interval{500};
    /// Retry of a registration rejected for an instance conflict (e.g. after a fast app restart).
    std::chrono::milliseconds register_retry{2000};
    /// Interval of stop checks and retries while the socket is silent.
    std::uint32_t poll_ms{100};
    std::uint32_t send_timeout_ms{200};
    LogFn log;
  };

  DaemonClient();
  explicit DaemonClient(Options opts);
  ~DaemonClient();
  DaemonClient(const DaemonClient&) = delete;
  DaemonClient& operator=(const DaemonClient&) = delete;

  /// The process client (bindings). Socket: env `SRP_MAVLINK_DAEMON_SOCKET`, otherwise the
  /// default; logs through ara::log once it is initialised.
  static DaemonClient& Instance();

  /// Attaches a service instance and registers it in the daemon (at once when connected,
  /// otherwise after connecting). Never blocks on the daemon. The handlers are fixed for the life
  /// of the endpoint, so they are in place before the first message can arrive.
  std::shared_ptr<MavlinkEndpoint> AttachEndpoint(const ara::core::model::ModelMavlink& model,
                                                  bool skeleton,
                                                  MavlinkEndpoint::Handlers handlers = {});
  /// Unregisters an instance and waits for a handler in progress (do not call from a handler of
  /// that instance).
  void DetachEndpoint(const std::shared_ptr<MavlinkEndpoint>& ep);
  /// Detaches all instances, closes the connection and stops the IO thread (Deinitialize).
  void Shutdown();

  bool connected() const;

 private:
  friend class MavlinkEndpoint;
  enum class RegState { kIdle, kPending, kRegistered, kRejected };
  struct Entry {
    std::shared_ptr<MavlinkEndpoint> ep;
    std::optional<daemon::RegisterInstance> reg;  ///< nullopt = not registered
    RegState state{RegState::kIdle};
    daemon::RegisterStatus reject{daemon::RegisterStatus::kOk};
    std::chrono::steady_clock::time_point rejected_at;
    bool logged_reject{false};
  };

  /// Sends a message to the daemon (thread-safe); an error = not connected, or the socket broke,
  /// or the daemon buffer stayed full for `send_timeout_ms` (the connection is kept then).
  ara::core::Result<void> Send(const daemon::Message& m);
  bool IsConnected() const;
  void EnsureStarted();
  void IoLoop(std::stop_token st);
  bool ConnectOnce();
  void RegisterPending();
  void RegisterEntry(std::uint32_t handle);
  void Dispatch(daemon::Message m);
  void OnRegisterReply(const daemon::RegisterReply& r);
  void Disconnect();
  void RetryRejected();
  std::shared_ptr<MavlinkEndpoint> Find(std::uint32_t handle);
  void Log(LogLevel l, const std::string& m) const;

  Options opts_;
  mutable std::mutex mutex_;
  daemon::MessageSocket conn_;
  bool connected_{false};
  std::map<std::uint32_t, Entry> entries_;
  std::uint32_t next_handle_{1};
  std::jthread io_;
};

}  // namespace mavlink
}  // namespace com
}  // namespace ara

#endif  // ARA_COM_MAVLINK_CLIENT_DAEMON_CLIENT_H_
