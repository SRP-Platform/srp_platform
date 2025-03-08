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
#ifndef PLATFORM_COM_INTERNAL_SD_COMPONENT_H_
#define PLATFORM_COM_INTERNAL_SD_COMPONENT_H_

#include <cstdint>
#include <functional>

namespace platform {
namespace com {
namespace sd {
class SdComponent {
 public:
  struct SdFindEntry {
    const uint16_t service_id_;
    const uint16_t instance_id_;
    const uint32_t src_id_;
  };

  using SDFindCallback = std::function<void(const SdFindEntry&)>;

  struct SdEntry {
    const uint16_t service_id_;
    const uint16_t instance_id_;
    const uint8_t major_version_;
    const uint32_t minor_version_;
  };

  virtual void StartFindService(const SdEntry& entry,
                                SDFindCallback&& callback) noexcept = 0;

  virtual void Stop() noexcept = 0;
  virtual ~SdComponent() = default;
};
}  // namespace sd
}  // namespace com
}  // namespace platform

#endif  // PLATFORM_COM_INTERNAL_SD_COMPONENT_H_
