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

#include <vector>

namespace srp {
namespace bindings {
namespace com {
namespace ipc {

IpcController::IpcController(/* args */) {}

void IpcController::HandleNewMsg(uint32_t pid,
                                 const std::vector<uint8_t>& payload) noexcept {
}

void IpcController::Init() noexcept {}

}  // namespace ipc
}  // namespace com
}  // namespace bindings
}  // namespace srp
