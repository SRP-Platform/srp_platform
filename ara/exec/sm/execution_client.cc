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
namespace ara {
namespace exec {
namespace {
    constexpr auto kIpcPath = "";
}
ara::core::Result<void> ExecutionClient::ReportExecutionState(ExecutionState state) const noexcept {
    srp::platform::em::ExecutionHeader hdr_;
    hdr_.app_id = this->app_id_;
    hdr_.execution_state = state;
    auto buf = srp::data::Convert2Vector<srp::platform::em::ExecutionHeader>::Conv(hdr_);
    return sock_->Transmit(kIpcPath, buf);
}
ExecutionClient::ExecutionClient(const uint32_t& app_id, std::unique_ptr<ISocket> sock):
                                                        app_id_(app_id), sock_(std::move(sock)) {
    this->ReportExecutionState(ExecutionState::kIdle);
}
}
}
