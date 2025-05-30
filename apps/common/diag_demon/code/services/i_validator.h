/**
 * @file i_validator.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-04-28
 *
 * @copyright Copyright (c) 2024
 *
 */
#ifndef APPS_COMMON_DIAG_DEMON_CODE_SERVICES_I_VALIDATOR_H_
#define APPS_COMMON_DIAG_DEMON_CODE_SERVICES_I_VALIDATOR_H_

#include "apps/common/diag_demon/code/services/uds_nrc_codes.h"
#include "apps/common/diag_demon/code/services/uds_request.h"
namespace srp {
namespace platform {
namespace diag_demon {
namespace uds {

class IValidator {
 public:
  virtual UdsNrcCodes Validate(const UdsRequest& req) const = 0;
  virtual ~IValidator() = default;
};
}  // namespace uds
}  // namespace diag_demon
}  // namespace platform
}  // namespace srp

#endif  // APPS_COMMON_DIAG_DEMON_CODE_SERVICES_I_VALIDATOR_H_
