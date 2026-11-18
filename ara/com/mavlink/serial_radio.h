/**
 * Copyright (c) 2024
 *
 * @file serial_radio.h
 * @brief Built-in radio driver `serial_radio`: MAVLink frames as raw bytes over a UART,
 * without AT commands or modem configuration.
 *
 * Driver config file (`driver_config` of the link), JSON:
 * @code
 *   { "device": "/dev/ttyS2", "baudrate": 57600 }
 * @endcode
 *
 * The driver thread waits on `poll()` (data / room to write). `Send()` is non-blocking:
 * no room in the UART buffer = `kBusy` (and `OnTxReady()` after POLLOUT); if the UART accepts
 * only part of a frame, the driver appends the rest on its own thread — for the daemon the
 * frame is then accepted as a whole, and further Send() calls return kBusy until the rest is
 * written. A plugin with the id `serial_radio` replaces this driver (logged).
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

/// Id of the built-in driver.
constexpr const char* kSerialRadioId = "serial_radio";

class SerialRadio final : public IRadioDriver {
 public:
  /// Explicit parameters (tests); `name` is for logs.
  SerialRadio(std::string name, std::string device, std::uint32_t baudrate);
  /// From a JSON file (factory); a file error only surfaces from Start() (kPersistent).
  explicit SerialRadio(const std::string& config_path);
  ~SerialRadio() override;

  ara::core::Result<void> Start(IRadioListener& listener, std::stop_token token) noexcept override;
  void Stop() noexcept override;
  ara::core::Result<void> Send(std::span<const std::uint8_t> frame) noexcept override;
  std::string Name() const noexcept override { return name_; }

 private:
  void Loop(std::stop_token caller, IRadioListener* listener);
  /// Appends the rest of the frame and calls OnTxReady() when owed; false = fatal error (reported).
  bool FlushTail(IRadioListener* listener);

  std::string name_;
  std::string config_path_;
  std::string device_;
  std::uint32_t baudrate_{0};
  bool configured_{true};
  std::string config_error_;

  srp::core::serial::SerialPort port_;
  std::mutex mutex_;                  // tx_tail_, tx_ready_owed_, running_
  std::vector<std::uint8_t> tx_tail_;  // rest of a partially written frame
  bool tx_ready_owed_{false};         // kBusy was returned: OnTxReady() must be called
  bool running_{false};
  std::mutex lifecycle_mutex_;        // Start/Stop
  std::stop_source stop_;
  std::thread thread_;
};

/// Factory of `serial_radio` (for `RadioDriverRegistry::AddBuiltin` or registration in a plugin).
RadioDriverFactory SerialRadioFactory();

}  // namespace mavlink
}  // namespace com
}  // namespace ara

#endif  // ARA_COM_MAVLINK_SERIAL_RADIO_H_
