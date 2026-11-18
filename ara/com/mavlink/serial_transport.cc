/**
 * @file serial_transport.cc
 * @copyright Copyright (c) 2024
 */
#include "ara/com/mavlink/serial_transport.h"

#include <cerrno>
#include <cstring>
#include <string>

#include "ara/com/mavlink/transport_error.h"

namespace ara {
namespace com {
namespace mavlink {
namespace {
using srp::core::serial::IoStatus;

TransportErrc ClassifyOpenError(int err, bool busy) {
  if (busy) {
    return TransportErrc::kPersistent;
  }
  switch (err) {
    case ENOENT:
    case ENODEV:
    case ENXIO:
    case EACCES:
    case EPERM:
    case EBUSY:
    case EINVAL:
    case ENOTTY:
    case EISDIR:
    case ENOTDIR:
    case ELOOP:
    case ENAMETOOLONG:
      return TransportErrc::kPersistent;
    default:
      return TransportErrc::kRetryable;
  }
}
}  // namespace

ara::core::Result<void> SerialTransport::Open(std::stop_token token) noexcept {
  if (token.stop_requested()) {
    return MakeTransportError(TransportErrc::kClosed, "Open przerwany");
  }
  const auto res = port_.Open(device_, baudrate_);
  if (!res.ok) {
    return MakeTransportError(ClassifyOpenError(res.err, res.busy), res.message);
  }
  return {};
}

ara::core::Result<std::size_t> SerialTransport::Read(
    std::span<std::uint8_t> buffer, std::uint32_t timeout_ms,
    std::stop_token token) noexcept {
  const auto r = port_.Read(buffer.data(), buffer.size(), timeout_ms, token);
  switch (r.status) {
    case IoStatus::kOk:
      return static_cast<std::size_t>(r.bytes);
    case IoStatus::kTimeout:
      return static_cast<std::size_t>(0);
    case IoStatus::kClosed:
      return MakeTransportError(TransportErrc::kClosed, Name() + ": zamknięty");
    case IoStatus::kError:
      break;
  }
  return MakeTransportError(
      TransportErrc::kPersistent,
      Name() + ": błąd odczytu: " + std::string{std::strerror(r.err)});
}

ara::core::Result<std::size_t> SerialTransport::Write(
    std::span<const std::uint8_t> data, std::uint32_t timeout_ms,
    std::stop_token token) noexcept {
  const auto r = port_.Write(data.data(), data.size(), timeout_ms, token);
  switch (r.status) {
    case IoStatus::kOk:
    case IoStatus::kTimeout:
      return static_cast<std::size_t>(r.bytes);
    case IoStatus::kClosed:
      if (r.bytes > 0) {
        return static_cast<std::size_t>(r.bytes);  // nie gubimy już zapisanych bajtów
      }
      return MakeTransportError(TransportErrc::kClosed, Name() + ": zamknięty");
    case IoStatus::kError:
      break;
  }
  return MakeTransportError(
      TransportErrc::kPersistent,
      Name() + ": błąd zapisu: " + std::string{std::strerror(r.err)});
}

void SerialTransport::Close() noexcept { port_.Close(); }

}  // namespace mavlink
}  // namespace com
}  // namespace ara
