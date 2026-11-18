/**
 * Copyright (c) 2024
 *
 * @file mavlink_bindings.cc
 */
#include "ara/com/someip/bindings/mavlink/mavlink_bindings.h"

#include <utility>

#include "ara/com/com_error_domain.h"

namespace ara {
namespace com {
namespace someip {
namespace bindings {

ara::core::model::ModelSomeIp MavlinkModelView(const ara::core::model::ModelMavlink& m) {
  using Dir = ara::core::model::ModelSomeIp::Direction;
  return ara::core::model::ModelSomeIp{
      m.com_id_, 0U, m.instance_, m.major_version_, m.minor_version_,
      m.dir_ == ara::core::model::ModelMavlink::kOut ? Dir::kOut : Dir::kIn};
}

// ---------------------------------------------------------------- skeleton

MavlinkSkeletonBindings::MavlinkSkeletonBindings(const ara::core::model::ModelMavlink& model,
                                                 ara::com::mavlink::LinkManager* manager)
    : model_{model}, manager_{manager} {}

void MavlinkSkeletonBindings::Start(std::stop_token) {
  if (endpoint_) {
    return;
  }
  endpoint_ = manager_->AttachEndpoint(model_, /*skeleton=*/true);
  endpoint_->SetMethodHandler(
      [this](std::uint16_t id, const std::vector<std::uint8_t>& payload)
          -> ara::core::Result<std::vector<std::uint8_t>> {
        if (!method_req_with_response_) {
          return ara::com::MakeErrorCode(ara::com::ComErrc::kSetHandlerNotSet,
                                         "brak handlera metod skeletonu");
        }
        return method_req_with_response_(id, payload);
      });
}

void MavlinkSkeletonBindings::Stop() {
  if (endpoint_) {
    manager_->DetachEndpoint(endpoint_);
    endpoint_.reset();
  }
}

ara::core::Result<std::vector<uint8_t>> MavlinkSkeletonBindings::HandleMethod(
    const uint16_t&, const std::vector<uint8_t>&) {
  // Skeleton nie wywołuje metod (to robi proxy).
  return ara::com::MakeErrorCode(ara::com::ComErrc::kWrongMethodCallProcessingMode,
                                 "skeleton MAVLink nie wywołuje metod");
}

void MavlinkSkeletonBindings::HandleEvent(const uint16_t& event_id,
                                          const std::vector<uint8_t>& payload) {
  if (!endpoint_) {
    return;
  }
  // Błąd kodowania/nieznany event nie może zatrzymać aplikacji; zostaje w logu managera.
  std::ignore = endpoint_->PublishEvent(event_id, payload);
}

void MavlinkSkeletonBindings::SubscribeToEvent(const uint16_t&) {
  // Eventy bez SOME/IP SD: skeleton zawsze nadaje.
}

// ---------------------------------------------------------------- proxy

MavlinkProxyBindings::MavlinkProxyBindings(const ara::core::model::ModelMavlink& model,
                                           ara::com::mavlink::LinkManager* manager)
    : model_{model}, manager_{manager} {}

void MavlinkProxyBindings::Start(std::stop_token) {
  if (endpoint_) {
    return;
  }
  endpoint_ = manager_->AttachEndpoint(model_, /*skeleton=*/false);
  endpoint_->SetEventHandler([this](std::uint16_t id, const std::vector<std::uint8_t>& payload) {
    if (event_notification_callback_) {
      event_notification_callback_(id, payload);
    }
  });
}

void MavlinkProxyBindings::Stop() {
  if (endpoint_) {
    manager_->DetachEndpoint(endpoint_);
    endpoint_.reset();
  }
}

ara::core::Result<std::vector<uint8_t>> MavlinkProxyBindings::HandleMethod(
    const uint16_t& method_id, const std::vector<uint8_t>& payload) {
  if (!endpoint_) {
    return ara::com::MakeErrorCode(ara::com::ComErrc::kServiceNotAvailable,
                                   "proxy MAVLink nie jest wystartowany");
  }
  return endpoint_->CallMethod(method_id, payload);
}

void MavlinkProxyBindings::HandleEvent(const uint16_t&, const std::vector<uint8_t>&) {
  // Proxy nie nadaje eventów.
}

void MavlinkProxyBindings::SubscribeToEvent(const uint16_t& event_id) {
  if (!subscribe_new_status_) {
    return;
  }
  const bool ok = endpoint_ && endpoint_->IsEventAvailable(event_id);
  subscribe_new_status_(event_id, ok ? kSubscribe : kNotSubscribe);
}

}  // namespace bindings
}  // namespace someip
}  // namespace com
}  // namespace ara
