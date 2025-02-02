/**
 * @file sd_controller.cc
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-03-28
 *
 * @copyright Copyright (c) 2025
 *
 */

#include "platform/com/internal/sd_controller.h"

#include "bindings/com/ipc/proxy/ipc_service_discover_proxy.h"
#include "platform/core/model/com_model_item.h"

namespace platform {
namespace com {
namespace sd {

std::unique_ptr<SdProxyComponent> SdController::StartFindServiceImpl(
    const uint32_t& type) noexcept {
  switch (type) {
    case platform::core::model::ModelCom::ComType::kIPC:
      return std::make_unique<
          srp::bindings::com::proxy::IpcServiceDiscover_Proxy>();
      break;

    default:
      return nullptr;
      break;
  }
}
std::shared_ptr<wrapper::ComWrapper> SdController::StartOfferServiceImpl(
    const uint32_t& type, const uint16_t service_id_,
    const uint16_t instance_id_, const uint8_t major_version_,
    const uint32_t minor_version_) noexcept {
  switch (type) {
    case platform::core::model::ModelCom::ComType::kIPC:
      return std::make_shared<srp::bindings::com::ipc::skeleton::IpcSkeleton>(
          service_id_, instance_id_);
      break;

    default:
      return nullptr;
      break;
  }
}
}  // namespace sd
}  // namespace com
}  // namespace platform
