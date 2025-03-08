/**
 * @file com_controller.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-11-26
 *
 * @copyright Copyright (c) 2024
 *
 */
#ifndef PLATFORM_COM_COM_CONTROLLER_H_
#define PLATFORM_COM_COM_CONTROLLER_H_

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "platform/com/i_com_client.h"
#include "bindings/common/socket/proccess_socket.h"
#include "platform/core/result.h"

namespace platform {
namespace com {
class ComController {
 private:
  std::unordered_map<IComClient::MsgType, std::shared_ptr<IComClient>>
      handlers_{};
  const uint32_t app_id_;

  std::unique_ptr<srp::bindings::com::ProccessSocket> ipc_soc_;
  void IpcRxCallback(const uint32_t pid, const std::vector<uint8_t>& payload);
  bool SendCallback(uint32_t pid, const std::vector<uint8_t>& payload,
                    IComClient::MsgType type);
  bool SendCallbackTo(const std::string& desc,
                      const std::vector<uint8_t>& payload,
                      IComClient::MsgType type);

 public:
  explicit ComController(const uint32_t& app_id);
  platform::core::Result<void> Init() noexcept;
  bool AddHandler(IComClient::MsgType msg_type,
                  std::shared_ptr<IComClient> handler_);
  static ComController& GetInstance() noexcept;
  static ComController& GetInstance(const uint32_t app_id) noexcept;
  ~ComController();
};

}  // namespace com
}  // namespace platform

#endif  // PLATFORM_COM_COM_CONTROLLER_H_
