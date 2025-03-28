/**
 * @file sd_component.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-02-23
 *
 * @copyright Copyright (c) 2025
 *
 */
#ifndef PLATFORM_COM_INTERNAL_SD_CONTROLLER_H_
#define PLATFORM_COM_INTERNAL_SD_CONTROLLER_H_

#include <cstdint>
#include <functional>
#include <memory>
#include "bindings/com/ipc/skeleton/ipc_skeleton.h"
#include "platform/com/com_wrapper.h"
#include "platform/com/internal/sd_component.h"

namespace platform {
namespace com {
namespace sd {

class SdController {
 public:
  static std::unique_ptr<SdProxyComponent> StartFindServiceImpl(
      const uint32_t& type) noexcept;
  static std::shared_ptr<wrapper::ComWrapper> StartOfferServiceImpl(
      const uint32_t& type, const uint16_t service_id_,
      const uint16_t instance_id_, const uint8_t major_version_,
      const uint32_t minor_version_) noexcept;
};

}  // namespace sd
}  // namespace com
}  // namespace platform

#endif  // PLATFORM_COM_INTERNAL_SD_CONTROLLER_H_
