/**
 * @file model_item.cc
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief 
 * @version 0.1
 * @date 2024-09-01
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#include "platform/core/model_item.h"

namespace platform {
namespace core {

template <>
std::optional<model::ModelCom> ModelItem::GetValue() const {
  if (value_.index() == 0) {
    return std::get<model::ModelCom>(value_);
  }
  return std::nullopt;
}

template <>
std::optional<model::ModelDtc> ModelItem::GetValue() const {
  if (value_.index() == 1) {
    return std::get<model::ModelDtc>(value_);
  }
  return std::nullopt;
}

template <>
std::optional<model::ModelUds> ModelItem::GetValue() const {
  if (value_.index() == 2) {
    return std::get<model::ModelUds>(value_);
  }
  return std::nullopt;
}

template <>
std::optional<model::SomeIpBindModel> ModelItem::GetValue() const {
  if (value_.index() == 3) {
    return std::get<model::SomeIpBindModel>(value_);
  }
  return std::nullopt;
}

}  // namespace core
}  // namespace platform
