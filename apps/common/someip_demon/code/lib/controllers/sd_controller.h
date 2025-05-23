#ifndef APPS_COMMON_SOMEIP_DEMON_CODE_LIB_CONTROLLERS_SD_CONTROLLER_H_
#define APPS_COMMON_SOMEIP_DEMON_CODE_LIB_CONTROLLERS_SD_CONTROLLER_H_

namespace srp {
namespace apps {
namespace network {
namespace someip {
namespace controllers {
class SdController {
 public:
  virtual ~SdController() = default;
  SdController(SdController&) noexcept = delete;
  SdController(SdController&&) noexcept = delete;
  SdController& operator=(SdController&) noexcept = delete;
  SdController& operator=(SdController&&) noexcept = delete;
};
}  // namespace controllers
}  // namespace someip
}  // namespace network
}  // namespace apps
}  // namespace srp

#endif  // APPS_COMMON_SOMEIP_DEMON_CODE_LIB_CONTROLLERS_SD_CONTROLLER_H_
