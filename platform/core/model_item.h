/**
 * @file model_item.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-09-01
 *
 * @copyright Copyright (c) 2024
 *
 */
#ifndef PLATFORM_CORE_MODEL_ITEM_H_
#define PLATFORM_CORE_MODEL_ITEM_H_
#include <optional>
#include <variant>

#include "platform/core/model/com_model_item.h"
#include "platform/core/model/diag_model_item.h"

namespace platform {
namespace core {
class ModelItem {
 private:
  std::variant<model::ModelCom, model::ModelDtc, model::ModelUds, model::SomeIpBindModel> value_;

 public:
  template <typename T>
  ModelItem(const T& value) : value_{value} {}  // NOLINT
  template <typename T>
  std::optional<T> GetValue() const;
  ~ModelItem() = default;
};
}  // namespace core
}  // namespace platform

#endif  // PLATFORM_CORE_MODEL_ITEM_H_
