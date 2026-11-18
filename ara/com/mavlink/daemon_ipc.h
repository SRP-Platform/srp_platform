/**
 * Copyright (c) 2024
 *
 * @file daemon_ipc.h
 * @brief Gniazdo unix SOCK_SEQPACKET niosące komunikaty `daemon_protocol.h`:
 * strona serwera (daemon) i klienta (aplikacja). Bez zależności od ara::log.
 *
 * Współbieżność: `Send()` wolno wołać z wielu wątków; `Receive()` z jednego.
 * `Shutdown()` jest thread-safe i budzi zablokowany `Receive()`/`Send()`.
 */
#ifndef ARA_COM_MAVLINK_DAEMON_IPC_H_
#define ARA_COM_MAVLINK_DAEMON_IPC_H_

#include <sys/types.h>

#include <atomic>
#include <memory>
#include <mutex>  // NOLINT
#include <optional>
#include <string>

#include "ara/com/mavlink/daemon_protocol.h"
#include "ara/core/result.h"

namespace ara {
namespace com {
namespace mavlink {
namespace daemon {

enum class RecvStatus {
  kMessage,        ///< odebrano komunikat
  kTimeout,        ///< minął czas
  kClosed,         ///< druga strona zamknęła połączenie albo `Shutdown()`
  kProtocolError   ///< pakiet nie jest poprawnym komunikatem (połączenie do zamknięcia)
};

struct RecvResult {
  RecvStatus status{RecvStatus::kClosed};
  std::optional<Message> message;
};

/// Połączony koniec gniazda. Kopie współdzielą to samo gniazdo (wymóg kopiowalności
/// `ara::core::Result`); gniazdo zamyka się, gdy zniknie ostatnia kopia.
class MessageSocket {
 public:
  MessageSocket() = default;
  explicit MessageSocket(int fd);

  /// Klient: łączy się z daemonem pod `path`. Błąd (brak daemona) = natychmiast.
  static ara::core::Result<MessageSocket> Connect(const std::string& path);

  bool IsOpen() const { return impl_ != nullptr; }

  /// Wysyła komunikat; czeka na miejsce w buforze najwyżej `timeout_ms`
  /// (wolna druga strona), po czym błąd. Błąd = połączenie do zamknięcia.
  ara::core::Result<void> Send(const Message& m, std::uint32_t timeout_ms = 1000);

  RecvResult Receive(std::uint32_t timeout_ms);

  /// Thread-safe, idempotentne: zamyka połączenie (druga strona widzi EOF).
  void Shutdown();

 private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};

/// Serwer: gniazdo nasłuchujące pod ścieżką (usuwana przy zamknięciu).
class MessageServer {
 public:
  MessageServer() = default;
  ~MessageServer();
  MessageServer(const MessageServer&) = delete;
  MessageServer& operator=(const MessageServer&) = delete;

  /// Tworzy gniazdo (usuwa nieaktualny plik), `mode` = prawa pliku gniazda.
  ara::core::Result<void> Listen(const std::string& path, mode_t mode = 0660);
  /// Czeka na połączenie do `timeout_ms`; nullopt = brak.
  ara::core::Result<std::optional<MessageSocket>> Accept(std::uint32_t timeout_ms);
  void Close();
  const std::string& path() const { return path_; }

 private:
  int fd_{-1};
  std::string path_;
};

}  // namespace daemon
}  // namespace mavlink
}  // namespace com
}  // namespace ara

#endif  // ARA_COM_MAVLINK_DAEMON_IPC_H_
