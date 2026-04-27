/**
 * @file aplication_status_did.h
 * @author Mateusz Krajewski (matikrajek42@gmail.com)
 * @brief 
 * @version 0.1
 * @date 2025-04-15
 * 
 * @copyright Copyright (c) 2025
 * 
 */
#ifndef APPS_COMMON_EM_CODE_SERVICES_EM_APLICATION_STATUS_DID_H_
#define APPS_COMMON_EM_CODE_SERVICES_EM_APLICATION_STATUS_DID_H_
#include <memory>
#include <vector>

#include "platform/diag/generic_data_identifier.h"
#include "apps/common/em/code/services/em/i_app_db.h"
namespace srp {
namespace em {
namespace service {
class AplicationStatusDiD : public ::platform::diag::GenericDiD {
 private:
  std::shared_ptr<data::IAppDb> db_;
 public:
    AplicationStatusDiD(const ::platform::core::InstanceSpecifier &specifier, std::shared_ptr<data::IAppDb> db);
    ::platform::core::Result<::platform::diag::OperationOutput> Read() noexcept override;
    ::platform::core::Result<void> Write(const std::vector<uint8_t> &) noexcept override;
};

}  // namespace service
}  // namespace em
}   // namespace srp

#endif  // APPS_COMMON_EM_CODE_SERVICES_EM_APLICATION_STATUS_DID_H_
