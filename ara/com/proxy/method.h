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
#ifndef ARA_COM_PROXY_METHOD_H_
#define ARA_COM_PROXY_METHOD_H_

#include <atomic>
#include <chrono>              // NOLINT
#include <condition_variable>  // NOLINT
#include <mutex>               // NOLINT
#include <string>
#include <vector>

#include "ara/com/message_error_domain.h"
#include "ara/com/proxy/packet_interpreter.h"
#include "ara/com/types.h"

namespace ara {
namespace com {
namespace proxy {

using namespace std::chrono_literals; // NOLINT

class Method : protected ara::com::proxy::interpreter::PacketInterpreter {
 private:
  std::mutex m_;
  std::condition_variable cv_;
  std::atomic<bool> msg_rx;
  static constexpr auto timeout = 2000ms;

 protected:
  void ProceedPacket(const ara::com::IpcMsg &msg) noexcept override;
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
}  // namespace ara

#endif  // ARA_COM_PROXY_METHOD_H_
