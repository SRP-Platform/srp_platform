/**
 * @file sd_item.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief 
 * @version 0.1
 * @date 2025-03-28
 * 
 * @copyright Copyright (c) 2025
 * 
 */
#ifndef BINDINGS_COM_IPC_SKELETON_SD_ITEM_H_
#define BINDINGS_COM_IPC_SKELETON_SD_ITEM_H_

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

#include "platform/com/msg_type.h"

namespace srp {
namespace bindings {
namespace com {
namespace ipc {
namespace skeleton {
class SdItem {
 public:
  using TxCallback = std::function<bool(const ::platform::com::IpcMsg&)>;

 protected:
  TxCallback callback_;

 public:
  virtual void Update(const std::vector<uint8_t> value) noexcept = 0;
  virtual void Subscribe(const uint32_t app_id, const uint16_t session_id) = 0;

  void SetCallback(TxCallback&& callback) { callback_ = std::move(callback); }
  virtual ~SdItem() = default;
};
}  // namespace skeleton
}  // namespace ipc
}  // namespace com
}  // namespace bindings
}  // namespace srp
#endif  // BINDINGS_COM_IPC_SKELETON_SD_ITEM_H_
