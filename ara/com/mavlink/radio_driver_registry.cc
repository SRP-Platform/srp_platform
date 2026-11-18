/**
 * Copyright (c) 2024
 *
 * @file radio_driver_registry.cc
 */
#include "ara/com/mavlink/radio_driver_registry.h"

#include <exception>
#include <tuple>

namespace ara {
namespace com {
namespace mavlink {
namespace {
ara::core::ErrorCode RegistryError(const std::string& what) {
  return MakeTransportError(TransportErrc::kDriverRegistryError, what);
}
}  // namespace

RadioDriverRegistry& RadioDriverRegistry::Global() {
  static RadioDriverRegistry registry;  // function-local: safe against static init order
  return registry;
}

ara::core::Result<void> RadioDriverRegistry::Register(const std::string& id,
                                                      RadioDriverFactory factory) noexcept {
  try {
    std::lock_guard<std::mutex> lock{mutex_};
    std::string error;
    if (id.empty() || !factory) {
      error = "empty driver id or no factory (id '" + id + "')";
    } else if (!entries_.emplace(id, std::move(factory)).second) {
      error = "duplicate driver id '" + id + "' (the driver is registered twice in this binary)";
    } else {
      return {};
    }
    errors_.push_back(error);
    return RegistryError(error);
  } catch (...) {
    return RegistryError("driver '" + id + "': exception during registration");
  }
}

std::vector<std::string> RadioDriverRegistry::Errors() const {
  std::lock_guard<std::mutex> lock{mutex_};
  return errors_;
}

std::vector<std::string> RadioDriverRegistry::Ids() const {
  std::lock_guard<std::mutex> lock{mutex_};
  std::vector<std::string> ids;
  for (const auto& kv : entries_) {
    ids.push_back(kv.first);
  }
  return ids;
}

ara::core::Result<std::shared_ptr<IRadioDriver>> RadioDriverRegistry::Create(
    const std::string& id, const std::string& config_path) const {
  RadioDriverFactory factory;
  {
    std::lock_guard<std::mutex> lock{mutex_};
    const auto it = entries_.find(id);
    if (it == entries_.end()) {
      std::string known;
      for (const auto& kv : entries_) {
        known += (known.empty() ? "" : ", ") + kv.first;
      }
      return RegistryError("unknown driver '" + id + "' (known: " + (known.empty() ? "none" : known) + ")");
    }
    factory = it->second;
  }
  std::shared_ptr<IRadioDriver> driver;
  try {
    driver = factory(config_path);
  } catch (const std::exception& e) {
    return RegistryError("driver '" + id + "': exception while creating: " + e.what());
  } catch (...) {
    return RegistryError("driver '" + id + "': exception while creating");
  }
  if (!driver) {
    return RegistryError("driver '" + id + "' was not created (config file '" + config_path + "')");
  }
  return driver;
}

}  // namespace mavlink
}  // namespace com
}  // namespace ara
