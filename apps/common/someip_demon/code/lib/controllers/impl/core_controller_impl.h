#ifndef APPS_COMMON_SOMEIP_DEMON_CODE_LIB_CONTROLLERS_IMPL_CORE_CONTTROLLER_IMPL_H_
#define APPS_COMMON_SOMEIP_DEMON_CODE_LIB_CONTROLLERS_IMPL_CORE_CONTTROLLER_IMPL_H_

namespace srp {
namespace apps {
namespace network {
namespace someip {
namespace controllers {
class CoreControllerImpl {
 public:
  CoreControllerImpl() = default;
  virtual ~CoreControllerImpl() = default;
  CoreControllerImpl(CoreControllerImpl&) noexcept = delete;
  CoreControllerImpl(CoreControllerImpl&&) noexcept = delete;
  CoreControllerImpl& operator=(CoreControllerImpl&) noexcept = delete;
  CoreControllerImpl& operator=(CoreControllerImpl&&) noexcept = delete;
};
}  // namespace controllers
}  // namespace someip
}  // namespace network
}  // namespace apps
}  // namespace srp

#endif  // APPS_COMMON_SOMEIP_DEMON_CODE_LIB_CONTROLLERS_IMPL_CORE_CONTTROLLER_IMPL_H_