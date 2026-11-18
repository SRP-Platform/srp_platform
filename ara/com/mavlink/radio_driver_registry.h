/**
 * Copyright (c) 2024
 *
 * @file radio_driver_registry.h
 * @brief Registry of radio drivers (factories by id) filled statically at program start.
 *
 * Drivers are linked into the daemon binary (see `mavlink_daemon_binary` in
 * platform/common/mavlink_daemon/defs.bzl); each driver library registers itself with
 * `SRP_MAVLINK_REGISTER_DRIVER("id", factory)` in a global object, so the library must be
 * `alwayslink = True` (otherwise the linker drops the unreferenced object file and the driver
 * is silently missing).
 *
 * A registration runs before main(), so it cannot return an error: a duplicate or an empty id
 * is remembered in Errors() and the daemon refuses to start (the error names the id). The first
 * registration of an id stays in place.
 *
 * Registration happens during static initialization (one thread); the registry is also locked,
 * so the daemon may use it from any thread afterwards.
 */
#ifndef ARA_COM_MAVLINK_RADIO_DRIVER_REGISTRY_H_
#define ARA_COM_MAVLINK_RADIO_DRIVER_REGISTRY_H_

#include <map>
#include <memory>
#include <mutex>  // NOLINT
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "ara/com/mavlink/mavlink_log.h"
#include "ara/com/mavlink/radio_driver.h"
#include "ara/core/result.h"

namespace ara {
namespace com {
namespace mavlink {

class RadioDriverRegistry {
 public:
  RadioDriverRegistry() = default;
  RadioDriverRegistry(const RadioDriverRegistry&) = delete;
  RadioDriverRegistry& operator=(const RadioDriverRegistry&) = delete;

  /// The process-wide registry filled by SRP_MAVLINK_REGISTER_DRIVER.
  static RadioDriverRegistry& Global();

  /// Registers a factory. Error: empty id / no factory / id already taken. Every error is also
  /// remembered in Errors().
  ara::core::Result<void> Register(const std::string& id, RadioDriverFactory factory) noexcept;

  /// Registration errors so far (duplicate ids etc.), one text per error.
  std::vector<std::string> Errors() const;

  /// Ids of the registered drivers (sorted).
  std::vector<std::string> Ids() const;

  /// Creates a driver (nullptr from the factory = error). Unknown id = kDriverRegistryError listing the
  /// known ids.
  ara::core::Result<std::shared_ptr<IRadioDriver>> Create(const std::string& id,
                                                          const std::string& config_path) const;

 private:
  mutable std::mutex mutex_;
  std::map<std::string, RadioDriverFactory> entries_;
  std::vector<std::string> errors_;
};

/// Registers a driver in the global registry from a static initializer.
class DriverRegistrar {
 public:
  DriverRegistrar(const char* id, RadioDriverFactory factory) {
    std::ignore = RadioDriverRegistry::Global().Register(id != nullptr ? id : "", std::move(factory));
  }
};

}  // namespace mavlink
}  // namespace com
}  // namespace ara

#define SRP_MAVLINK_CONCAT_INNER(a, b) a##b
#define SRP_MAVLINK_CONCAT(a, b) SRP_MAVLINK_CONCAT_INNER(a, b)

/// Registers a radio driver: `id` is the value of `links.<name>.driver` in the daemon config,
/// the rest is the factory, `ara::com::mavlink::RadioDriverFactory`
/// (`std::shared_ptr<IRadioDriver>(const std::string& driver_config)`; variadic, so the commas of a
/// lambda capture list are safe). Use at namespace scope in
/// a .cc file of a library with `alwayslink = True`. Example:
/// @code
///   SRP_MAVLINK_REGISTER_DRIVER("my_radio", [](const std::string& cfg) {
///     return std::shared_ptr<ara::com::mavlink::IRadioDriver>{std::make_shared<MyRadio>(cfg)};
///   });
/// @endcode
#define SRP_MAVLINK_REGISTER_DRIVER(id, ...)                                                \
  static const ::ara::com::mavlink::DriverRegistrar SRP_MAVLINK_CONCAT(                     \
      srp_mavlink_driver_registrar_, __COUNTER__) {                                         \
    id, __VA_ARGS__                                                                         \
  }

#endif  // ARA_COM_MAVLINK_RADIO_DRIVER_REGISTRY_H_
