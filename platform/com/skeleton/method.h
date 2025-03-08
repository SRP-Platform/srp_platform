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

#include <vector>

#include "platform/com/message_error_domain.h"
#include "platform/com/msg_type.h"
#include "platform/com/types.h"
#include "platform/core/result.h"

namespace platform {
namespace com {
namespace skeleton {
class Method {
 protected:
  void ProceedPacket(const platform::com::IpcMsg &msg) noexcept;
  virtual platform::core::Result<std::vector<uint8_t>> ProceedRequest(
      const std::vector<uint8_t> &payload) noexcept = 0;

 public:
  Method(/* args */);
  ~Method();
};
}  // namespace skeleton
}  // namespace com
}  // namespace platform
#endif  // PLATFORM_COM_SKELETON_METHOD_H_
