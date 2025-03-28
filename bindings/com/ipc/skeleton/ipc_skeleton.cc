/**
 * @file ipc_skeleton.cc
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief 
 * @version 0.1
 * @date 2025-03-28
 * 
 * @copyright Copyright (c) 2025
 * 
 */
#include "bindings/com/ipc/skeleton/ipc_skeleton.h"

#include <string>
#include <utility>

#include "bindings/log.h"

namespace srp {
namespace bindings {
namespace com {
namespace ipc {
namespace skeleton {
void IpcSkeleton::RxCallback(const uint32_t app_id,
                             const std::vector<uint8_t>& payload) {
  const auto res = srp::data::Convert<::platform::com::IpcMsg>::Conv(
      std::vector<uint8_t>{payload.begin() + 1, payload.end()});
  if (res.has_value()) {
    ::srp::bindings::LogInfo() << "[Skeleton] " << "New msg from: " << app_id
                               << " / " << res.value().app_id_;
    this->rx_qeueue_.push(res.value());
  }
}

void IpcSkeleton::ProceedFrame(const platform::com::IpcMsg& msg) noexcept {}

bool IpcSkeleton::TransmitFrame(const platform::com::IpcMsg& msg) noexcept {
  if ((msg.msg_type_ == ::platform::com::IpcMsgType::kObjInit) ||
      (msg.msg_type_ == ::platform::com::IpcMsgType::kEvent &&
       msg.app_id_ == 0U)) {
    return sd_.ProceedFrame(msg);
  } else {
    // std::unique_lock<std::mutex> lk(tx_mutex_);
    ::srp::bindings::LogInfo()
        << "[Skeleton] " << "Sending msg to: " << msg.app_id_;
    const auto temp =
        srp::data::Convert2Vector<platform::com::IpcMsg>::Conv(msg);
    std::vector<uint8_t> payload{0x01};
    payload.insert(payload.end(), temp.begin(), temp.end());
    const auto status =
        this->sock_->TransmitToPid(msg.app_id_, payload).HasValue();
    ::srp::bindings::LogInfo() << "[Skeleton] Tx status: " << status;
    return status;
  }
}

void IpcSkeleton::Release() { this->sock_->StopOffer(); }
::platform::core::Result<void> IpcSkeleton::Start() {
  this->sock_ = std::make_unique<bindings::com::ProccessSocket>(
      "s-" + std::to_string(static_cast<uint64_t>(
                 (static_cast<uint64_t>(this->service_id_) << 32) +
                 this->instance_id_)));
  this->sock_->SetCallback(std::bind(&IpcSkeleton::RxCallback, this,
                                     std::placeholders::_1,
                                     std::placeholders::_2));
  ::srp::bindings::LogInfo()
      << "[Skeleton] " << "[IPC] Offering -> s-"
      << std::to_string(static_cast<uint64_t>(
             (static_cast<uint64_t>(this->service_id_) << 32) +
             this->instance_id_));
  return this->sock_->Offer();
}

void IpcSkeleton::Loop(std::stop_token token) {
  while (!token.stop_requested()) {
    const auto msg = this->rx_qeueue_.Get(token);
    if (msg.has_value()) {
      if ((msg.value().msg_type_ == ::platform::com::IpcMsgType::kSubscribe)) {
        sd_.ProceedFrame(msg.value());
      } else {
        this->callback_(std::move(msg.value()));
      }
    }
  }
  ::srp::bindings::LogInfo() << "[Skeleton] Loop exit";
}

IpcSkeleton::IpcSkeleton(const uint32_t& service_id,
                         const uint32_t& instance_id)
    : service_id_{service_id},
      instance_id_{instance_id},
      skeleton_thread_{[this](std::stop_token token) { this->Loop(token); }},
      sd_{service_id, instance_id, *this} {}

}  // namespace skeleton
}  // namespace ipc
}  // namespace com
}  // namespace bindings
}  // namespace srp
