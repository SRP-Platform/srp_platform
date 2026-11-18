/**
 * Copyright (c) 2024
 *
 * @file radio_driver.h
 * @brief API drivera radia MAVLink (rev. 10): IRadioDriver / IRadioListener, fabryka
 * driverów i interfejs pluginu `.so` (makro SRP_MAVLINK_DRIVER_PLUGIN).
 *
 * Platforma nie zna żadnego modemu. Driver to klasa z własnym wątkiem (czeka na
 * przerwanie AUX/UART przez `poll()`), którą MavlinkDaemon otwiera dla łącza z configu
 * (`links.<nazwa>.driver` = id z fabryki, `driver_config` = ścieżka do pliku drivera;
 * format zna tylko driver). Wbudowany driver `serial_radio` (serial_radio.h) jest
 * wzorcem; plugin o tym samym id go zastępuje.
 *
 * UWAGA: zmiana tego nagłówka = breaking change (implementacje żyją w repo srp) —
 * wyłącznie przez planistę/usera; wtedy trzeba podnieść `kRadioDriverApiVersion`.
 *
 * KONTRAKT (sprawdza go zestaw testów `radio_driver_contract_test`):
 *
 *  Start(listener, stop_token)
 *    Konfiguruje radio i uruchamia wątek drivera; może blokować (np. komendy AT), powinno
 *    reagować na stop_token. Wołane w wątku łącza daemona, nie w wątku drivera. Błąd =
 *    łącze ponawia po 30 s (kPersistent: Error w logu raz; kRetryable: Warn). Start() po
 *    Stop() na tym samym obiekcie MUSI działać. Po udanym Start() można wołać Send().
 *
 *  Stop()
 *    Zatrzymuje wątek drivera; PO POWROCIE ZERO CALLBACKÓW (żadne OnReceive/OnTxReady/
 *    OnError nie jest w toku ani nie nastąpi). Idempotentne, thread-safe względem Send().
 *    NIE wolno wołać Stop() z callbacku listenera (zakleszczenie na join wątku drivera).
 *
 *  Send(frame) — NIEBLOKUJĄCE
 *    Przyjmuje całą ramkę albo nic (bez zapisu częściowego widocznego dla daemona):
 *    sukces = ramka przyjęta do nadania; kBusy = nic nie przyjęto, daemon wstrzymuje ramkę
 *    do OnTxReady(); inny błąd = utrata urządzenia (kPersistent/kClosed) albo błąd
 *    przejściowy (kRetryable, ramka porzucona). Po kBusy driver MUSI zawołać OnTxReady()
 *    (raz), gdy znowu jest miejsce — także gdy kBusy zwrócono tuż przed zwolnieniem miejsca.
 *    Send() może być wołany z dowolnego wątku daemona (jeden naraz) i NIE wolno mu wołać
 *    callbacków listenera synchronnie (OnTxReady tylko z wątku drivera).
 *
 *  Callbacki listenera (z wątku drivera, nigdy z Send()/Start())
 *    OnReceive(bytes)   dowolny kawałek strumienia (też połowa ramki); daemon kopiuje i budzi
 *                       swój wątek RX, więc callback ma być krótki; bajty ważne tylko w jego trakcie;
 *    OnTxReady()        zwolniło się miejsce po kBusy;
 *    OnError(e, fatal)  fatal=true: radio nieużywalne -> daemon: Stop(), po 30 s Start().
 *                       fatal=false: informacja (daemon loguje, łącze działa).
 *
 *  Wymagania ogólne
 *    wirtualny destruktor; metody noexcept; żadnych wyjątków przez granicę interfejsu;
 *    Name() = stabilna nazwa do logów.
 */
#ifndef ARA_COM_MAVLINK_RADIO_DRIVER_H_
#define ARA_COM_MAVLINK_RADIO_DRIVER_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>  // NOLINT
#include <stop_token>  // NOLINT
#include <string>

#include "ara/com/mavlink/transport_error.h"
#include "ara/core/error_code.h"
#include "ara/core/result.h"

namespace ara {
namespace com {
namespace mavlink {

/// Implementuje daemon; wołane z wątku drivera.
class IRadioListener {
 public:
  virtual ~IRadioListener() = default;
  virtual void OnReceive(std::span<const std::uint8_t> bytes) noexcept = 0;
  virtual void OnTxReady() noexcept = 0;
  virtual void OnError(ara::core::ErrorCode e, bool fatal) noexcept = 0;
};

class IRadioDriver {
 public:
  virtual ~IRadioDriver() = default;
  virtual ara::core::Result<void> Start(IRadioListener& listener,  // NOLINT(runtime/references)
                                        std::stop_token token) noexcept = 0;
  virtual void Stop() noexcept = 0;
  virtual ara::core::Result<void> Send(std::span<const std::uint8_t> frame) noexcept = 0;
  virtual std::string Name() const noexcept = 0;
};

/// Wersja API pluginów (symbol `srp_mavlink_driver_api_version`); inna niż daemona = błąd startu.
constexpr std::uint32_t kRadioDriverApiVersion = 1;

/// Tworzy driver dla łącza; `config_path` = `driver_config` z configu daemona (może być
/// pusty). nullptr = driver nie może powstać (daemon: błąd startu z nazwą łącza).
using RadioDriverFactory =
    std::function<std::shared_ptr<IRadioDriver>(const std::string& config_path)>;

/// Rejestr, który daemon podaje pluginowi.
class IRadioDriverRegistry {
 public:
  virtual ~IRadioDriverRegistry() = default;
  /// Rejestruje fabrykę pod id. Błąd: pusty id/fabryka albo id zajęte przez inny plugin
  /// (duplikat = błąd startu daemona, nawet gdy plugin zignoruje wynik). Plugin o id wbudowanego
  /// drivera (`serial_radio`) go zastępuje.
  virtual ara::core::Result<void> Register(const std::string& id,
                                           RadioDriverFactory factory) noexcept = 0;
};

using DriverPluginRegisterFn = void (*)(IRadioDriverRegistry&);

constexpr const char* kPluginVersionSymbol = "srp_mavlink_driver_api_version";
constexpr const char* kPluginRegisterSymbol = "srp_mavlink_driver_register";

}  // namespace mavlink
}  // namespace com
}  // namespace ara

/// Deklaracja pluginu `.so`: `register_fn` to `void(ara::com::mavlink::IRadioDriverRegistry&)`,
/// woła `registry.Register("id", factory)` dla 1+ driverów. Użycie raz na bibliotekę (zwykle przez
/// regułę bazela `mavlink_driver_plugin`).
#define SRP_MAVLINK_DRIVER_PLUGIN(register_fn)                                              \
  extern "C" __attribute__((visibility("default"))) std::uint32_t                          \
  srp_mavlink_driver_api_version() {                                                       \
    return ::ara::com::mavlink::kRadioDriverApiVersion;                                    \
  }                                                                                        \
  extern "C" __attribute__((visibility("default"))) void srp_mavlink_driver_register(      \
      ::ara::com::mavlink::IRadioDriverRegistry* registry) {                               \
    register_fn(*registry);                                                                \
  }

#endif  // ARA_COM_MAVLINK_RADIO_DRIVER_H_
