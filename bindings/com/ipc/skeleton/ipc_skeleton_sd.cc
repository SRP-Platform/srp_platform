/**
 * @file ipc_skeleton_sd.cc
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief 
 * @version 0.1
 * @date 2025-03-28
 * 
 * @copyright Copyright (c) 2025
 * 
 */
#include "bindings/com/ipc/skeleton/ipc_skeleton_sd.h"

#include "bindings/com/ipc/com_thread_pool.h"
#include "bindings/com/ipc/skeleton/sd_event.h"
#include "bindings/log.h"

namespace srp {
namespace bindings {
namespace com {
namespace ipc {
namespace skeleton {
IpcSkeletonSd::IpcSkeletonSd(const uint32_t service_id,
                             const uint32_t instance_id,
                             ::platform::com::wrapper::ComWrapper& wrapper)
    : service_id_{service_id},
      instance_id_{instance_id},
      wrapper_{wrapper},
      endpoint_list_{} {}

bool IpcSkeletonSd::ProceedFrame(const platform::com::IpcMsg& msg) noexcept {
  if (::platform::com::IpcMsgType::kObjInit == msg.msg_type_) {
    this->CreatEntry(msg.method_id_, msg.payload_[0]);
    return true;
  } else if (::platform::com::IpcMsgType::kEvent == msg.msg_type_) {
    this->UpdateEntry(msg.method_id_, msg.payload_);
    return true;
  } else if (::platform::com::IpcMsgType::kSubscribe == msg.msg_type_) {
    this->Subscribe(msg.method_id_, msg.app_id_, msg.session_id_);
    return true;
  }
  return true;
}

void IpcSkeletonSd::UpdateEntry(const uint32_t endpoint_id,
                                const std::vector<uint8_t>& new_value) {
  const auto iter = this->endpoint_list_.find(endpoint_id);
  if (iter != this->endpoint_list_.end()) {
    const auto ptr = iter->second;
    ComThreadPool::GetInstance().Post(
        [this, new_value, ptr]() { ptr->Update(new_value); });
  }
}

void IpcSkeletonSd::Subscribe(const uint32_t endpoint_id, const uint32_t app_id,
                              const uint32_t session) {
  const auto iter = this->endpoint_list_.find(endpoint_id);
  if (iter != this->endpoint_list_.end()) {
    const auto ptr = iter->second;
    ComThreadPool::GetInstance().Post(
        [this, app_id, session, ptr]() { ptr->Subscribe(app_id, session); });
  }
}

void IpcSkeletonSd::CreatEntry(const uint32_t endpoint_id, const uint8_t type) {
  switch (type) {
    case 0x01U:
      this->Create<SdEvent>(endpoint_id);
      break;

    default:
      break;
  }
}

IpcSkeletonSd::~IpcSkeletonSd() {}
}  // namespace skeleton
}  // namespace ipc
}  // namespace com
}  // namespace bindings
}  // namespace srp
