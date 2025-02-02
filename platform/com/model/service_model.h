/**
 * @file service_model.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-01-23
 *
 * @copyright Copyright (c) 2025
 *
 */
#ifndef PLATFORM_COM_MODEL_SERVICE_MODEL_H_
#define PLATFORM_COM_MODEL_SERVICE_MODEL_H_
#include <cstdint>

namespace platform {
namespace com {
namespace model {

struct ServiceModel {
  constexpr ServiceModel(const uint16_t service_id, const uint16_t instance_id)
      : service_id_{service_id}, instance_id_{instance_id} {}

  const uint16_t service_id_;
  const uint16_t instance_id_;
};
}  // namespace model
}  // namespace com
}  // namespace platform

#endif  // PLATFORM_COM_MODEL_SERVICE_MODEL_H_
