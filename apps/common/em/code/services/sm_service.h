/**
 * @file sm_service.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-12-30
 *
 * @copyright Copyright (c) 2024
 *
 */
#ifndef APPS_COMMON_EM_CODE_SERVICES_SM_SERVICE_H_
#define APPS_COMMON_EM_CODE_SERVICES_SM_SERVICE_H_

#include <functional>
#include <utility>

#include "srp/platform/sm/StateManagerSkeleton.h"

namespace srp {
namespace em {
namespace service {
class SmService : public srp::platform::sm::StateManagerSkeleton {
  const std::function<int8_t(uint16_t)> set_state_callback_;

 private:
  platform::core::Result<void> SetState(const std::uint16_t& in_parm) override {
    set_state_callback_(in_parm);
    return {};
  }
  platform::core::Result<std::uint16_t> GetState() override { return 8U; }

 public:
  SmService(const platform::core::InstanceSpecifier& instance,
            const std::function<int8_t(uint16_t)>&& set_state_callback)
      : srp::platform::sm::StateManagerSkeleton{instance},
        set_state_callback_{std::move(set_state_callback)} {}
  ~SmService() {}
};

}  // namespace service
}  // namespace em
}  // namespace srp
#endif  // APPS_COMMON_EM_CODE_SERVICES_SM_SERVICE_H_
