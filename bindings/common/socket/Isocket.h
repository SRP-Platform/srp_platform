/**
 * @file Isocket.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2023-11-13
 *
 * @copyright Copyright (c) 2023
 *
 */
#ifndef COMMUNICATION_CORE_SOCKETS_ISOCKET_H_
#define COMMUNICATION_CORE_SOCKETS_ISOCKET_H_
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "ara/core/result.h"

namespace srp {
namespace bindings {
namespace com {
namespace soc {
using RXCallback = std::function<void(const std::vector<std::uint8_t>&)>;
class ISocket {
 public:
  /**
   * @brief
   *
   * @param socket_path
   * @return ara::core::Result<void>
   */
  virtual ara::core::Result<void> Init(const std::string& socket_path) = 0;
  /**
   * @brief Setter for rx callback
   *
   * @param callback
   */
  virtual void SetRXCallback(RXCallback callback) = 0;
  /**
   * @brief Function to send data by socket
   *
   * @param ip target ip or path
   * @param payload payload to send
   * @return core::ErrorCode status
   */
  virtual ara::core::Result<void> Transmit(
      const std::string& ip, std::vector<std::uint8_t> payload) = 0;
  /**
   * @brief This function start RX thread
   *
   */
  virtual void StartRXThread() = 0;
  virtual ~ISocket() = default;
  virtual void StopRXThread() {}
};

}  // namespace soc
}  // namespace com
}  // namespace bindings
}  // namespace srp
#endif  // COMMUNICATION_CORE_SOCKETS_ISOCKET_H_
