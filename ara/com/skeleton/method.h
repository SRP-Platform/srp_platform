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
#ifndef ARA_COM_SKELETON_METHOD_H_
#define ARA_COM_SKELETON_METHOD_H_

#include <vector>

#include "ara/com/message_error_domain.h"
#include "ara/com/msg_type.h"
#include "ara/com/types.h"
#include "ara/core/result.h"

namespace ara {
namespace com {
namespace skeleton {
class Method {
 protected:
  void ProceedPacket(const ara::com::IpcMsg &msg) noexcept;
  virtual ara::core::Result<std::vector<uint8_t>> ProceedRequest(
      const std::vector<uint8_t> &payload) noexcept = 0;

 public:
  Method(/* args */);
  ~Method();
};
}  // namespace skeleton
}  // namespace com
}  // namespace ara
#endif  // ARA_COM_SKELETON_METHOD_H_
