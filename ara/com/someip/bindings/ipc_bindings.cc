/**
 * @file ipc_bindings.cc
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-09-15
 *
 * @copyright Copyright (c) 2024
 *
 */
#include "ara/com/someip/bindings/ipc_bindings.h"

#include <algorithm>
#include <array>
#include <chrono>  // NOLINT
#include <condition_variable>  // NOLINT
#include <future>  // NOLINT
#include <mutex>  // NOLINT
#include <tuple>
#include <memory>
#include <string>
#include <vector>

#include "ara/com/log.h"
#include "ara/com/someip/message_code.h"
#include "ara/com/someip/message_type.h"

namespace ara {
namespace com {
namespace someip {
namespace bindings {
IPCSkeletonBindings::IPCSkeletonBindings(const ara::core::StringView path)
    : main_path_{path},
buffor_{ara::core::InstanceSpecifier{"/" + path + "-buffor"}},
cv_{ara::core::InstanceSpecifier{"/" + path + "-cv"}} {}
void IPCSkeletonBindings::Start(std::stop_token token) {
  ara::com::LogInfo() << "IPCSkeletonBindings: " << "Started";
  buffor_.OfferService();
  cv_.Offer();
  stream_sock_ = std::make_unique<srp::bindings::com::soc::StreamIpcSocket>();
  stream_sock_->Init(main_path_);
  stream_sock_->SetRXCallback(
      std::bind(&IPCSkeletonBindings::RXCallback, this, std::placeholders::_1));
  stream_sock_->StartRXThread();
}

std::vector<uint8_t> IPCSkeletonBindings::RXCallback(
    std::vector<std::uint8_t> data) {
  ara::com::LogDebug() << "RX new frame size: "
                       << static_cast<uint8_t>(data.size());
  const auto& frame_r = ara::com::someip::SomeipFrame::MakeFrame(data);
  if (!frame_r.HasValue()) {
    ara::com::LogError() << "Parsing Error: " << frame_r.Error();
    ara::com::someip::HeaderStructure res{this->service_id_,
                                          0U,
                                          0U,
                                          0U,
                                          0U,
                                          1U,
                                          this->major_version_,
                                          MessageType::kError,
                                          MessageCode::kEMalformedMessage};
    return ara::com::someip::SomeipFrame::MakeFrame(res).GetRaw();
  }

  const auto& frame = frame_r.Value();
  if ((frame.header_.method_id & 0x8000) != 0) {
    ara::com::someip::HeaderStructure res{this->service_id_,
                                          frame.header_.method_id,
                                          0U,
                                          frame.header_.request_id,
                                          0U,
                                          1U,
                                          this->major_version_,
                                          MessageType::kError,
                                          MessageCode::kEUnknownMethod};
    return ara::com::someip::SomeipFrame::MakeFrame(res).GetRaw();
  }
  if (frame.header_.message_type == MessageType::kRequest &&
      method_req_with_response_) {
    const auto res_p = this->method_req_with_response_(frame.header_.method_id,
                                                       frame.payload_);
    if (res_p.HasValue()) {
      ara::com::someip::HeaderStructure res{this->service_id_,
                                            frame.header_.method_id,
                                            0U,
                                            frame.header_.request_id,
                                            0U,
                                            1U,
                                            this->major_version_,
                                            MessageType::kResponse,
                                            MessageCode::kEOk};
      return ara::com::someip::SomeipFrame::MakeFrame(res, res_p.Value())
          .GetRaw();
    }
  }
  ara::com::someip::HeaderStructure res{this->service_id_,
                                        frame.header_.method_id,
                                        0U,
                                        frame.header_.request_id,
                                        0U,
                                        1U,
                                        this->major_version_,
                                        MessageType::kError,
                                        MessageCode::kEUnknownMethod};
  return ara::com::someip::SomeipFrame::MakeFrame(res).GetRaw();
}

void IPCSkeletonBindings::Stop() {}

ara::core::Result<std::vector<uint8_t>> IPCSkeletonBindings::HandleMethod(
    const uint16_t& method_id, const std::vector<uint8_t>& payload) {
  return ara::com::MakeErrorCode(ara::com::ComErrc::kUnsetFailure,
                                 " Not supported");
}

void IPCSkeletonBindings::HandleEvent(const uint16_t& method_id,
                                      const std::vector<uint8_t>& payload) {
  ara::com::LogDebug() << "IPCSkeletonBindings: " << "SomeIp event send";
  HeaderStructure header_{this->service_id_,
                          method_id,
                          0U,
                          0U,
                          0U,
                          1U,
                          this->major_version_,
                          MessageType::kNotification,
                          MessageCode::kEOk};
  const auto frame = ara::com::someip::SomeipFrame::MakeFrame(header_, payload);
  const auto vec = frame.GetRaw();
  ara::com::LogDebug() << "IPCSkeletonBindings: " << "Vec size: "
                       << static_cast<uint8_t>(vec.size());
  std::copy(vec.begin(), vec.end(), buffor_.GetNewSamplesPointer() + 2);
  *buffor_.GetNewSamplesPointer() = (*buffor_.GetNewSamplesPointer()) + 1;
  *(buffor_.GetNewSamplesPointer() + 1) = static_cast<uint8_t>(vec.size());
  cv_.NotifyAll();
}
void IPCSkeletonBindings::SubscribeToEvent(const uint16_t& event_id) {}

/////////////////////////  PROXY  ////////////////////////////////////

bool IPCProxyBindings::TryAttach() {
  if (!buffor_.Attached() && !buffor_.Reattach().HasValue()) {
    return false;
  }
  if (!cv_.Attached() && !cv_.Reattach().HasValue()) {
    return false;
  }
  return true;
}

void IPCProxyBindings::ShmLoop(std::stop_token token) {
  // No stop_callback with cv_.NotifyAll(): it would race with cv_.Reattach() (unmap) from the other thread.
  // Every wait below is bounded (kPoll / backoff), so a stop request is noticed within ~kPoll.
  constexpr std::chrono::milliseconds kPoll{200};
  constexpr std::chrono::milliseconds kBackoffMax{1000};
  std::mutex sleep_mutex;
  std::condition_variable_any sleep_cv;
  const auto sleep_for = [&](std::chrono::milliseconds d) {
    std::unique_lock lock{sleep_mutex};
    sleep_cv.wait_for(lock, token, d, [] { return false; });
  };
  ara::com::LogDebug() << "[IPCProxyBindings]: SHM loop Start";
  bool attached = true;  // Start() found both segments before launching this loop
  std::chrono::milliseconds backoff = kPoll;
  while (!token.stop_requested()) {
    if (!attached) {
      // Detached: a restart of the skeleton left no usable segment (the name is gone or mapping failed).
      // Never touch the (unmapped) buffers; wait with a growing backoff and try to attach again.
      sleep_for(backoff);
      backoff = std::min(std::max(backoff * 2, kPoll), kBackoffMax);
      if (token.stop_requested()) {
        break;
      }
      if (!TryAttach()) {
        continue;
      }
      ara::com::LogInfo() << "[IPCProxyBindings]: re-attached to the restarted skeleton";
      attached = true;
      backoff = kPoll;
      last_msg_id = 0U;
      continue;
    }
    if (!buffor_.Attached() || !cv_.Attached()) {
      attached = false;
      continue;
    }
    if (*buffor_.GetNewSamplesPointer() == last_msg_id) {
      // Not an unbounded Wait(): a restarted daemon re-initialises the shared condition variable and
      // does not wake a waiter of the old one, so re-check the counter periodically.
      cv_.WaitFor(kPoll);
      // A restarted skeleton creates NEW shared segments: attach to them (the old ones are dead).
      if (buffor_.Stale() || cv_.Stale()) {
        ara::com::LogInfo() << "[IPCProxyBindings]: the skeleton was restarted, re-attaching";
        // Both segments are re-mapped together; on any failure the loop goes to the detached state above.
        attached = false;
        buffor_.Detach();
        std::ignore = cv_.Reattach();  // leaves cv_ detached on failure; retried above
        backoff = std::chrono::milliseconds{0};
      }
      continue;
    }
    if (token.stop_requested()) {
      break;
    }
    if (*buffor_.GetNewSamplesPointer() != last_msg_id) {
      const auto p_size = *(buffor_.GetNewSamplesPointer() + 1);
      uint8_t* buff = buffor_.GetNewSamplesPointer();
      //   // std::memcpy(buff, buffor_.GetNewSamplesPointer() + 2, p_size);
      std::vector<uint8_t> msg_v{buff + 2, buff + 2 + p_size};
      //   // delete[] buff;
      const auto& frame_r = ara::com::someip::SomeipFrame::MakeFrame(msg_v);
      last_msg_id = *buffor_.GetNewSamplesPointer();
      if (frame_r.HasValue()) {
        const auto& frame = frame_r.Value();
        ara::com::LogDebug() << "[IPCProxyBindings]: New Event Value";
        ara::com::LogDebug()
            << "[IPCProxyBindings]: Service_id: " << frame.header_.service_id;
        ara::com::LogDebug()
            << "[IPCProxyBindings]: Method_id: " << frame.header_.method_id;
        std::ignore = std::async(std::launch::async, [this, &frame]() {
          this->event_notification_callback_(frame.header_.method_id,
                                             frame.payload_);
        });

      } else {
        ara::com::LogError() << "[IPCProxyBindings]: " << frame_r.Error();
        ara::com::LogError()
            << "[IPCProxyBindings]: ID: " << *buffor_.GetNewSamplesPointer();
        ara::com::LogError() << "[IPCProxyBindings]: Size: "
                             << *(buffor_.GetNewSamplesPointer() + 1);
      }
    }
  }
}

IPCProxyBindings::IPCProxyBindings(const ara::core::StringView path)
    : main_path_{path},
buffor_{ara::core::InstanceSpecifier{"/" + path + "-buffor"}},
cv_{ara::core::InstanceSpecifier{"/" + path + "-cv"}} {}

void IPCProxyBindings::Start(std::stop_token token) {
  ara::com::LogInfo() << "IPCProxyBindings: " << "Started";

  while (!token.stop_requested()) {
    if (buffor_.FindService().HasValue()) {
      break;
    } else {
      srp::core::condition::wait_for(std::chrono::milliseconds{500}, token);
    }
  }
  while (!token.stop_requested()) {
    if (cv_.Find().HasValue()) {
      break;
    } else {
      srp::core::condition::wait_for(std::chrono::milliseconds{500}, token);
    }
  }
  if (!token.stop_requested()) {
    shm_loop_thread_ = std::make_unique<std::jthread>(
        [this](std::stop_token token) { this->ShmLoop(token); });
    stream_sock_ = std::make_unique<srp::bindings::com::soc::StreamIpcSocket>();
  }
}
void IPCProxyBindings::Stop() {
  // shm_loop_thread_->request_stop();
  // shm_loop_thread_->join();
  shm_loop_thread_.release();
}
ara::core::Result<std::vector<uint8_t>> IPCProxyBindings::HandleMethod(
    const uint16_t& method_id, const std::vector<uint8_t>& payload) {
  ara::com::LogDebug() << "IPCSkeletonBindings: " << "SomeIp Method Send";
  HeaderStructure header_{this->service_id_,
                          method_id,
                          0U,
                          msg_id++,
                          0U,
                          1U,
                          this->major_version_,
                          MessageType::kRequest,
                          MessageCode::kEOk};
  const auto& frame =
      ara::com::someip::SomeipFrame::MakeFrame(header_, payload);
  const auto& vec = frame.GetRaw();
  // TODO(Bartosz Snieg): Send and Recive
  const auto& resp_o = stream_sock_->Transmit(main_path_, vec);
  {
    if (!resp_o.has_value()) {
      return ara::com::MakeErrorCode(ara::com::ComErrc::kCommunicationLinkError,
                                     "");
    }
    const auto& resp_frame_r =
        ara::com::someip::SomeipFrame::MakeFrame(resp_o.value());
    if (!resp_frame_r.HasValue()) {
      return ara::com::MakeErrorCode(ara::com::ComErrc::kFieldValueIsNotValid,
                                     "");
    }
    const auto& resp_frame = resp_frame_r.Value();

    if (resp_frame.header_.return_code == MessageCode::kEOk) {
      return resp_frame.payload_;
    }
    return ara::com::MakeErrorCode(
        ara::com::ComErrc::kCommunicationStackError,
        "Response Status: " + std::to_string(resp_frame.header_.return_code));
  }
}

void IPCProxyBindings::HandleEvent(const uint16_t& method_id,
                                   const std::vector<uint8_t>& payload) {}

void IPCProxyBindings::SubscribeToEvent(const uint16_t& event_id) {
  this->subscribe_new_status_(event_id, SubscribeStatus::kSubscribe);
}
}  // namespace bindings
}  // namespace someip
}  // namespace com
}  // namespace ara
