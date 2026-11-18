/**
 * Copyright (c) 2024
 *
 * @file serial_radio.h
 * @brief Wbudowany driver radia `serial_radio`: ramki MAVLink jako surowe bajty po UART,
 * bez AT i konfiguracji modemu.
 *
 * Plik konfiguracji drivera (`driver_config` łącza), JSON:
 * @code
 *   { "device": "/dev/ttyS2", "baudrate": 57600 }
 * @endcode
 *
 * Wątek drivera czeka na `poll()` (dane / miejsce do zapisu). `Send()` jest nieblokujące:
 * brak miejsca w buforze UART = `kBusy` (i `OnTxReady()` po POLLOUT); jeśli UART przyjmie
 * tylko część ramki, driver dopisuje resztę w swoim wątku — dla daemona ramka jest wtedy
 * przyjęta w całości, a kolejne Send() zwracają kBusy do dopisania reszty.
 * Plugin o id `serial_radio` zastępuje ten driver (log).
 */
#ifndef ARA_COM_MAVLINK_SERIAL_RADIO_H_
#define ARA_COM_MAVLINK_SERIAL_RADIO_H_

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>  // NOLINT
#include <stop_token>  // NOLINT
#include <string>
#include <thread>  // NOLINT
#include <vector>

#include "ara/com/mavlink/radio_driver.h"
#include "core/serial/serial_port.h"

namespace ara {
namespace com {
namespace mavlink {

/// Id wbudowanego drivera.
constexpr const char* kSerialRadioId = "serial_radio";

class SerialRadio final : public IRadioDriver {
 public:
  /// Parametry wprost (testy); `name` do logów.
  SerialRadio(std::string name, std::string device, std::uint32_t baudrate);
  /// Z pliku JSON (fabryka); błąd pliku wychodzi dopiero z Start() (kPersistent).
  explicit SerialRadio(const std::string& config_path);
  ~SerialRadio() override;

  ara::core::Result<void> Start(IRadioListener& listener, std::stop_token token) noexcept override;
  void Stop() noexcept override;
  ara::core::Result<void> Send(std::span<const std::uint8_t> frame) noexcept override;
  std::string Name() const noexcept override { return name_; }

 private:
  void Loop(std::stop_token caller, IRadioListener* listener);
  /// Dopisuje resztę ramki i woła OnTxReady(), gdy należne; false = błąd fatalny (zgłoszony).
  bool FlushTail(IRadioListener* listener);

  std::string name_;
  std::string config_path_;
  std::string device_;
  std::uint32_t baudrate_{0};
  bool configured_{true};
  std::string config_error_;

  srp::core::serial::SerialPort port_;
  std::mutex mutex_;                  // tx_tail_, tx_ready_owed_, running_
  std::vector<std::uint8_t> tx_tail_;  // reszta częściowo zapisanej ramki
  bool tx_ready_owed_{false};         // zwrócono kBusy: trzeba zawołać OnTxReady()
  bool running_{false};
  std::mutex lifecycle_mutex_;        // Start/Stop
  std::stop_source stop_;
  std::thread thread_;
};

/// Fabryka `serial_radio` (do `RadioDriverRegistry::AddBuiltin` albo rejestracji w pluginie).
RadioDriverFactory SerialRadioFactory();

}  // namespace mavlink
}  // namespace com
}  // namespace ara

#endif  // ARA_COM_MAVLINK_SERIAL_RADIO_H_
