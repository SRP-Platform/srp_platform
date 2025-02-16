/**
 * @file Method.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-01-12
 *
 * @copyright Copyright (c) 2025
 *
 */

#include "ara/com/proxy/method.h"

#include <atomic>
#include <chrono>  // NOLINT
#include <condition_variable>  // NOLINT
#include <mutex>  // NOLINT
#include <string>
#include <utility>
#include <vector>

#include "ara/com/message_error_domain.h"
#include "ara/com/proxy/packet_interpreter.h"
#include "ara/com/types.h"

namespace ara {
namespace com {
namespace proxy {

void Method::ProceedPacket(const ara::com::IpcMsg &msg) noexcept {
  switch (msg.msg_type_) {
    case ara::com::IpcMsgType::kResponse:
      this->ProceedResponse(msg.payload_);
      this->msg_rx = true;
      this->cv_.notify_one();
      break;
    case ara::com::IpcMsgType::kError:
      if (msg.payload_.size() > 0) {
        this->ProceedError(msg.payload_.at(0));
      } else {
        this->ProceedError(
            ara::com::internal::MessageErrorDomain::Errc::kENotOk);
      }
      this->msg_rx = true;
      this->cv_.notify_one();
      break;
    case ara::com::IpcMsgType::kRequestAck:
      if (ara::com::model::kNoReturnWithAck == this->mode_) {
        this->ProceedAck();
        this->msg_rx = true;
        this->cv_.notify_one();
      }
      break;
    default:
      break;
  }
}
void Method::SendRequest(const std::vector<uint8_t> &payload,
                         const bool no_blocking) noexcept {
  IpcMsg msg{
      ara::com::IpcMsgType::kRequest,        0x00U,  0x00U, 0x00U, 0x00U, 0x00U,
      static_cast<uint32_t>(payload.size()), payload};
  const auto no_return =
      ara::com::model::EndpointMode::kNoReturn == this->mode_;
  if (no_return) {
    msg.msg_type_ = ara::com::IpcMsgType::kRequestNoReturn;
  }
  if (ara::com::model::EndpointMode::kNoReturnWithAck == this->mode_) {
    msg.msg_type_ = ara::com::IpcMsgType::kRequestNoReturnAck;
  }
  if (!this->TransmitPacket(std::move(msg))) {
    this->ProceedError(
        ara::com::internal::MessageErrorDomain::Errc::kENotReachable);
    return;
  }
  if (no_return) {
    this->ProceedAck();
    return;
  }
  if (!no_blocking) {
    std::unique_lock lk(m_);
    if (!cv_.wait_for(lk, Method::timeout, [this] { return msg_rx.load(); })) {
      this->ProceedError(
          ara::com::internal::MessageErrorDomain::Errc::kETimeout);
    }
  }
  msg_rx = false;
}
Method::Method(const std::string &name,
               interpreter::ProxyPacketInterpreter &handler) noexcept
    : interpreter::PacketInterpreter(name, handler),
      m_{},
      cv_{},
      msg_rx{false} {}

Method::~Method() noexcept {}

}  // namespace proxy
}  // namespace com
}  // namespace ara
