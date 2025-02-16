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
#ifndef ARA_COM_INSTANCE_IDENTIFIER_CONTAINER_H_
#define ARA_COM_INSTANCE_IDENTIFIER_CONTAINER_H_
#include <map>
#include <string>
#include "ara/com/model/endpoint_model.h"
#include "ara/com/model/service_model.h"

namespace ara {
namespace com {
struct InstanceIdentifierContainer {
  const ara::com::model::ServiceModel service_model_;
  const std::map<std::string, ara::com::model::EndpointModel> endpoints_;
};
}  // namespace com
}  // namespace ara
#endif  // ARA_COM_INSTANCE_IDENTIFIER_CONTAINER_H_
