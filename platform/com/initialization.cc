/**
 * @file initialization.cc
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-11-26
 *
 * @copyright Copyright (c) 2024
 *
 */
#include "platform/com/initialization.h"

#include <memory>

#include "bindings/com/ipc/ipc_controller.h"
#include "bindings/common/controller/controller.h"
#include "platform/core/result.h"
#include "platform/exec/em/execution_client.h"

namespace platform {
namespace com {
platform::core::Result<void> Initialize(const uint32_t& app_id) noexcept {
  auto& controller = srp::bindings::Controller::GetInstance(app_id);
  controller.AddHandler(
      srp::bindings::ControllerClient::kIPC,
      std::make_shared<srp::bindings::com::ipc::IpcController>());
  controller.AddHandler(
    srp::bindings::ControllerClient::kExec,
    platform::exec::ExecutionClient::GetInstance(app_id));
  controller.Init();
  return {};
}

platform::core::Result<void> Deinitialize() noexcept { return {}; }
}  // namespace com
}  // namespace platform
