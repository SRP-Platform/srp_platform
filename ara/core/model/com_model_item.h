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
#ifndef ARA_CORE_MODEL_COM_MODEL_ITEM_H_
#define ARA_CORE_MODEL_COM_MODEL_ITEM_H_

#include <string>

#include "ara/com/instance_identifier_container.h"
#include "ara/core/result.h"

namespace ara {
namespace core {
namespace model {

class ModelCom {
 public:
  const ara::com::InstanceIdentifierContainer container_;
  const std::uint16_t instance_;

 public:
  ModelCom(const ara::com::InstanceIdentifierContainer& container,
           std::uint16_t instance)
      : container_{container}, instance_{instance} {}
};
}  // namespace model
}  // namespace core
}  // namespace ara

#endif  // ARA_CORE_MODEL_COM_MODEL_ITEM_H_
