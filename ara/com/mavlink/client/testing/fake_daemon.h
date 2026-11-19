/**
 * Copyright (c) 2024
 *
 * @file fake_daemon.h
 * @brief A fake MavlinkDaemon for the client and bindings tests: listens on the socket, answers
 * `RegisterInstance` (by default `kOk` + `LinkStatus{ready}` for every link) and queues the
 * remaining application messages for the test to read.
 */
#ifndef ARA_COM_MAVLINK_CLIENT_TESTING_FAKE_DAEMON_H_
#define ARA_COM_MAVLINK_CLIENT_TESTING_FAKE_DAEMON_H_

#include <atomic>
#include <chrono>  // NOLINT
#include <condition_variable>  // NOLINT
#include <deque>
#include <memory>
#include <mutex>  // NOLINT
#include <optional>
#include <string>
#include <thread>  // NOLINT
#include <vector>

#include "ara/com/mavlink/daemon_ipc.h"
#include "ara/com/mavlink/daemon_protocol.h"

namespace ara {
namespace com {
namespace mavlink {
namespace testing {

class FakeDaemon {
 public:
  explicit FakeDaemon(std::string path);
  ~FakeDaemon();

  /// Starts listening (Start again after Stop = a "daemon restart").
  bool Start();
  /// Closes the listener and all connections (applications see the break).
  void Stop();
  /// Closes the connections but keeps listening.
  void DropConnections();

  /// Status of the replies to the following registrations (default kOk).
  void SetRegisterStatus(daemon::RegisterStatus s) { status_ = s; }
  /// Whether to send `LinkStatus{ready}` for every link after `kOk` (default yes).
  void SetLinksReady(bool ready) { links_ready_ = ready; }

  /// While paused the fake stops reading its sockets, so the application's send buffer fills up.
  void SetReading(bool reading) { reading_ = reading; }

  /// Sends a message to the most recently connected application.
  bool SendToApp(const daemon::Message& m);
  /// Next application message other than Register/Unregister; nullopt = timeout.
  std::optional<daemon::Message> WaitMessage(std::chrono::milliseconds timeout);
  /// Waits until the number of received `RegisterInstance` messages reaches `n` (in total since
  /// the fake started).
  bool WaitRegistrations(std::size_t n, std::chrono::milliseconds timeout);

  std::vector<daemon::RegisterInstance> registrations() const;
  std::size_t unregistered() const { return unregistered_.load(); }
  std::size_t connections() const { return connections_.load(); }

 private:
  struct Conn {
    daemon::MessageSocket sock;
    std::jthread reader;
  };
  void AcceptLoop(std::stop_token st);
  void ReadLoop(std::stop_token st, daemon::MessageSocket sock);

  std::string path_;
  daemon::MessageServer server_;
  std::jthread acceptor_;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::vector<std::unique_ptr<Conn>> conns_;
  std::deque<daemon::Message> inbox_;
  std::vector<daemon::RegisterInstance> regs_;
  std::atomic<daemon::RegisterStatus> status_{daemon::RegisterStatus::kOk};
  std::atomic<bool> links_ready_{true};
  std::atomic<bool> reading_{true};
  std::atomic<std::size_t> unregistered_{0};
  std::atomic<std::size_t> connections_{0};
  bool running_{false};
};

}  // namespace testing
}  // namespace mavlink
}  // namespace com
}  // namespace ara

#endif  // ARA_COM_MAVLINK_CLIENT_TESTING_FAKE_DAEMON_H_
