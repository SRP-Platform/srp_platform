/**
 * @file serial_transport.h
 * @brief Gotowy IByteTransport dla portu szeregowego (rejestrowany w kodzie) (core/serial, termios raw).
 * @copyright Copyright (c) 2024
 */
#ifndef ARA_COM_MAVLINK_SERIAL_TRANSPORT_H_
#define ARA_COM_MAVLINK_SERIAL_TRANSPORT_H_

#include <cstdint>
#include <string>
#include <utility>

#include "ara/com/mavlink/i_byte_transport.h"
#include "core/serial/serial_port.h"

namespace ara {
namespace com {
namespace mavlink {

class SerialTransport final : public IByteTransport {
 public:
  SerialTransport(std::string link_name, std::string device,
                  std::uint32_t baudrate)
      : link_name_{std::move(link_name)},
        device_{std::move(device)},
        baudrate_{baudrate} {}

  ara::core::Result<void> Open(std::stop_token token) noexcept override;
  ara::core::Result<std::size_t> Read(std::span<std::uint8_t> buffer,
                                      std::uint32_t timeout_ms,
                                      std::stop_token token) noexcept override;
  ara::core::Result<std::size_t> Write(std::span<const std::uint8_t> data,
                                       std::uint32_t timeout_ms,
                                       std::stop_token token) noexcept override;
  void Close() noexcept override;
  std::string Name() const noexcept override { return "serial:" + link_name_; }

 private:
  const std::string link_name_;
  const std::string device_;
  const std::uint32_t baudrate_;
  srp::core::serial::SerialPort port_;
};

}  // namespace mavlink
}  // namespace com
}  // namespace ara

#endif  // ARA_COM_MAVLINK_SERIAL_TRANSPORT_H_
