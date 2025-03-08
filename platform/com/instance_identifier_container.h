/**
 * @file instance_identifier_container.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-01-23
 *
 * @copyright Copyright (c) 2025
 *
 */
#ifndef PLATFORM_COM_INSTANCE_IDENTIFIER_CONTAINER_H_
#define PLATFORM_COM_INSTANCE_IDENTIFIER_CONTAINER_H_
#include <map>
#include <string>
#include "platform/com/model/endpoint_model.h"
#include "platform/com/model/service_model.h"

namespace platform {
namespace com {
struct InstanceIdentifierContainer {
  const platform::com::model::ServiceModel service_model_;
  const std::map<std::string, platform::com::model::EndpointModel> endpoints_;
};
}  // namespace com
}  // namespace platform
#endif  // PLATFORM_COM_INSTANCE_IDENTIFIER_CONTAINER_H_
