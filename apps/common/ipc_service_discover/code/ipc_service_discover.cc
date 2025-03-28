/**
 * @file ipc_service_discover.cc
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-02-21
 *
 * @copyright Copyright (c) 2025
 *
 */
#include "apps/common/ipc_service_discover/code/ipc_service_discover.h"

#include <vector>

#include "core/common/condition.h"
#include "platform/log/log.h"

namespace srp {
namespace apps {
namespace com {
namespace ipc {
IpcServiceDiscover::IpcServiceDiscover()
    : sock_{std::make_unique<srp::bindings::com::ProccessSocket>(
          "SRP.COM.IPC-D")} {}
IpcServiceDiscover::~IpcServiceDiscover() {}

int IpcServiceDiscover::Initialize(
    const std::map<platform::core::StringView, platform::core::StringView>
        parms) {
  sock_->SetCallback(std::bind(&IpcServiceDiscover::ProceedWithMsg, this,
                               std::placeholders::_1, std::placeholders::_2));
  platform::log::LogInfo() << "App started";
  return 0;
}
void IpcServiceDiscover::ProceedWithMsg(const uint32_t pid,
                                        const std::vector<uint8_t>& data) {}

int IpcServiceDiscover::Run(const std::stop_token& token) {
  sock_->Offer();
  platform::log::LogInfo() << "App running";
  core::condition::wait(token);
  platform::log::LogInfo() << "App stopped";
  sock_->StopOffer();
  return 0;
}
}  // namespace ipc
}  // namespace com
}  // namespace apps
}  // namespace srp
