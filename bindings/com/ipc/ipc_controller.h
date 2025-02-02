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

#include <memory>
#include <mutex>  // NOLINT
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "bindings/com/ipc/handler.h"
#include "bindings/common/controller/controller_client.h"
#include "platform/com/msg_type.h"

namespace srp {
namespace bindings {
namespace com {
namespace ipc {
class IpcController : public ControllerClient {
 private:
  std::mutex adding_mutex{};
  uint16_t current_session_id{0x0001U};
  std::unordered_map<uint16_t, std::shared_ptr<Handler>> handler_list{};
  static std::shared_ptr<IpcController> instance_;
  bool TxCallback(const std::string& desc, ::platform::com::IpcMsg&& msg);
  const uint32_t app_id_;

 public:
  explicit IpcController(const uint32_t app_id);
  ~IpcController() = default;

  void HandleNewMsg(uint32_t pid,
                    const std::vector<uint8_t>& payload) noexcept override;

  template <class T, typename... Args>
  std::shared_ptr<T> CreateHandler(Args&&... args) {
    static_assert(std::is_base_of<Handler, T>::value);
    std::lock_guard<std::mutex> lk(adding_mutex);
    auto temp =
        std::make_shared<T>(current_session_id, std::forward<Args>(args)...);
    this->handler_list.insert({current_session_id, temp});
    temp->SetCallback(std::bind(&IpcController::TxCallback, this,
                                std::placeholders::_1, std::placeholders::_2));
    current_session_id++;
    return temp;
  }
  void Init() noexcept override;

  static std::shared_ptr<IpcController> GetInstance(const uint32_t app_id);
  static std::shared_ptr<IpcController> GetInstance();
};
}  // namespace ipc
}  // namespace com
}  // namespace bindings
}  // namespace srp

#endif  // BINDINGS_COM_IPC_IPC_CONTROLLER_H_
