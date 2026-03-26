#ifndef APPS_COMMON_SOMEIP_DEMON_CODE_LIB_SOMEIP_COMMON_INTERFACE_H_
#define APPS_COMMON_SOMEIP_DEMON_CODE_LIB_SOMEIP_COMMON_INTERFACE_H_

#include "platform/core/result.h"

namespace srp::someip_demon
{
namespace someip_demon
{
namespace lib
{
class SomeIpCommonInterface
{
public:
    virtual ~SomeIpCommonInterface() = default;
    virtual platform::core::Result<void> Start() noexcept = 0;
    virtual platform::core::Result<void> Stop() noexcept = 0;
};
} // namespace lib
} // namespace someip_demon
} // namespace srp::someip_demon

#endif  // APPS_COMMON_SOMEIP_DEMON_CODE_LIB_SOMEIP_COMMON_INTERFACE_H_
