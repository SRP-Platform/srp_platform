/**
 * @file com_wrapper.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-01-11
 *
 * @copyright Copyright (c) 2025
 *
 */
#ifndef APPS_COMMON_STATE_MANAGER_CODE_COM_WRAPPER_H_
#define APPS_COMMON_STATE_MANAGER_CODE_COM_WRAPPER_H_
#include "platform/exec/sm/i_sm_com_wrapper.h"
#include "platform/log/log.h"
#include "srp/platform/sm/StateManagerHandler.h"

namespace srp {
namespace sm {

class ComWrapper : public ::platform::exec::sm::ISmComWrapper {
 private:
  const srp::platform::sm::StateManagerProxy::Handler& handler_;

 public:
  /**
   * @brief Sending/providing new status to destination
   *
   * @pplatformm new_state new state id
   * @return int8_t 0-OK
   */
  int8_t SendNewState(const uint16_t& new_state) noexcept override {
    if (handler_) {
      const auto res = handler_->SetState(new_state);
      if (!res.HasValue()) {
        ::platform::log::LogError() << "Tx error: " << res.Error().Message();
      }

      return 0;
    }
    return -1;
  }
  /**
   * @brief Set the Dtc Status
   *
   * @pplatformm new_status new status
   * @return int8_t  0-OK
   */
  int8_t SetDtcStatus(const bool& new_status) noexcept override { return 0; }
  explicit ComWrapper(
      srp::platform::sm::StateManagerProxy::Handler& handler)  // NOLINT
      : handler_{handler} {}
  ~ComWrapper() = default;
};
}  // namespace sm
}  // namespace srp
#endif  // APPS_COMMON_STATE_MANAGER_CODE_COM_WRAPPER_H_
