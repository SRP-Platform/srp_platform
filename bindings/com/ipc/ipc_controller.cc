/**
 * @file ipc_controller.cc
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-02-21
 *
 * @copyright Copyright (c) 2025
 *
 */

#include "bindings/com/ipc/ipc_controller.h"

#include <string>
#include <utility>
#include <vector>

#include "bindings/log.h"
#include "platform/com/msg_type.h"

namespace srp {
namespace bindings {
namespace com {
namespace ipc {
std::shared_ptr<IpcController> IpcController::instance_{nullptr};

IpcController::IpcController(const uint32_t app_id) : app_id_{app_id} {}

void IpcController::HandleNewMsg(uint32_t pid,
                                 const std::vector<uint8_t>& payload) noexcept {
  srp::bindings::LogInfo() << "New IPC msg from " << pid
                           << " size: " << payload.size();
  const auto msg = srp::data::Convert<platform::com::IpcMsg>::Conv(payload);
  if (!msg.has_value()) {
    srp::bindings::LogError() << "Incorrect msg format";
    return;
  }
  const auto iter = this->handler_list.find(msg.value().session_id_);
  if (iter == this->handler_list.end()) {
    srp::bindings::LogError()
        << "Handler not found (" << msg.value().service_id_ << "/"
        << msg.value().session_id_ << ")";
    return;
  }
  iter->second->ProceedFrame(msg.value());
}

void IpcController::Init() noexcept {}

bool IpcController::TxCallback(const std::string& desc,
                               ::platform::com::IpcMsg&& msg) {
  ::platform::com::IpcMsg temp{std::move(msg)};
  temp.app_id_ = this->app_id_;
  const auto payload =
      srp::data::Convert2Vector<::platform::com::IpcMsg>::Conv(temp);
  return this->send_callback_to_(
      desc, payload, srp::bindings::ControllerClient::MsgType::kIPC);
}

std::shared_ptr<IpcController> IpcController::GetInstance(
    const uint32_t app_id) {
  if (!instance_) {
    instance_ = std::make_shared<IpcController>(app_id);
  }

  return instance_;
}
std::shared_ptr<IpcController> IpcController::GetInstance() {
  return instance_;
}
}  // namespace ipc
}  // namespace com
}  // namespace bindings
}  // namespace srp
