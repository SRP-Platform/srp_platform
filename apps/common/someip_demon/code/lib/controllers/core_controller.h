#ifndef APPS_COMMON_SOMEIP_DEMON_CODE_LIB_CONTROLLERS_CORE_CONTTROLLER_H_
#define APPS_COMMON_SOMEIP_DEMON_CODE_LIB_CONTROLLERS_CORE_CONTTROLLER_H_

#include <memory>

#include "apps/common/someip_demon/code/lib/controllers/sd_controller.h"

namespace srp {
namespace apps {
namespace network {
namespace someip {
namespace controllers {
class CoreController {
 public:
  virtual ~CoreController() = default;
  CoreController(CoreController&) noexcept = delete;
  CoreController(CoreController&&) noexcept = delete;
  CoreController& operator=(CoreController&) noexcept = delete;
  CoreController& operator=(CoreController&&) noexcept = delete;

  virtual void Start() noexcept = 0;
  virtual void Stop() noexcept = 0;

  virtual void RegisterController(const std::unique_ptr<SdController> controller) noexcept = 0;
};
}  // namespace controllers
}  // namespace someip
}  // namespace network
}  // namespace apps
}  // namespace srp

#endif  // APPS_COMMON_SOMEIP_DEMON_CODE_LIB_CONTROLLERS_CORE_CONTTROLLER_H_
