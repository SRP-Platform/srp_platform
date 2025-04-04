/**
 * @file Isocket_stream.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-05-08
 *
 * @copyright Copyright (c) 2024
 *
 */
#ifndef BINDINGS_COMMON_SOCKET_ISOCKET_STREAM_H_
#define BINDINGS_COMMON_SOCKET_ISOCKET_STREAM_H_

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "platform/core/result.h"
namespace srp {
namespace bindings {
namespace com {
namespace soc {
using RXCallbackStream =
    std::function<std::vector<uint8_t>(const std::vector<uint8_t>&)>;
class ISocketStream {
 public:
  virtual ::platform::core::Result<void> Init(const std::string& socket_path) = 0;
  /**
   * @brief Setter for rx callback
   *
   * @pplatformm callback
   */
  virtual void SetRXCallback(RXCallbackStream&& callback) = 0;

  virtual std::optional<std::vector<uint8_t>> Transmit(
      const std::string& socket_path, std::vector<std::uint8_t> payload) = 0;
  /**
   * @brief This function start RX thread
   *
   */
  virtual void StartRXThread() = 0;
  virtual void StopRXThread() {}
  virtual ~ISocketStream() {}
};

}  // namespace soc
}  // namespace com
}  // namespace bindings
}  // namespace srp

#endif  // BINDINGS_COMMON_SOCKET_ISOCKET_STREAM_H_
