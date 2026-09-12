/**
 * @file execution_client.cc
 * @author Mateusz Krajewski (matikrajek42@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-02-26
 *
 * @copyright Copyright (c) 2025
 *
 */
#include "ara/exec/em/execution_client.h"

#include <memory>
#include <utility>
#include <vector>

#include "ara/com/com_error_domain.h"
#include "ara/exec/em/i_execution_client.h"
#include "ara/log/log.h"
#include "srp/platform/em/ExecutionHeader.h"

namespace ara {
namespace exec {
namespace {
static std::shared_ptr<ExecutionClient> instance_;
}

ara::core::Result<void> ExecutionClient::ReportExecutionState(
    const ExecutionState state) const noexcept {
  srp::platform::em::ExecutionHeader hdr_;
  hdr_.execution_state = state;
  hdr_.app_id = this->app_id_;
  ara::log::LogWarn() << "App with id:" << app_id_
                      << ", migrate to state:" << get_string(state);
  auto buf =
      srp::data::Convert2Vector<srp::platform::em::ExecutionHeader>::Conv(hdr_);
  if (!this->send_callback_to_) {
    return ara::com::MakeErrorCode(ara::com::ComErrc::kErroneousFileHandle,
                                   "Callback Not Set");
  }
  if (this->send_callback_to_("ARA.EXEC", buf,
                              ara::com::IComClient::MsgType::kExec)) {
    return {};
  }
  return ara::com::MakeErrorCode(ara::com::ComErrc::kCommunicationStackError,
                                 "Failed send data");
}

ExecutionClient::ExecutionClient() {}

std::shared_ptr<ExecutionClient> ExecutionClient::GetInstance(
    const uint32_t& app_id) noexcept {
  if (instance_ == nullptr) {
    instance_ = std::make_shared<ExecutionClient>();
  }
  instance_->app_id_ = app_id;
  return instance_;
}

std::shared_ptr<ExecutionClient> ExecutionClient::GetInstance() noexcept {
  if (instance_ == nullptr) {
    instance_ = std::make_shared<ExecutionClient>();
  }
  return instance_;
}

void ExecutionClient::HandleNewMsg(
    uint32_t pid, const std::vector<uint8_t>& payload) noexcept {
  return;
}
}  // namespace exec
}  // namespace ara
