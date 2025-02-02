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

#include "platform/com/skeleton/method.h"

#include <string>
#include <utility>
#include <vector>

#include "platform/com/message_error_domain.h"
#include "platform/com/msg_type.h"
#include "platform/com/types.h"
#include "platform/core/result.h"

namespace platform {
namespace com {
namespace skeleton {
Method::Method(const std::string& endpoint_name,
               interpreter::SkeletonPacketInterpreter& handler)
    : MethodPacketInterpreter{endpoint_name, handler} {}

void Method::ProceedPacket(const platform::com::IpcMsg& msg) noexcept {
  if (platform::com::IpcMsgType::kRequest != msg.msg_type_) {
    return;
  }
  if (this->request_.valid()) {
    if (this->request_.wait_for(std::chrono::seconds{10}) ==
        std::future_status::timeout) {
      platform::com::IpcMsg res_msg{
          platform::com::IpcMsgType::kError,
          0,
          0,
          endpoint_id_,
          msg.app_id_,
          msg.session_id_,
          1,
          {platform::com::internal::MessageErrorCode::kENotReady}};
      this->TransmitPacket(std::move(res_msg));
      return;
    }
    this->request_.get();
  }
  this->request_ = std::async(std::launch::async, &Method::LocalProceedPacket,
                              this, std::move(msg));
}
void Method::LocalProceedPacket(const platform::com::IpcMsg&& msg) noexcept {
  const auto res = this->ProceedRequest(msg.payload_);
  if (!res.HasValue()) {
    platform::com::IpcMsg res_msg{platform::com::IpcMsgType::kError,
                                  0,
                                  0,
                                  0,
                                  msg.app_id_,
                                  msg.session_id_,
                                  1,
                                  {static_cast<uint8_t>(res.Error().Value())}};
    this->TransmitPacket(std::move(res_msg));
    return;
  }
  const auto vec = res.Value();
  platform::com::IpcMsg res_msg{platform::com::IpcMsgType::kResponse,
                                0,
                                0,
                                0,
                                msg.app_id_,
                                msg.session_id_,
                                static_cast<uint32_t>(vec.size()),
                                vec};
  this->TransmitPacket(std::move(res_msg));
}
}  // namespace skeleton
}  // namespace com
}  // namespace platform
