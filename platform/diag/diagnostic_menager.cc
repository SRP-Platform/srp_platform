/**
 * @file diagnostic_menager.cc
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-12-03
 *
 * @copyright Copyright (c) 2024
 *
 */
#include "platform/diag/diagnostic_menager.h"

#include <utility>
#include <vector>

#include "platform/diag/diag_error_domain.h"
#include "srp/platform/diag/DtcComDataStructure.h"

namespace platform {
namespace diag {
std::shared_ptr<DiagnosticMenager> DiagnosticMenager::instance_{nullptr};

DiagnosticMenager::DiagnosticMenager(/* args */) {}

DiagnosticMenager::~DiagnosticMenager() {}

void DiagnosticMenager::HandleNewMsg(
    uint32_t pid, const std::vector<uint8_t>& payload) noexcept {}

platform::core::Result<void> DiagnosticMenager::SendDtcUpdate(
    const uint32_t& id, const uint8_t& new_status) noexcept {
  srp::platform::diag::DtcComDataStructure msg{id, 0x01, new_status};
  const auto raw_o = srp::data::Convert2Vector<
      srp::platform::diag::DtcComDataStructure>::Conv(msg);
  this->send_callback_to_("SRP.platform.DTC", raw_o, com::IComClient::kDiag);

  return {};
}

std::shared_ptr<DiagnosticMenager> DiagnosticMenager::GetInstance() noexcept {
  if (instance_ == nullptr) {
    instance_ = std::make_shared<DiagnosticMenager>();
  }
  return instance_;
}

platform::core::Result<void> DiagnosticMenager::RegisterDtcHandler(
    const uint32_t& id, DtcUpdateCallback&& callback_) {
  if (this->dtc_handler_list_.insert({id, std::move(callback_)}).second) {
    srp::platform::diag::DtcComDataStructure msg{id, 0x00, 0x00};

    const auto raw_o = srp::data::Convert2Vector<
        srp::platform::diag::DtcComDataStructure>::Conv(msg);

    if (this->send_callback_to_("SRP.platform.DTC", raw_o,
                                com::IComClient::kDiag)) {
      return {};
    }

  } else {
    srp::platform::diag::DtcComDataStructure msg{id, 0x00, 0x00};
    const auto raw_o = srp::data::Convert2Vector<
        srp::platform::diag::DtcComDataStructure>::Conv(msg);
    if (this->send_callback_to_("SRP.platform.DTC", raw_o,
                                com::IComClient::kDiag)) {
      return {};
    }
  }
  return platform::diag::MakeErrorCode(diag::DiagErrc::kBusy,
                                  "dtc already offered localy");
}

}  // namespace diag
}  // namespace platform
