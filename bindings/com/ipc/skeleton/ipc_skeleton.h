/**
 * @file ipc_skeleton.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief 
 * @version 0.1
 * @date 2025-03-28
 * 
 * @copyright Copyright (c) 2025
 * 
 */
#ifndef BINDINGS_COM_IPC_SKELETON_IPC_SKELETON_H_
#define BINDINGS_COM_IPC_SKELETON_IPC_SKELETON_H_

#include <memory>
#include <mutex>   // NOLINT
#include <thread>  // NOLINT
#include <vector>

#include "bindings/com/ipc/skeleton/ipc_skeleton_sd.h"
#include "bindings/common/socket/proccess_socket.h"
#include "core/common/wait_queue.h"
#include "platform/com/com_wrapper.h"
#include "platform/com/msg_type.h"

namespace srp {
namespace bindings {
namespace com {
namespace ipc {
namespace skeleton {
class IpcSkeleton final : public ::platform::com::wrapper::ComWrapper {
 private:
  const uint32_t service_id_;
  const uint32_t instance_id_;
  std::jthread skeleton_thread_;
  std::mutex tx_mutex_;
  std::unique_ptr<bindings::com::ProccessSocket> sock_;
  ::srp::core::WaitQueue<::platform::com::IpcMsg> rx_qeueue_;
  IpcSkeletonSd sd_;
  void RxCallback(const uint32_t app_id, const std::vector<uint8_t>& payload);
  void Loop(std::stop_token token);

 public:
  void ProceedFrame(const platform::com::IpcMsg& msg) noexcept override;
  bool TransmitFrame(const platform::com::IpcMsg& msg) noexcept override;
  void Release() override;
  ::platform::core::Result<void> Start() override;
  IpcSkeleton(const uint32_t& service_id, const uint32_t& instance_id);
  ~IpcSkeleton() { skeleton_thread_.request_stop(); }
};

}  // namespace skeleton
}  // namespace ipc
}  // namespace com
}  // namespace bindings
}  // namespace srp

#endif  // BINDINGS_COM_IPC_SKELETON_IPC_SKELETON_H_
