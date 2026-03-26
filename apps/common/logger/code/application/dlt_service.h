/**
 * @file dlt_service.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-02-07
 *
 * @copyright Copyright (c) 2024
 *
 */
#ifndef APPS_COMMON_LOGGER_CODE_APPLICATION_DLT_SERVICE_H_
#define APPS_COMMON_LOGGER_CODE_APPLICATION_DLT_SERVICE_H_
#include <map>
#include <memory>
#include <string>

#include "platform/exec/adaptive_application.h"
#include "bindings/common/socket/ipc_socket.h"
#include "apps/common/someip_demon/code/old/common/udp_multicast_socket.h"
#include "core/common/wait_queue.h"
#include "apps/common/logger/code/data/idlt_frame.h"

namespace srp {

namespace dlt {
class DltService final : public platform::exec::AdaptiveApplication {
 private:
  srp::common::soc::UdpMulticastSocket soc{};
  srp::bindings::com::soc::IpcSocket ipc_soc{};
  std::string ip_;
  uint16_t tx_port;
  std::string ec_name;

  core::WaitQueue<std::shared_ptr<srp::dlt::data::IDLTFrame>, 1000> logs{};
  /**
   * @brief This function is called to initialiaze the application
   *
   * @pplatformm parms map with parms
   */
  int Initialize(const std::map<platform::core::StringView, platform::core::StringView>
                     parms) override;
  /**
   * @brief This function is called to launch the application
   *
   * @pplatformm token stop token
   */
  int Run(const std::stop_token& token) override;
  void InitIPC() noexcept;
  void InitUDP() noexcept;

 public:
  DltService(/* args */);
  ~DltService();
};
}  // namespace dlt
}  // namespace srp
#endif  // APPS_COMMON_LOGGER_CODE_APPLICATION_DLT_SERVICE_H_
