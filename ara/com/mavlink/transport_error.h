/**
 * @file transport_error.h
 * @brief Kody błędów transportu bajtów MAVLink (domena ara::core::ErrorDomain).
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
  /// Błąd przejściowy: link ponawia po przerwie, w logu Warn.
  kRetryable = 1,
  /// Błąd trwały (np. brak urządzenia, port zajęty): Error w logu raz,
  /// ponowienia dalej co 30 s.
  kPersistent = 2,
  /// Transport zamknięty (Close()) albo przerwany stop_tokenem.
  kClosed = 3,
  /// Rejestr: nieznana nazwa łącza.
  kUnknownLink = 4,
  /// Rejestr: nieprawidłowy argument (np. nullptr).
  kInvalidArgument = 5,
  /// Rejestr: transport dla tej nazwy już zarejestrowany.
  kAlreadyRegistered = 6
};

const ara::core::ErrorDomain& GetTransportErrorDomain() noexcept;

ara::core::ErrorCode MakeTransportError(
    TransportErrc code, ara::core::ErrorDomain::SupportDataType data = {});

/// Czy `e` pochodzi z domeny transportu i ma dany kod.
bool IsTransportError(const ara::core::ErrorCode& e, TransportErrc code) noexcept;

}  // namespace mavlink
}  // namespace com
}  // namespace ara

#endif  // ARA_COM_MAVLINK_TRANSPORT_ERROR_H_
