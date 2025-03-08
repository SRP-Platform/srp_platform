/**
 * @file Method.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-01-12
 *
 * @copyright Copyright (c) 2025
 *
 */
#ifndef PLATFORM_COM_PROXY_METHOD_H_
#define PLATFORM_COM_PROXY_METHOD_H_

#include <atomic>
#include <chrono>              // NOLINT
#include <condition_variable>  // NOLINT
#include <mutex>               // NOLINT
#include <string>
#include <vector>

#include "platform/com/message_error_domain.h"
#include "platform/com/proxy/packet_interpreter.h"
#include "platform/com/types.h"

namespace platform {
namespace com {
namespace proxy {

using namespace std::chrono_literals; // NOLINT

class Method : protected platform::com::proxy::interpreter::PacketInterpreter {
 private:
  std::mutex m_;
  std::condition_variable cv_;
  std::atomic<bool> msg_rx;
  static constexpr auto timeout = 2000ms;

 protected:
  void ProceedPacket(const platform::com::IpcMsg &msg) noexcept override;
  virtual void ProceedResponse(
      const std::vector<uint8_t> &payload) noexcept = 0;
  virtual void ProceedAck() noexcept = 0;
  virtual void ProceedError(const uint8_t error_code) noexcept = 0;
  void SendRequest(const std::vector<uint8_t> &payload,
                   const bool no_blocking = false) noexcept;

 public:
  Method(const std::string &name,
         interpreter::ProxyPacketInterpreter &handler) noexcept;  // NOLINT

  explicit Method(Method &&) = delete;
  explicit Method(Method &) = delete;
  Method operator=(Method &) = delete;
  Method operator=(Method &&) = delete;

  ~Method() noexcept;
};
}  // namespace proxy
}  // namespace com
}  // namespace platform

#endif  // PLATFORM_COM_PROXY_METHOD_H_
