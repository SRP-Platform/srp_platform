/**
 * Copyright (c) 2024
 *
 * @file serial_port.h
 * @brief Serial port (termios raw 8N1, no flow control) for Linux.
 *
 * Low-level driver with no ara::* dependencies. One object = one device; the
 * same object can be reopened after Close().
 *
 * All I/O is non-blocking: TryRead()/TryWrite() return immediately and Poll()
 * is the only call that waits. Poll() is meant to be called from one thread at
 * a time; TryRead()/TryWrite()/Kick()/Close() may be called from any thread.
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
  kOk,       ///< bytes > 0 processed
  kTimeout,  ///< nothing ready (would block); bytes == 0
  kClosed,   ///< Close() or stop_token
  kError     ///< device lost / I/O error (errno in IoResult::err)
};

struct IoResult {
  IoStatus status{IoStatus::kError};
  std::size_t bytes{0};
  int err{0};
};

/// Result of Poll(): what is ready. `status` is kTimeout when nothing became ready in time.
struct PollResult {
  IoStatus status{IoStatus::kError};  ///< kOk/kTimeout/kClosed/kError
  bool readable{false};
  bool writable{false};  ///< set only when `want_write`
  bool kicked{false};    ///< woken by Kick() (flag cleared by Poll)
  int err{0};
};

struct OpenResult {
  bool ok{false};
  int err{0};           ///< errno
  bool busy{false};     ///< port in use (TIOCEXCL / flock)
  std::string message;  ///< human-readable error description
};

class SerialPort {
 public:
  SerialPort() = default;
  ~SerialPort();
  SerialPort(const SerialPort&) = delete;
  SerialPort& operator=(const SerialPort&) = delete;

  /// Opens the device exclusively (TIOCEXCL + flock). Baud rates: 1200..921600.
  OpenResult Open(const std::string& device, std::uint32_t baudrate);

  /// One read attempt without waiting. kOk: `bytes` > 0; kTimeout: no data;
  /// kError: device lost; kClosed: port closed.
  IoResult TryRead(std::uint8_t* buf, std::size_t len);

  /// One write attempt without waiting. kOk: `bytes` > 0 (may be < len, partial write);
  /// kTimeout: buffer full, bytes == 0 (EAGAIN); kClosed / kError as in TryRead().
  IoResult TryWrite(const std::uint8_t* data, std::size_t len);

  /// Waits up to `timeout_ms` for readable data (always), room to write (when `want_write`),
  /// Kick(), Close() or the stop token. Device error/loss -> kError.
  PollResult Poll(bool want_write, std::uint32_t timeout_ms, std::stop_token token);

  /// Thread-safe: wakes a blocked Poll() (e.g. when the awaited events change).
  void Kick();

  /// Thread-safe, idempotent; wakes a blocked Poll() in another thread.
  void Close();
  bool IsOpen() const;

  /// Whether the baud rate is supported.
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
