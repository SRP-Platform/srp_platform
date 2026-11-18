/**
 * Copyright (c) 2024
 *
 * @file i_byte_transport.h
 * @brief Interfejs transportu bajtów łącza MAVLink (granica platforma / aplikacja).
 *
 * Platforma nie zna żadnego modemu: bajty każdego łącza dostarcza aplikacja
 * przez ::ara::com::mavlink::RegisterLinkTransport(). Gotowy port szeregowy to
 * klasa SerialTransport (tworzona i rejestrowana w kodzie).
 *
 * UWAGA: zmiana tego nagłówka = breaking change (implementacje żyją w repo srp)
 * — wyłącznie przez planistę/usera.
 *
 * KONTRAKT (sprawdza go wspólny zestaw testów `byte_transport_contract_test`):
 *
 *  Open(stop_token)
 *    Wołany w wątku łącza (nie w wątku aplikacji), może blokować (np. konfiguracja
 *    modemu). Dopóki nie zwróci sukcesu, link nic nie nadaje. Błąd -> link ponawia
 *    co 30 s (patrz kody błędów). Open() po Close() na tym samym obiekcie MUSI
 *    działać. Powinien reagować na stop_token.
 *
 *  Read(span, timeout_ms, stop_token) -> Result<size_t>
 *    Zwraca liczbę bajtów (>0), 0 = minął timeout (nie jest błędem). Utrata
 *    urządzenia = błąd. Po Close()/stop_token: błąd kClosed.
 *
 *  Write(span, timeout_ms, stop_token) -> Result<size_t>
 *    MOŻE ZWRÓCIĆ MNIEJ NIŻ data.size() (zapis częściowy po timeout) i MOŻE
 *    BLOKOWAĆ do timeout_ms. Link dopisuje resztę, a gdy timeout nastąpił w
 *    połowie ramki, porzuca resztę ramki (odbiorca policzy błąd CRC; licznik
 *    tx_partial) i następną ramkę wysyła w całości. Błąd = utrata urządzenia.
 *
 *  Współbieżność
 *    Read() i Write() wolno wołać równolegle z dwóch wątków. Dwa równoległe
 *    Read() (albo dwa Write()) nie są wspierane. Close() jest thread-safe,
 *    idempotentne i odblokowuje zablokowane Read()/Write().
 *
 *  Kody błędów (domena TransportErrorDomain, transport_error.h)
 *    kRetryable  — błąd przejściowy (w logu Warn);
 *    kPersistent — błąd trwały (Error w logu raz); ponowienia dalej co 30 s;
 *    kClosed     — transport zamknięty / przerwany.
 *
 *  Wymagania ogólne
 *    wirtualny destruktor; metody noexcept; żadnych wyjątków przez granicę
 *    interfejsu; Name() zwraca stabilną nazwę (do logów).
 *
 */
#ifndef ARA_COM_MAVLINK_I_BYTE_TRANSPORT_H_
#define ARA_COM_MAVLINK_I_BYTE_TRANSPORT_H_

#include <cstddef>
#include <cstdint>
#include <span>  // NOLINT
#include <stop_token>  // NOLINT
#include <string>

#include "ara/core/result.h"

namespace ara {
namespace com {
namespace mavlink {

class IByteTransport {
 public:
  virtual ~IByteTransport() = default;

  virtual ara::core::Result<void> Open(std::stop_token token) noexcept = 0;
  virtual ara::core::Result<std::size_t> Read(std::span<std::uint8_t> buffer,
                                              std::uint32_t timeout_ms,
                                              std::stop_token token) noexcept = 0;
  virtual ara::core::Result<std::size_t> Write(
      std::span<const std::uint8_t> data, std::uint32_t timeout_ms,
      std::stop_token token) noexcept = 0;
  virtual void Close() noexcept = 0;
  virtual std::string Name() const noexcept = 0;
};

}  // namespace mavlink
}  // namespace com
}  // namespace ara

#endif  // ARA_COM_MAVLINK_I_BYTE_TRANSPORT_H_
