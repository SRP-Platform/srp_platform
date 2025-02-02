/**
 * @file ipc_skeleton_sd.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief 
 * @version 0.1
 * @date 2025-03-28
 * 
 * @copyright Copyright (c) 2025
 * 
 */
#ifndef BINDINGS_COM_IPC_SKELETON_IPC_SKELETON_SD_H_
#define BINDINGS_COM_IPC_SKELETON_IPC_SKELETON_SD_H_

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "bindings/com/ipc/skeleton/sd_item.h"
#include "platform/com/com_wrapper.h"
#include "platform/com/msg_type.h"

namespace srp {
namespace bindings {
namespace com {
namespace ipc {
namespace skeleton {
class IpcSkeletonSd {
 private:
  const uint32_t service_id_;
  const uint32_t instance_id_;
  ::platform::com::wrapper::ComWrapper& wrapper_;
  std::unordered_map<uint32_t, std::shared_ptr<SdItem>> endpoint_list_;
  void CreatEntry(const uint32_t endpoint_id, const uint8_t type);
  void UpdateEntry(const uint32_t endpoint_id,
                   const std::vector<uint8_t>& new_value);
  void Subscribe(const uint32_t endpoint_id, const uint32_t app_id,
                 const uint32_t session);
  template <typename T>
  void Create(const uint32_t endpoint_id) {
    auto temp = std::make_shared<T>(service_id_, instance_id_, endpoint_id);
    temp->SetCallback([this](const ::platform::com::IpcMsg& msg) {
      return this->wrapper_.TransmitFrame(msg);
    });
    this->endpoint_list_.insert({endpoint_id, temp});
  }

 public:
  IpcSkeletonSd(const uint32_t service_id, const uint32_t instance_id,
                ::platform::com::wrapper::ComWrapper& wrapper);  // NOLINT
  bool ProceedFrame(const platform::com::IpcMsg& msg) noexcept;
  ~IpcSkeletonSd();
};
}  // namespace skeleton
}  // namespace ipc
}  // namespace com
}  // namespace bindings
}  // namespace srp

#endif  // BINDINGS_COM_IPC_SKELETON_IPC_SKELETON_SD_H_
