/**
 * Copyright (c) 2024
 *
 * @file mavlink_bindings.h
 * @brief Bindingi proxy/skeleton na MAVLinku (za IBindings). Cała logika
 * (łącza, redundancja, dedup, podpis, limity) jest w MavlinkDaemon; tu tylko
 * mapowanie wywołań IBindings na MavlinkEndpoint (klient IPC daemona).
 */
#ifndef ARA_COM_SOMEIP_BINDINGS_MAVLINK_MAVLINK_BINDINGS_H_
#define ARA_COM_SOMEIP_BINDINGS_MAVLINK_MAVLINK_BINDINGS_H_

#include <cstdint>
#include <memory>
#include <vector>

#include "ara/com/mavlink/client/daemon_client.h"
#include "ara/com/someip/bindings/common_bindings.h"
#include "ara/core/model/mavlink_model_item.h"

namespace ara {
namespace com {
namespace someip {
namespace bindings {

/// Widok ModelMavlink jako ModelSomeIp (SetServiceData oczekuje tego typu).
ara::core::model::ModelSomeIp MavlinkModelView(const ara::core::model::ModelMavlink& m);

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
  /// Lokalna subskrypcja: kSubscribe (event dostępny) albo kNotSubscribe (wykluczony).
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
