/**
 * Copyright (c) 2024
 *
 * @file daemon_client.h
 * @brief Klient IPC MavlinkDaemon w procesie aplikacji: jedno połączenie (gniazdo unix
 * SOCK_SEQPACKET, `daemon_ipc.h`), rejestracja instancji (`RegisterInstance`), rozdział
 * komunikatów daemona między instancje, ponowne połączenie i rejestracja po restarcie daemona.
 *
 * Model: wątek IO łączy się z daemonem (próba co `reconnect_interval`), rejestruje WSZYSTKIE
 * dołączone instancje i odbiera komunikaty. Zerwanie połączenia = instancje tracą rejestrację
 * (wywołania metod kończą się błędem od razu), po ponownym połączeniu rejestrują się od nowa.
 * Restart aplikacji: daemon widzi zamknięcie gniazda i sprząta rejestracje.
 */
#ifndef ARA_COM_MAVLINK_CLIENT_DAEMON_CLIENT_H_
#define ARA_COM_MAVLINK_CLIENT_DAEMON_CLIENT_H_

#include <atomic>
#include <chrono>  // NOLINT
#include <condition_variable>  // NOLINT
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
#include "ara/core/model/mavlink_model_item.h"
#include "ara/core/result.h"

namespace ara {
namespace com {
namespace mavlink {

enum class ClientLogLevel { kDebug, kInfo, kWarn, kError };
using ClientLogFn = std::function<void(ClientLogLevel, const std::string&)>;

class DaemonClient {
 public:
  struct Options {
    /// Ścieżka gniazda daemona (domyślnie `daemon::kDefaultSocketPath`).
    std::string socket_path{daemon::kDefaultSocketPath};
    /// Odstęp prób połączenia z daemonem.
    std::chrono::milliseconds reconnect_interval{500};
    /// Ponowienie rejestracji odrzuconej konfliktem instancji (np. po szybkim restarcie apki).
    std::chrono::milliseconds register_retry{2000};
    /// Odstęp sprawdzania zatrzymania i ponowień przy ciszy na gnieździe.
    std::uint32_t poll_ms{100};
    std::uint32_t send_timeout_ms{200};
    ClientLogFn log;
    /// TYLKO TESTY: wymusza początkowy `seq` każdego elementu (zamiast losowego, rev. 5 R2), żeby
    /// odtworzyć kolizję seq+payload po restarcie nadawcy. Produkcja: nie ustawiać.
    std::optional<std::uint16_t> test_initial_seq;
  };

  DaemonClient();
  explicit DaemonClient(Options opts);
  ~DaemonClient();
  DaemonClient(const DaemonClient&) = delete;
  DaemonClient& operator=(const DaemonClient&) = delete;

  /// Klient procesu (bindingi). Gniazdo: env `SRP_MAVLINK_DAEMON_SOCKET`, inaczej domyślne;
  /// log przez ara::log, gdy zainicjowany.
  static DaemonClient& Instance();

  /// Dołącza instancję usługi i rejestruje ją w daemonie (od razu, gdy połączony; inaczej po
  /// połączeniu). Nigdy nie blokuje na daemonie.
  std::shared_ptr<MavlinkEndpoint> AttachEndpoint(const ara::core::model::ModelMavlink& model,
                                                  bool skeleton);
  /// Wyrejestrowuje instancję i czeka na handler w toku (nie wołać z handlera instancji).
  void DetachEndpoint(const std::shared_ptr<MavlinkEndpoint>& ep);
  /// Odłącza wszystkie instancje, zamyka połączenie i zatrzymuje wątek IO (Deinitialize).
  void Shutdown();

  bool connected() const;
  /// Liczba udanych połączeń z daemonem (testy: wykrycie ponownego połączenia).
  std::uint64_t connects() const { return connects_.load(); }

 private:
  friend class MavlinkEndpoint;
  enum class RegState { kIdle, kPending, kRegistered, kRejected };
  struct Entry {
    std::shared_ptr<MavlinkEndpoint> ep;
    std::optional<daemon::RegisterInstance> reg;  ///< nullopt = nie rejestrujemy
    RegState state{RegState::kIdle};
    daemon::RegisterStatus reject{daemon::RegisterStatus::kOk};
    std::chrono::steady_clock::time_point rejected_at;
    bool logged_reject{false};
  };

  /// Wysyła komunikat do daemona (wątkowo bezpieczne); błąd = brak połączenia albo zerwane.
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
  void Log(ClientLogLevel l, const std::string& m) const;

  Options opts_;
  mutable std::mutex mutex_;
  daemon::MessageSocket conn_;
  bool connected_{false};
  std::map<std::uint32_t, Entry> entries_;
  std::uint32_t next_handle_{1};
  std::jthread io_;
  std::mutex wake_mutex_;
  std::condition_variable_any wake_cv_;
  std::atomic<std::uint64_t> connects_{0};
};

}  // namespace mavlink
}  // namespace com
}  // namespace ara

#endif  // ARA_COM_MAVLINK_CLIENT_DAEMON_CLIENT_H_
