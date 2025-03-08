/**
 * @file model_db.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief 
 * @version 0.1
 * @date 2024-09-01
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#ifndef PLATFORM_CORE_MODEL_DB_H_
#define PLATFORM_CORE_MODEL_DB_H_

#include <unordered_map>
#include <utility>

#include "platform/core/core_error_domain.h"
#include "platform/core/instance_specifier.h"
#include "platform/core/model_item.h"
#include "platform/core/result.h"
#include "platform/core/string_view.h"
namespace platform {
namespace core {
class ModelDataBase {
 private:
  std::unordered_map<StringView, ModelItem> db_{};

 public:
  ModelDataBase(/* args */) = default;

  template <typename T>
  Result<T> ResolveInstanceSpecifier(InstanceSpecifier instance) {
    const auto model_item = db_.find(instance.ToString());
    if (model_item == db_.cend()) {
      return MakeErrorCode(CoreErrc::kInvalidMetaModelPath,
                           "Instance: " + instance.ToString() + " not found");
    }
    const auto res = model_item->second.GetValue<T>();
    if (res.has_value()) {
      return res.value();
    }
    return MakeErrorCode(CoreErrc::kInvalidArgument, "Invalid Expect Type");
  }
  Result<void> AddNewItem(StringView instance, ModelItem item) {
    if (db_.find(instance) != db_.end()) {
      return MakeErrorCode(CoreErrc::kInvalidArgument,
                           " Instance: " + instance + " already exist");
    }
    db_.insert(std::make_pair(instance, item));
    return {};
  }
  static ModelDataBase& GetInstance() {
    static ModelDataBase db_{};
    return db_;
  }
};
}  // namespace core
}  // namespace platform

#endif  // PLATFORM_CORE_MODEL_DB_H_
