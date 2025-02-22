/**
 * @file initialization.cc
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-11-26
 *
 * @copyright Copyright (c) 2024
 *
 */
#include "ara/com/initialization.h"

#include "bindings/common/controller/controller.h"
#include "ara/core/result.h"
namespace ara {
namespace com {
ara::core::Result<void> Initialize(const uint32_t& app_id) noexcept {
  auto& controller = srp::bindings::Controller::GetInstance(app_id);

  controller.Init();
  return {};
}

ara::core::Result<void> Deinitialize() noexcept { return {}; }
}  // namespace com
}  // namespace ara
