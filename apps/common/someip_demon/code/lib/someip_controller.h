#ifndef APPS_COMMON_SOMEIP_DEMON_CODE_LIB_SOMEIP_CONTROLLER_H_
#define APPS_COMMON_SOMEIP_DEMON_CODE_LIB_SOMEIP_CONTROLLER_H_

#include <memory>
#include "apps/common/someip_demon/code/lib/controllers/core_controller.h"

namespace srp
{
namespace apps
{
namespace network
{
namespace someip
{
using SomeIpController = srp::apps::network::someip::controllers::CoreController;

std::unique_ptr<SomeIpController> CreateSomeIPController() noexcept;

} // namespace someip
} // namespace network
} // namespace apps
} // namespace srp


#endif  // APPS_COMMON_SOMEIP_DEMON_CODE_LIB_SOMEIP_CONTROLLER_H_
