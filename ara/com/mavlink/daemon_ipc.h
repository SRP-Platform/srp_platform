/**
 * Copyright (c) 2024
 *
 * @file daemon_ipc.h
 * @brief Unix SOCK_SEQPACKET socket carrying the `daemon_protocol.h` messages: the server
 * side (daemon) and the client side (application). No ara::log dependency.
 *
 * Concurrency: `Send()` may be called from many threads; `Receive()` from one.
 * `Shutdown()` is thread-safe and wakes a blocked `Receive()`/`Send()`.
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
  kMessage,        ///< a message was received
  kTimeout,        ///< time elapsed
  kClosed,         ///< the peer closed the connection or `Shutdown()` was called
  kProtocolError   ///< the packet is not a valid message (the connection should be closed)
};

struct RecvResult {
  RecvStatus status{RecvStatus::kClosed};
  std::optional<Message> message;
};

/// A connected socket end. Copies share the same socket (`ara::core::Result` requires
/// copyability); the socket is closed when the last copy goes away.
/// Whether a Send() error only means that the peer buffer stayed full for the whole timeout (the
/// connection itself may be fine, unlike a broken socket).
bool IsSendTimeout(const ara::core::ErrorCode& e);

class MessageSocket {
 public:
  MessageSocket() = default;
  explicit MessageSocket(int fd);

  /// Client: connects to the daemon at `path`. An error (no daemon) is returned at once.
  static ara::core::Result<MessageSocket> Connect(const std::string& path);

  bool IsOpen() const { return impl_ != nullptr; }

  /// Sends a message; waits for room in the buffer at most `timeout_ms` (slow peer), then
  /// fails. A full buffer is not a broken connection: only the caller decides to close it.
  ara::core::Result<void> Send(const Message& m, std::uint32_t timeout_ms = 1000);

  RecvResult Receive(std::uint32_t timeout_ms);

  /// Thread-safe, idempotent: closes the connection (the peer sees EOF).
  void Shutdown();

 private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};

/// Server: a listening socket at a path (removed on close).
class MessageServer {
 public:
  MessageServer() = default;
  ~MessageServer();
  MessageServer(const MessageServer&) = delete;
  MessageServer& operator=(const MessageServer&) = delete;

  /// Creates the socket (replacing a stale file), `mode` = permissions of the socket file. Fails
  /// when another server holds the flock on `<path>.lock`.
  ara::core::Result<void> Listen(const std::string& path, mode_t mode = 0660);
  /// Waits up to `timeout_ms` for a connection; nullopt = none.
  ara::core::Result<std::optional<MessageSocket>> Accept(std::uint32_t timeout_ms);
  void Close();
  const std::string& path() const { return path_; }

 private:
  int fd_{-1};
  int lock_fd_{-1};  // flock on <path>.lock for the server lifetime
  std::string path_;
};

}  // namespace daemon
}  // namespace mavlink
}  // namespace com
}  // namespace ara

#endif  // ARA_COM_MAVLINK_DAEMON_IPC_H_
