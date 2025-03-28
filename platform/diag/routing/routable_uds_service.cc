/**
 * @file routable_uds_service.cc
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-11-30
 *
 * @copyright Copyright (c) 2024
 *
 */
#include "platform/diag/routing/routable_uds_service.h"

#include <utility>

#include "platform/core/model_db.h"
#include "platform/diag/diag_error_domain.h"
#include "platform/log/logging_menager.h"

namespace platform {
namespace diag {
namespace routing {
const uint8_t RoutableUdsService::cPositiveResponseSidIncrement;
const uint8_t RoutableUdsService::cNegativeResponseCodeSid;

RoutableUdsService::RoutableUdsService(
    const platform::core::InstanceSpecifier &specifier, uint8_t sid,
    std::unique_ptr<srp::bindings::com::soc::StreamIpcSocket> sock) noexcept
    : mSid{sid},
      mOffered{false},
      mSpecifier{specifier},
      diag_logger_{platform::log::LoggingMenager::GetInstance()->CreateLogger(
          "diag", "", platform::log::LogLevel::kWarn)},
      sock_{std::move(sock)} {}

void RoutableUdsService::GenerateNegativeResponse(OperationOutput &response,
                                                  uint8_t nrc) const {
  response.responseData = {cNegativeResponseCodeSid, mSid, nrc};
}

uint8_t RoutableUdsService::GetSid() const noexcept { return mSid; }

platform::core::Result<void> RoutableUdsService::Offer() {
  if (mOffered) {
    return platform::diag::MakeErrorCode(platform::diag::DiagErrc::kAlreadyOffered, "");
  } else {
    const auto data =
        platform::core::ModelDataBase::GetInstance()
            .ResolveInstanceSpecifier<platform::core::model::ModelUds>(mSpecifier);
    if (!data.HasValue()) {
      diag_logger_.LogError() << data.Error();
      return data.Error();
    }
    const auto &model = data.Value();
    this->job_id_ = model.com_id_;
    diag_logger_.LogInfo() << "Mapping: " << mSpecifier
                           << " to id: " << this->job_id_;
    const auto res = sock_->Init("SRP.DIAG." + this->job_id_);
    sock_->SetRXCallback(
        std::bind(&RoutableUdsService::RXLoop, this, std::placeholders::_1));
    sock_->StartRXThread();

    mOffered = true;
    return {};
  }
}

bool RoutableUdsService::IsOffered() const noexcept { return mOffered; }

void RoutableUdsService::StopOffer() noexcept {
  if (mOffered) {
    mOffered = false;
    sock_->StopRXThread();
  }
}

std::vector<uint8_t> RoutableUdsService::RXLoop(
    const std::vector<uint8_t> &data) {
  const auto res = this->HandleMessage(data);
  if (res.HasValue()) {
    return res.Value().responseData;
  } else {
    OperationOutput temp{};
    this->GenerateNegativeResponse(temp,
                                   static_cast<uint8_t>(res.Error().Value()));
    return temp.responseData;
  }
}

}  // namespace routing
}  // namespace diag
}  // namespace platform
