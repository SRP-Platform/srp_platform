/**
 * @file com_model_item.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-09-01
 *
 * @copyright Copyright (c) 2024
 *
 */
#ifndef PLATFORM_CORE_MODEL_COM_MODEL_ITEM_H_
#define PLATFORM_CORE_MODEL_COM_MODEL_ITEM_H_

#include <string>

#include "platform/com/instance_identifier_container.h"
#include "platform/core/result.h"

namespace platform {
namespace core {
namespace model {

class ModelCom {
 public:
  enum ComType : uint8_t { kIPC = 0 };

  const platform::com::InstanceIdentifierContainer container_;
  const ComType type_;

 public:
  ModelCom(const platform::com::InstanceIdentifierContainer& container,
           const ComType type)
      : container_{container}, type_{type} {}
};
}  // namespace model
}  // namespace core
}  // namespace platform

#endif  // PLATFORM_CORE_MODEL_COM_MODEL_ITEM_H_
