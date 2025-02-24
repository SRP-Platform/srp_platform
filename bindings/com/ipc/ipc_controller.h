/**
 * @file ipc_controller.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-02-21
 *
 * @copyright Copyright (c) 2025
 *
 */
#ifndef BINDINGS_COM_IPC_IPC_CONTROLLER_H_
#define BINDINGS_COM_IPC_IPC_CONTROLLER_H_

#include "bindings/common/controller/controller_client.h"

namespace srp {
namespace bindings {
namespace com {
namespace ipc {
class IpcController : public ControllerClient {
 private:
  /* data */
 public:
  IpcController(/* args */);
  ~IpcController() = default;

  void HandleNewMsg(uint32_t pid,
                    const std::vector<uint8_t>& payload) noexcept override;
  void Init() noexcept override;
};
}  // namespace ipc
}  // namespace com
}  // namespace bindings
}  // namespace srp

#endif  // BINDINGS_COM_IPC_IPC_CONTROLLER_H_
