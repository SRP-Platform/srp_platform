/**
 * Copyright (c) 2024
 *
 * @file fake_daemon.h
 * @brief Atrapa MavlinkDaemon do testów klienta i bindingów: słucha na gnieździe, odpowiada na
 * `RegisterInstance` (domyślnie `kOk` + `LinkStatus{ready}` dla każdego łącza), a pozostałe
 * komunikaty aplikacji składa w kolejkę do odczytu w teście.
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

  /// Zaczyna nasłuchiwać (ponowny Start po Stop = „restart daemona”).
  bool Start();
  /// Zamyka nasłuch i wszystkie połączenia (aplikacje widzą zerwanie).
  void Stop();
  /// Zamyka połączenia, ale nasłuch zostaje.
  void DropConnections();

  /// Wynik odpowiedzi na kolejne rejestracje (domyślnie kOk).
  void SetRegisterStatus(daemon::RegisterStatus s) { status_ = s; }
  /// Czy po `kOk` wysyłać `LinkStatus{ready}` dla każdego łącza (domyślnie tak).
  void SetLinksReady(bool ready) { links_ready_ = ready; }

  /// Wysyła komunikat do ostatnio połączonej aplikacji.
  bool SendToApp(const daemon::Message& m);
  /// Następny komunikat aplikacji inny niż Register/Unregister; nullopt = timeout.
  std::optional<daemon::Message> WaitMessage(std::chrono::milliseconds timeout);
  /// Czeka, aż liczba odebranych `RegisterInstance` osiągnie `n` (łącznie od startu atrapy).
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
  std::atomic<std::size_t> unregistered_{0};
  std::atomic<std::size_t> connections_{0};
  bool running_{false};
};

}  // namespace testing
}  // namespace mavlink
}  // namespace com
}  // namespace ara

#endif  // ARA_COM_MAVLINK_CLIENT_TESTING_FAKE_DAEMON_H_
