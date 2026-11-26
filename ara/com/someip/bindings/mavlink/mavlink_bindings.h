/**
 * Copyright (c) 2024
 *
 * @file mavlink_bindings.h
 * @brief Proxy/skeleton bindings over MAVLink (behind IBindings). All logic (links,
 * redundancy, signing, limits) lives in the MavlinkDaemon; this only maps IBindings
 * calls onto a MavlinkEndpoint (the daemon IPC client).
 */
#ifndef ARA_COM_SOMEIP_BINDINGS_MAVLINK_MAVLINK_BINDINGS_H_
#define ARA_COM_SOMEIP_BINDINGS_MAVLINK_MAVLINK_BINDINGS_H_

#include <cstdint>
#include <memory>
#include <vector>

#include "ara/com/mavlink/client/daemon_client.h"
#include "ara/com/someip/bindings/common_bindings.h"
#include "ara/core/instance_specifier.h"
#include "ara/core/model/mavlink_model_item.h"

namespace ara {
namespace com {
namespace someip {
namespace bindings {

/// Bindings of the `on: mavlink` instance `instance` with the service data already set, or
/// nullptr when the instance has no ModelMavlink (it is a SOME/IP instance).
std::shared_ptr<IBindings> MakeMavlinkBindings(const ara::core::InstanceSpecifier& instance,
                                               std::uint16_t service_id);

class MavlinkSkeletonBindings : public CommonBindings {
 public:
  explicit MavlinkSkeletonBindings(
      const ara::core::model::ModelMavlink& model,
      ara::com::mavlink::DaemonClient* client = &ara::com::mavlink::DaemonClient::Instance());
  void Start(std::stop_token token) override;
  void Stop() override;
  ara::core::Result<std::vector<uint8_t>> HandleMethod(
      const uint16_t& method_id, const std::vector<uint8_t>& payload) override;
  void HandleEvent(const uint16_t& method_id, const std::vector<uint8_t>& payload) override;
  void SubscribeToEvent(const uint16_t& event_id) override;

  std::shared_ptr<ara::com::mavlink::MavlinkEndpoint> endpoint() const { return endpoint_; }

 private:
  const ara::core::model::ModelMavlink model_;
  ara::com::mavlink::DaemonClient* client_;
  std::shared_ptr<ara::com::mavlink::MavlinkEndpoint> endpoint_;
};

class MavlinkProxyBindings : public CommonBindings {
 public:
  explicit MavlinkProxyBindings(
      const ara::core::model::ModelMavlink& model,
      ara::com::mavlink::DaemonClient* client = &ara::com::mavlink::DaemonClient::Instance());
  void Start(std::stop_token token) override;
  void Stop() override;
  ara::core::Result<std::vector<uint8_t>> HandleMethod(
      const uint16_t& method_id, const std::vector<uint8_t>& payload) override;
  void HandleEvent(const uint16_t& method_id, const std::vector<uint8_t>& payload) override;
  /// Local subscription: kSubscribe (event available) or kNotSubscribe (excluded).
  void SubscribeToEvent(const uint16_t& event_id) override;

  std::shared_ptr<ara::com::mavlink::MavlinkEndpoint> endpoint() const { return endpoint_; }

 private:
  const ara::core::model::ModelMavlink model_;
  ara::com::mavlink::DaemonClient* client_;
  std::shared_ptr<ara::com::mavlink::MavlinkEndpoint> endpoint_;
};

}  // namespace bindings
}  // namespace someip
}  // namespace com
}  // namespace ara

#endif  // ARA_COM_SOMEIP_BINDINGS_MAVLINK_MAVLINK_BINDINGS_H_
