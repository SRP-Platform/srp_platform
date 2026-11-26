/**
 * Copyright (c) 2024
 *
 * @file mavlink_bindings.cc
 */
#include "ara/com/someip/bindings/mavlink/mavlink_bindings.h"

#include <utility>

#include "ara/com/com_error_domain.h"
#include "ara/com/log.h"
#include "ara/com/someip/bindings/binding_factory.h"
#include "ara/core/model_db.h"

namespace ara {
namespace com {
namespace someip {
namespace bindings {

std::shared_ptr<IBindings> MakeMavlinkBindings(const ara::core::InstanceSpecifier& instance,
                                               std::uint16_t service_id) {
  const auto model = ara::core::ModelDataBase::GetInstance()
                         .ResolveInstanceSpecifier<ara::core::model::ModelMavlink>(instance);
  if (!model.HasValue()) {
    return nullptr;
  }
  const auto& mav = model.Value();
  auto bindings = BindingsFactory::GetBindings(mav);
  using Dir = ara::core::model::ModelSomeIp::Direction;
  // SetServiceData expects a ModelSomeIp; MAVLink has no port and no interface version.
  bindings->SetServiceData(
      service_id, ara::core::model::ModelSomeIp{
                      mav.com_id_, 0U, mav.instance_, 0U, 0U,
                      mav.dir_ == ara::core::model::ModelMavlink::kOut ? Dir::kOut : Dir::kIn});
  ara::com::LogInfo() << "Mapping: [" << instance << "] map as service_id: " << service_id
                      << " instance: " << mav.instance_ << " (mavlink)";
  return bindings;
}

// ---------------------------------------------------------------- skeleton

MavlinkSkeletonBindings::MavlinkSkeletonBindings(const ara::core::model::ModelMavlink& model,
                                                 ara::com::mavlink::DaemonClient* client)
    : model_{model}, client_{client} {}

void MavlinkSkeletonBindings::Start(std::stop_token) {
  if (endpoint_) {
    return;
  }
  ara::com::mavlink::MavlinkEndpoint::Handlers handlers;
  handlers.method = [this](std::uint16_t id, const std::vector<std::uint8_t>& payload)
      -> ara::core::Result<std::vector<std::uint8_t>> {
    if (!method_req_with_response_) {
      return ara::com::MakeErrorCode(ara::com::ComErrc::kSetHandlerNotSet,
                                     "no method handler set on the skeleton");
    }
    return method_req_with_response_(id, payload);
  };
  endpoint_ = client_->AttachEndpoint(model_, /*skeleton=*/true, std::move(handlers));
}

void MavlinkSkeletonBindings::Stop() {
  if (endpoint_) {
    client_->DetachEndpoint(endpoint_);
    endpoint_.reset();
  }
}

ara::core::Result<std::vector<uint8_t>> MavlinkSkeletonBindings::HandleMethod(
    const uint16_t&, const std::vector<uint8_t>&) {
  // A skeleton does not call methods (the proxy does).
  return ara::com::MakeErrorCode(ara::com::ComErrc::kWrongMethodCallProcessingMode,
                                 "a MAVLink skeleton does not call methods");
}

void MavlinkSkeletonBindings::HandleEvent(const uint16_t& event_id,
                                          const std::vector<uint8_t>& payload) {
  if (!endpoint_) {
    return;
  }
  // An encoding error / unknown event must not stop the application (counted in the endpoint).
  std::ignore = endpoint_->PublishEvent(event_id, payload);
}

void MavlinkSkeletonBindings::SubscribeToEvent(const uint16_t&) {
  // Events without SOME/IP SD: the skeleton always transmits.
}

// ---------------------------------------------------------------- proxy

MavlinkProxyBindings::MavlinkProxyBindings(const ara::core::model::ModelMavlink& model,
                                           ara::com::mavlink::DaemonClient* client)
    : model_{model}, client_{client} {}

void MavlinkProxyBindings::Start(std::stop_token) {
  if (endpoint_) {
    return;
  }
  ara::com::mavlink::MavlinkEndpoint::Handlers handlers;
  handlers.event = [this](std::uint16_t id, const std::vector<std::uint8_t>& payload) {
    if (event_notification_callback_) {
      event_notification_callback_(id, payload);
    }
  };
  endpoint_ = client_->AttachEndpoint(model_, /*skeleton=*/false, std::move(handlers));
}

void MavlinkProxyBindings::Stop() {
  if (endpoint_) {
    client_->DetachEndpoint(endpoint_);
    endpoint_.reset();
  }
}

ara::core::Result<std::vector<uint8_t>> MavlinkProxyBindings::HandleMethod(
    const uint16_t& method_id, const std::vector<uint8_t>& payload) {
  if (!endpoint_) {
    return ara::com::MakeErrorCode(ara::com::ComErrc::kServiceNotAvailable,
                                   "the MAVLink proxy is not started");
  }
  return endpoint_->CallMethod(method_id, payload);
}

void MavlinkProxyBindings::HandleEvent(const uint16_t&, const std::vector<uint8_t>&) {
  // A proxy does not publish events.
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
