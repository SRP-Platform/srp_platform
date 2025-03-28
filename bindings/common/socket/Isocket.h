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
#ifndef BINDINGS_COMMON_SOCKET_ISOCKET_H_
#define BINDINGS_COMMON_SOCKET_ISOCKET_H_
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "platform/core/result.h"

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
   * @pplatformm socket_path
   * @return platform::core::Result<void>
   */
  virtual platform::core::Result<void> Init(const std::string& socket_path) = 0;
  /**
   * @brief Setter for rx callback
   *
   * @pplatformm callback
   */
  virtual void SetRXCallback(RXCallback callback) = 0;
  /**
   * @brief Function to send data by socket
   *
   * @pplatformm ip target ip or path
   * @pplatformm payload payload to send
   * @return core::ErrorCode status
   */
  virtual platform::core::Result<void> Transmit(
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
#endif  // BINDINGS_COMMON_SOCKET_ISOCKET_H_
