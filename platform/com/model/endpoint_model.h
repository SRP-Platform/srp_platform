/**
 * @file endpoint_model.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-01-23
 *
 * @copyright Copyright (c) 2025
 *
 */

#ifndef PLATFORM_COM_MODEL_ENDPOINT_MODEL_H_
#define PLATFORM_COM_MODEL_ENDPOINT_MODEL_H_

#include <cstdint>

namespace platform {
namespace com {
namespace model {
enum EndpointMode : uint8_t {
  kNormal = 0x00U,
  kNoReturn = 0x01U,
  kNoReturnWithAck = 0x02U,
  kEvent = 0x03U,
  kField = 0x04U,
};
struct EndpointModel {
  EndpointModel(const uint16_t endpoint_id,
                const EndpointMode mode = EndpointMode::kNormal)
      : endpoint_id_{endpoint_id}, mode_{mode} {}

  const uint16_t endpoint_id_;
  const EndpointMode mode_;
};

}  // namespace model
}  // namespace com
}  // namespace platform

#endif  // PLATFORM_COM_MODEL_ENDPOINT_MODEL_H_
