/**
 * @file network_controller.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-11-26
 *
 * @copyright Copyright (c) 2024
 *
 */
#ifndef APPS_COMMON_SOMEIP_DEMON_CODE_COMMON_NETWORK_CONTROLLER_H_
#define APPS_COMMON_SOMEIP_DEMON_CODE_COMMON_NETWORK_CONTROLLER_H_

#include <memory>
#include <string>
#include <vector>

#include "apps/common/someip_demon/code/common/i_controller.h"
#include "apps/common/someip_demon/code/common/i_network_controller.h"
#include "apps/log/logger.h"
namespace srp {
namespace someip_demon {
namespace common {
class NetworkController : public INetworkController {
 private:
  const apps::log::Logger& logger_;
  std::shared_ptr<IController> sd_controller_;

 public:
  NetworkController(/* args */);
  ~NetworkController();
  void ProcessRawFrame(const std::string& ip, const std::uint16_t& port,
                       const std::vector<std::uint8_t>& data) noexcept override;
  void SetController(std::shared_ptr<IController> controller,
                     INetworkController::ControllerType type) noexcept override;
};

}  // namespace common
}  // namespace someip_demon
}  // namespace srp

#endif  // APPS_COMMON_SOMEIP_DEMON_CODE_COMMON_NETWORK_CONTROLLER_H_
