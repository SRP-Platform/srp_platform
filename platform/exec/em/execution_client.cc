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

#include <memory>
#include <utility>
#include "platform/exec/em/execution_client.h"
#include "bindings/common/socket/ipc_socket.h"
#include "srp/platform/em/ExecutionHeader.h"
#include "platform/com/com_error_domain.h"
#include "platform/exec/em/i_execution_client.h"
#include "platform/log/log.h"
namespace platform {
namespace exec {
namespace {
static std::shared_ptr<ExecutionClient> instance_;
}

platform::core::Result<void> ExecutionClient::ReportExecutionState(const ExecutionState state) const noexcept {
    srp::platform::em::ExecutionHeader hdr_;
    hdr_.execution_state = state;
    hdr_.app_id = this->app_id_;
    platform::log::LogWarn() << "App with id:" << app_id_ << ", migrate to state:" << get_string(state);
    auto buf = srp::data::Convert2Vector<srp::platform::em::ExecutionHeader>::Conv(hdr_);
    if (!this->send_callback_to_) {
        return platform::com::MakeErrorCode(platform::com::ComErrc::kErroneousFileHandle, "Callback Not Set");
    }
    if (this->send_callback_to_("ARA.EXEC", buf, srp::bindings::ControllerClient::MsgType::kExec)) {
        return {};
    }
    return platform::com::MakeErrorCode(platform::com::ComErrc::kCommunicationStackError, "Failed send data");
}
ExecutionClient::ExecutionClient() {
}

std::shared_ptr<ExecutionClient> ExecutionClient::GetInstance(const uint32_t& app_id) noexcept {
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
void ExecutionClient::HandleNewMsg(uint32_t pid,
    const std::vector<uint8_t>& payload) noexcept {
        return;
}
}  // namespace exec
}  // namespace platform
