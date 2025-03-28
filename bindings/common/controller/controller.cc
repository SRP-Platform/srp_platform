/**
 * @file controller.cc
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-02-21
 *
 * @copyright Copyright (c) 2025
 *
 */
#include "bindings/common/controller/controller.h"

#include <memory>
#include <utility>

#include "bindings/common/log.h"
#include "platform/com/com_error_domain.h"

namespace srp {
namespace bindings {
namespace {
static std::shared_ptr<Controller> instance_;
}  // namespace

Controller::Controller(const uint32_t& app_id) : app_id_{app_id} {}

Controller::~Controller() {}

platform::core::Result<void> Controller::Init() noexcept {
  if (ipc_soc_ != nullptr) {
    return platform::com::MakeErrorCode(
        platform::com::ComErrc::kNetworkBindingFailure,
        "Controller already Started");
  }
  ipc_soc_ = std::make_unique<srp::bindings::com::ProccessSocket>(app_id_);

  ipc_soc_->SetCallback(std::bind(&Controller::IpcRxCallback, this,
                                  std::placeholders::_1,
                                  std::placeholders::_2));

  const auto res = ipc_soc_->Offer();
  if (!res.HasValue()) {
    bindings::log::LogFatal()
        << res.Error().Message() << ": " << res.Error().SupportData();
  }
  bindings::log::LogInfo() << "Controller started";
  for (auto& item : this->handlers_) {
    item.second->Init();
  }
  return {};
}
void Controller::IpcRxCallback(const uint32_t pid,
                               const std::vector<uint8_t>& payload) {
  bindings::log::LogDebug()
      << "New msg from: " << pid << " with payload: " << payload;
  const auto& item =
      handlers_.find(static_cast<ControllerClient::MsgType>(payload[0]));
  if (item != handlers_.end()) {
    item->second->HandleNewMsg(
        pid, std::vector<uint8_t>(payload.begin() + 1, payload.end()));
  } else {
    bindings::log::LogError()
        << "New msg for not supported handler type: " << payload[0];
  }
}

bool Controller::AddHandler(ControllerClient::MsgType msg_type,
                            std::shared_ptr<ControllerClient> handler_) {
  bindings::log::LogDebug() << "New Handler for: " << msg_type << " added";
  handler_->SetSendCallback(
      std::bind(&Controller::SendCallback, this, std::placeholders::_1,
                std::placeholders::_2, std::placeholders::_3),
      std::bind(&Controller::SendCallbackTo, this, std::placeholders::_1,
                std::placeholders::_2, std::placeholders::_3));
  const auto res = handlers_.insert({msg_type, handler_}).second;
  if (res && (nullptr != this->ipc_soc_)) {
    handler_->Init();
  }
  return res;
}
bool Controller::SendCallback(uint32_t pid, const std::vector<uint8_t>& payload,
                              ControllerClient::MsgType type) {
  std::vector<uint8_t> temp_payload{type};
  temp_payload.insert(temp_payload.end(), payload.begin(), payload.end());
  return this->ipc_soc_->TransmitToPid(pid, std::move(temp_payload)).HasValue();
}

bool Controller::SendCallbackTo(const std::string& desc,
                                const std::vector<uint8_t>& payload,
                                ControllerClient::MsgType type) {
  bindings::log::LogDebug() << "Sending to: " << desc;
  std::vector<uint8_t> temp_payload{type};
  temp_payload.insert(temp_payload.end(), payload.begin(), payload.end());
  if (ipc_soc_ != nullptr) {
    return this->ipc_soc_->Transmit(desc, temp_payload).HasValue();
  } else {
    bindings::log::LogError() << "Missing ipc sock!!";
    return false;
  }
}

Controller& Controller::GetInstance() noexcept { return *instance_; }

Controller& Controller::GetInstance(const uint32_t app_id) noexcept {
  if (nullptr == instance_) {
    instance_ = std::make_unique<Controller>(app_id);
  }
  return *instance_;
}
}  // namespace bindings
}  // namespace srp
