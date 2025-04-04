/**
 * @file did_impl.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-01-11
 *
 * @copyright Copyright (c) 2025
 *
 */
#ifndef APPS_COMMON_STATE_MANAGER_CODE_DID_IMPL_H_
#define APPS_COMMON_STATE_MANAGER_CODE_DID_IMPL_H_

#include <memory>
#include <vector>

#include "core/data/type_converter.h"
#include "platform/core/instance_specifier.h"
#include "platform/diag/generic_data_identifier.h"
#include "platform/diag/uds_error_domain.h"
#include "platform/exec/sm/i_state_controller.h"

namespace srp {
namespace sm {
class DiDImpl : public ::platform::diag::GenericDiD {
 private:
  const std::shared_ptr<::platform::exec::sm::IStateController> state_con_;

  ::platform::core::Result<::platform::diag::OperationOutput> Read() noexcept
      override {
    ::platform::diag::OperationOutput res{
        srp::data::Convert2Vector<uint16_t>::Conv(
            srp::data::EndianConvert<uint16_t>::Conv(
                state_con_->GetCurrentState()))};
    return res;
  }

  ::platform::core::Result<void> Write(
      const std::vector<uint8_t>& val) noexcept override {
    const auto val_16 = srp::data::Convert<uint16_t>::Conv(val);
    if (val_16.has_value()) {
      const auto res = state_con_->ChangeState(
          srp::data::EndianConvert<uint16_t>::Conv(val_16.value()));
      if (res == 0) {
        return {};
      } else {
        return ::platform::diag::MakeErrorCode(
            ::platform::diag::UdsDiagErrc::kProgrammingFailure);
      }
    }

    return ::platform::diag::MakeErrorCode(
        ::platform::diag::UdsDiagErrc::kGeneralReject);
  }

 public:
  DiDImpl(const ::platform::core::InstanceSpecifier instance,
          std::shared_ptr<::platform::exec::sm::IStateController> state_con)
      : ::platform::diag::GenericDiD{instance}, state_con_{state_con} {}
  ~DiDImpl() = default;
};
}  // namespace sm
}  // namespace srp

#endif  // APPS_COMMON_STATE_MANAGER_CODE_DID_IMPL_H_
