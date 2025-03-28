/**
 * @file sd_event.cc
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-03-28
 *
 * @copyright Copyright (c) 2025
 *
 */
#include "bindings/com/ipc/skeleton/sd_event.h"

#include <vector>

#include "bindings/log.h"
#include "platform/com/msg_type.h"

namespace srp {
namespace bindings {
namespace com {
namespace ipc {
namespace skeleton {
void SdEvent::Update(const std::vector<uint8_t> value) noexcept {
  ::platform::com::IpcMsg msg{::platform::com::IpcMsgType::kEvent,
                              service_id_,
                              instance_id_,
                              endpoint_id_,
                              0U,
                              0U,
                              value.size(),
                              value};
  bindings::LogInfo() << "Data update";
  for (const auto& item : client_list_) {
    msg.app_id_ = static_cast<uint32_t>(item >> 32);
    msg.session_id_ = static_cast<uint32_t>(item);
    bindings::LogInfo() << "Data update sended to: " << msg.app_id_;
    std::ignore = this->callback_(msg);
  }
}

void SdEvent::Subscribe(const uint32_t app_id, const uint16_t session_id) {
  const uint64_t temp{(static_cast<uint64_t>(app_id) << 32) + session_id};
  if (client_list_.insert(temp).second) {
    ::platform::com::IpcMsg msg{::platform::com::IpcMsgType::kSubscribeAck,
                                service_id_,
                                instance_id_,
                                endpoint_id_,
                                app_id,
                                session_id,
                                0,
                                {}};
    std::ignore = this->callback_(msg);
  }
}

SdEvent::SdEvent(const uint16_t service_id, const uint16_t instance_id,
                 const uint16_t endpoint_id)
    : service_id_{service_id},
      instance_id_{instance_id},
      endpoint_id_{endpoint_id},
      client_list_{} {}

}  // namespace skeleton
}  // namespace ipc
}  // namespace com
}  // namespace bindings
}  // namespace srp
