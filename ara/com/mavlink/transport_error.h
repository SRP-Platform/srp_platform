/**
 * @file transport_error.h
 * @brief Error codes of the MAVLink radio transport (an ara::core::ErrorDomain).
 * Code values are part of the plugin ABI; removed codes leave gaps.
 * @copyright Copyright (c) 2024
 */
#ifndef ARA_COM_MAVLINK_TRANSPORT_ERROR_H_
#define ARA_COM_MAVLINK_TRANSPORT_ERROR_H_

#include <string>

#include "ara/core/error_code.h"
#include "ara/core/error_domain.h"

namespace ara {
namespace com {
namespace mavlink {

enum class TransportErrc : ara::core::ErrorDomain::CodeType {
  /// Transient error: the link retries after a pause, logged as Warn.
  kRetryable = 1,
  /// Persistent error (e.g. missing device, port in use): logged as Error once,
  /// retries continue every 30 s.
  kPersistent = 2,
  /// Transport closed (Close()) or interrupted by a stop token.
  kClosed = 3,
  /// Invalid argument.
  kInvalidArgument = 5,
  /// IRadioDriver::Send: the radio has no room for the frame (nothing written); wait for
  /// IRadioListener::OnTxReady.
  kBusy = 7,
  /// Driver plugin loading: bad plugin / API version / duplicate id / unknown driver.
  kDriverRegistryError = 8
};

const ara::core::ErrorDomain& GetTransportErrorDomain() noexcept;

ara::core::ErrorCode MakeTransportError(
    TransportErrc code, ara::core::ErrorDomain::SupportDataType data = {});

/// Whether `e` belongs to the transport domain and carries the given code.
bool IsTransportError(const ara::core::ErrorCode& e, TransportErrc code) noexcept;

}  // namespace mavlink
}  // namespace com
}  // namespace ara

#endif  // ARA_COM_MAVLINK_TRANSPORT_ERROR_H_
