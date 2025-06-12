/**
 * @file aplication_status_did.cc
 * @author Mateusz Krajewski (matikrajek42@gmail.com)
 * @brief 
 * @version 0.1
 * @date 2025-04-15
 * 
 * @copyright Copyright (c) 2025
 * 
 */
#include "apps/common/em/code/services/em/aplication_status_did.h"
#include "platform/diag/uds_error_domain.h"
#include "platform/diag/diag_error_domain.h"
#include "platform/log/log.h"
namespace srp {
namespace em {
namespace service {

AplicationStatusDiD::AplicationStatusDiD(const ::platform::core::InstanceSpecifier &specifier,
                            std::shared_ptr<data::IAppDb> db): db_(db), GenericDiD(specifier) {
}
::platform::core::Result<platform::diag::OperationOutput> AplicationStatusDiD::Read() noexcept {
    auto state_id = this->db_->GetActualFunctionGroupID();
    auto FgAppList = this->db_->GetFgAppList(state_id);
    if (!FgAppList.has_value()) {
        return platform::diag::MakeErrorCode(platform::diag::DiagErrc::kMemoryError, "");
    }
    uint8_t size = FgAppList.value().get().size();
    std::vector<uint8_t> res;
    res.push_back(size);
    ::platform::log::LogDebug() << "number of services: " << size;
    for (const auto app_id_ : FgAppList.value().get()) {
        auto app = db_->GetAppConfig(app_id_);
        if (!app.has_value()) {
            continue;
        }
        uint8_t app_state = static_cast<uint8_t>(app.value().get().GetExecutionState());
        ::platform::log::LogDebug() << "app_id: " << app_id_ << ", state: "
                        << ::platform::exec::get_string(app.value().get().GetExecutionState());
        res.push_back(static_cast<uint8_t>(app_id_ >> 8));
        res.push_back(static_cast<uint8_t>(app_id_ & 0xFF));
        res.push_back(app_state);
    }
    return ::platform::diag::OperationOutput{res};
}
::platform::core::Result<void> AplicationStatusDiD::Write(const std::vector<uint8_t> &) noexcept {
    return platform::diag::MakeErrorCode(platform::diag::DiagErrc::kNotOffered, "");
}
}  // namespace service
}  // namespace em
}   // namespace srp
