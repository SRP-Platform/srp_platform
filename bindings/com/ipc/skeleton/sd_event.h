/**
 * @file sd_event.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief 
 * @version 0.1
 * @date 2025-03-28
 * 
 * @copyright Copyright (c) 2025
 * 
 */
#ifndef BINDINGS_COM_IPC_SKELETON_SD_EVENT_H_
#define BINDINGS_COM_IPC_SKELETON_SD_EVENT_H_

#include <unordered_set>
#include <utility>
#include <vector>

#include "bindings/com/ipc/skeleton/sd_item.h"

namespace srp {
namespace bindings {
namespace com {
namespace ipc {
namespace skeleton {

class SdEvent final : public SdItem {
 private:
  const uint16_t service_id_;
  const uint16_t instance_id_;
  const uint16_t endpoint_id_;
  std::unordered_set<uint64_t> client_list_;

 public:
  void Update(const std::vector<uint8_t> value) noexcept override;
  void Subscribe(const uint32_t app_id, const uint16_t session_id) override;
  SdEvent(const uint16_t service_id, const uint16_t instance_id,
          const uint16_t endpoint_id);
  ~SdEvent() = default;
};

}  // namespace skeleton
}  // namespace ipc
}  // namespace com
}  // namespace bindings
}  // namespace srp

#endif  // BINDINGS_COM_IPC_SKELETON_SD_EVENT_H_
