/**
 * @file example_did.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-09-01
 *
 * @copyright Copyright (c) 2024
 *
 */
#ifndef APPS_COMMON_DIAG_DEMON_CODE_SERVICES_EXAMPLE_DID_H_
#define APPS_COMMON_DIAG_DEMON_CODE_SERVICES_EXAMPLE_DID_H_
#include <string>
#include <vector>

#include "platform/diag/generic_data_identifier.h"
#include "platform/diag/uds_error_domain.h"
#include "platform/log/log.h"

namespace srp {
namespace platform {
namespace diag_demon {

class ExampleDiD : public platform::diag::GenericDiD {
 private:
  platform::core::Result<platform::diag::OperationOutput> Read() noexcept override {
    return platform::diag::OperationOutput{{0x01, 0x02, 0x03, 0x04}};
  }
  platform::core::Result<void> Write(const std::vector<uint8_t>&) noexcept override {
    return platform::diag::MakeErrorCode(
        platform::diag::UdsDiagErrc::kSubFunctionNotSupported);
  }

 public:
  explicit ExampleDiD(const platform::core::InstanceSpecifier& instance)
      : platform::diag::GenericDiD(instance) {}
};
}  // namespace diag_demon
}  // namespace platform
}  // namespace srp

#endif  // APPS_COMMON_DIAG_DEMON_CODE_SERVICES_EXAMPLE_DID_H_
