/**
 * @file handler.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-03-28
 *
 * @copyright Copyright (c) 2025
 *
 */

#ifndef BINDINGS_COM_IPC_HANDLER_H_
#define BINDINGS_COM_IPC_HANDLER_H_

#include <cstdint>
#include <functional>
#include <string>
#include <utility>

#include "platform/com/msg_type.h"

namespace srp {
namespace bindings {
namespace com {
namespace ipc {
class Handler {
 protected:
  const std::uint16_t session_id_;
  using Callback_t = std::function<bool(const std::string& desc,
                                        ::platform::com::IpcMsg&& msg)>;
  Callback_t callback_;

 public:
  void SetCallback(Callback_t&& callback) { callback_ = std::move(callback); }
  virtual void ProceedFrame(const platform::com::IpcMsg& msg) noexcept = 0;
  explicit Handler(const std::uint16_t id) : session_id_{id} {}
  virtual ~Handler() = default;
};
}  // namespace ipc
}  // namespace com
}  // namespace bindings
}  // namespace srp
#endif  // BINDINGS_COM_IPC_HANDLER_H_
