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
#ifndef ARA_COM_MODEL_SERVICE_MODEL_H_
#define ARA_COM_MODEL_SERVICE_MODEL_H_
#include <cstdint>

namespace ara {
namespace com {
namespace model {

struct ServiceModel {
  constexpr ServiceModel(const uint16_t service_id, const uint16_t instance_id,
                         const uint8_t major_version,
                         const uint32_t minor_version)
      : service_id_{service_id},
        instance_id_{instance_id},
        major_version_{major_version},
        minor_version_{minor_version} {}

  const uint16_t service_id_;
  const uint16_t instance_id_;
  const uint8_t major_version_;
  const uint32_t minor_version_;
};
}  // namespace model
}  // namespace com
}  // namespace ara

#endif  // ARA_COM_MODEL_SERVICE_MODEL_H_
