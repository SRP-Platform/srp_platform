/**
 * @file method.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-02-25
 *
 * @copyright Copyright (c) 2025
 *
 */
#ifndef PLATFORM_COM_SKELETON_METHOD_H_
#define PLATFORM_COM_SKELETON_METHOD_H_

#include <future>  // NOLINT
#include <string>
#include <vector>

#include "platform/com/message_error_domain.h"
#include "platform/com/msg_type.h"
#include "platform/com/skeleton/method_packet_interpreter.h"
#include "platform/com/types.h"
#include "platform/core/result.h"

namespace platform {
namespace com {
namespace skeleton {
class Method : public interpreter::MethodPacketInterpreter {
 protected:
  void ProceedPacket(const platform::com::IpcMsg& msg) noexcept override;
  virtual platform::core::Result<std::vector<uint8_t>> ProceedRequest(
      const std::vector<uint8_t>& payload) noexcept = 0;
  void LocalProceedPacket(const platform::com::IpcMsg&& msg) noexcept;
  std::future<void> request_;

 public:
  Method(const std::string& endpoint_name,
         interpreter::SkeletonPacketInterpreter& handler);  // NOLINT
  virtual ~Method() = default;
};
}  // namespace skeleton
}  // namespace com
}  // namespace platform
#endif  // PLATFORM_COM_SKELETON_METHOD_H_
