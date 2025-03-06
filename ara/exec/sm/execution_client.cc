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
#include "ara/exec/sm/execution_client.h"
#include "bindings/common/socket/ipc_socket.h"
#include "srp/platform/em/ExecutionHeader.h"
#include "ara/com/i_com_client.h"
namespace ara {
namespace exec {

static std::shared_ptr<ExecutionClient> instance_;

ara::core::Result<void> ExecutionClient::ReportExecutionState(ExecutionState state) const noexcept {
    srp::platform::em::ExecutionHeader hdr_;
    hdr_.execution_state = state;
    hdr_.app_id = 1; // TODO how to read that
    auto buf = srp::data::Convert2Vector<srp::platform::em::ExecutionHeader>::Conv(hdr_);
    if (!this->send_callback_to_) {
        return ara::com::MakeErrorCode(com::someip::MessageCode::kEOk, "Callback Not Set");
    }
    if (this->send_callback_to_("ARA.EXEC", buf, com::IComClient::MsgType::kExec)) {
        return {};
    }
    return ara::com::MakeErrorCode(com::someip::MessageCode::kEOk, "Failed send data");
}
ExecutionClient::ExecutionClient() {
    this->ReportExecutionState(ExecutionState::kIdle);
}

std::shared_ptr<ExecutionClient> ExecutionClient::GetInstance() noexcept {
    if (instance_ == nullptr) {
      instance_ = std::make_shared<ExecutionClient>();
    }
    return instance_;
}
void ExecutionClient::HandleNewMsg(uint32_t pid,
    const std::vector<uint8_t>& payload) {
        return;
}
}
}