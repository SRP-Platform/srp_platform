/**
* @file em_application.cc
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-02-06
 *
 * @copyright Copyright (c) 2024
 *
 */

#ifndef PLATFORM_COMMON_SOMEIP_DEMON_CODE_COMMON_COMMON_CONFIG_H_
#define PLATFORM_COMMON_SOMEIP_DEMON_CODE_COMMON_COMMON_CONFIG_H_

#include <cstdint>

namespace srp {
namespace someip_demon {
namespace common {
class CommonConfig {
 public:
  std::uint32_t local_ip_address_{0};
  static CommonConfig& GetInstance() {
    static CommonConfig instance;
    return instance;
  }
};
}  // namespace common
}  // namespace someip_demon
}  // namespace srp


#endif  // PLATFORM_COMMON_SOMEIP_DEMON_CODE_COMMON_COMMON_CONFIG_H_
