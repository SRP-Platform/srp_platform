/**
 * @file common_connector.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-11-26
 *
 * @copyright Copyright (c) 2024
 *
 */
#ifndef APPS_COMMON_SOMEIP_DEMON_CODE_CONNECTOR_COMMON_CONNECTOR_H_
#define APPS_COMMON_SOMEIP_DEMON_CODE_CONNECTOR_COMMON_CONNECTOR_H_
#include <memory>

#include "platform/com/someip/someip_frame.h"
#include "bindings/common/socket/proccess_socket.h"
#include "platform/common/someip_demon/code/common/udp_socket.h"
#include "platform/common/someip_demon/code/connector/i_connector.h"

namespace srp {
namespace someip_demon {
namespace connectors {
class CommonConnector : public IConnector {
 protected:
  std::shared_ptr<srp::bindings::com::ProccessSocket> ipc_soc_;
  std::shared_ptr<common::soc::UdpSocket> udp_sock_;

 public:
  void RegisterSockets(
      std::shared_ptr<srp::bindings::com::ProccessSocket> ipc_soc,
      std::shared_ptr<common::soc::UdpSocket> udp_sock) override;
  virtual ~CommonConnector() = default;
};
}  // namespace connectors
}  // namespace someip_demon
}  // namespace srp
#endif  // APPS_COMMON_SOMEIP_DEMON_CODE_CONNECTOR_COMMON_CONNECTOR_H_
