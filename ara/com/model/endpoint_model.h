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

#ifndef ARA_COM_MODEL_ENDPOINT_MODEL_H_
#define ARA_COM_MODEL_ENDPOINT_MODEL_H_

#include <cstdint>

namespace ara {
namespace com {
namespace model {
struct EndpointModel {
  EndpointModel(const uint16_t endpoint_id) : endpoint_id_{endpoint_id} {}

  const uint16_t endpoint_id_;
};

}  // namespace model
}  // namespace com
}  // namespace ara

#endif  // ARA_COM_MODEL_ENDPOINT_MODEL_H_
