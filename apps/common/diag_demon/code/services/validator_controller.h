/**
 * @file validator_controller.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-04-28
 *
 * @copyright Copyright (c) 2024
 *
 */

#ifndef APPS_COMMON_DIAG_DEMON_CODE_SERVICES_VALIDATOR_CONTROLLER_H_
#define APPS_COMMON_DIAG_DEMON_CODE_SERVICES_VALIDATOR_CONTROLLER_H_
#include "apps/common/diag_demon/code/services/i_validator.h"
namespace srp {
namespace platform {
namespace diag_demon {
namespace uds {

class ValidatorController : public IValidator {
 private:
  /* data */
 public:
  UdsNrcCodes Validate(const UdsRequest& req) const override;
  ValidatorController(/* args */);
  ~ValidatorController();
};
}  // namespace uds
}  // namespace diag_demon
}  // namespace platform
}  // namespace srp

#endif  // APPS_COMMON_DIAG_DEMON_CODE_SERVICES_VALIDATOR_CONTROLLER_H_
