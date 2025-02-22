/**
 * @file controller.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-02-21
 *
 * @copyright Copyright (c) 2025
 *
 */

#ifndef BINDINGS_COMMON_CONTROLLER_CONTROLLER_H
#define BINDINGS_COMMON_CONTROLLER_CONTROLLER_H

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "bindings/common/controller/controller_client.h"
#include "ara/core/result.h"
#include "bindings/common/socket/proccess_socket.h"

namespace srp {
namespace bindings {
class Controller {
 private:
  std::unordered_map<ControllerClient::MsgType, std::shared_ptr<ControllerClient>>
      handlers_{};
  const uint32_t app_id_;

  std::unique_ptr<srp::bindings::com::ProccessSocket> ipc_soc_;
  void IpcRxCallback(const uint32_t pid, const std::vector<uint8_t>& payload);
  bool SendCallback(uint32_t pid, const std::vector<uint8_t>& payload,
                    ControllerClient::MsgType type);
  bool SendCallbackTo(const std::string& desc,
                      const std::vector<uint8_t>& payload,
                      ControllerClient::MsgType type);

 public:
  explicit Controller(const uint32_t& app_id);
  ara::core::Result<void> Init() noexcept;
  bool AddHandler(ControllerClient::MsgType msg_type,
                  std::shared_ptr<ControllerClient> handler_);
  static Controller& GetInstance() noexcept;
  static Controller& GetInstance(const uint32_t app_id) noexcept;
  ~Controller();
};
}  // namespace bindings
}  // namespace srp

#endif  // BINDINGS_COMMON_CONTROLLER_CONTROLLER_H
