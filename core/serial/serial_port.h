/**
 * Copyright (c) 2024
 *
 * @file serial_port.h
 * @brief Port szeregowy (termios raw 8N1, bez flow control) dla Linuksa.
 *
 * Niskopoziomowy sterownik bez zależności od ara::*. Jeden obiekt = jedno
 * urządzenie; ten sam obiekt można otworzyć ponownie po Close().
 *
 * Współbieżność: Read() i Write() wolno wołać równolegle z dwóch wątków,
 * Close() jest thread-safe i budzi zablokowane Read()/Write().
 *
 */
#ifndef CORE_SERIAL_SERIAL_PORT_H_
#define CORE_SERIAL_SERIAL_PORT_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>  // NOLINT
#include <stop_token>  // NOLINT
#include <string>

namespace srp {
namespace core {
namespace serial {

enum class IoStatus {
  kOk,       ///< bytes > 0 przetworzone
  kTimeout,  ///< minął czas; bytes może być > 0 (zapis częściowy)
  kClosed,   ///< Close() albo stop_token
  kError     ///< utrata urządzenia / błąd I/O (errno w IoResult::err)
};

struct IoResult {
  IoStatus status{IoStatus::kError};
  std::size_t bytes{0};
  int err{0};
};

struct OpenResult {
  bool ok{false};
  int err{0};           ///< errno
  bool busy{false};     ///< port zajęty (TIOCEXCL / flock)
  std::string message;  ///< czytelny opis błędu
};

class SerialPort {
 public:
  SerialPort() = default;
  ~SerialPort();
  SerialPort(const SerialPort&) = delete;
  SerialPort& operator=(const SerialPort&) = delete;

  /// Otwiera urządzenie na wyłączność (TIOCEXCL + flock). Baudy: 1200..921600.
  OpenResult Open(const std::string& device, std::uint32_t baudrate);

  /// timeout_ms == 0: tylko sprawdza, co jest gotowe.
  IoResult Read(std::uint8_t* buf, std::size_t len, std::uint32_t timeout_ms,
                std::stop_token token);
  /// Zapis może być częściowy (kTimeout z bytes < len).
  IoResult Write(const std::uint8_t* data, std::size_t len,
                 std::uint32_t timeout_ms, std::stop_token token);

  /// Thread-safe, idempotentne; odblokowuje Read()/Write() innych wątków.
  void Close();
  bool IsOpen() const;

  /// Czy częstotliwość jest obsługiwana.
  static bool IsBaudrateSupported(std::uint32_t baudrate);

 private:
  struct State;
  std::shared_ptr<State> Acquire() const;

  mutable std::mutex mutex_;
  std::shared_ptr<State> state_;
};

}  // namespace serial
}  // namespace core
}  // namespace srp

#endif  // CORE_SERIAL_SERIAL_PORT_H_
