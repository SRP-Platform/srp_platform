/**
 * Copyright (c) 2024
 *
 * @file radio_driver_loader.h
 * @brief Registry of radio drivers (factories by id) and the `.so` plugin loader (dlopen).
 *
 * Order in the daemon: AddBuiltin(serial_radio) -> LoadDirectory(directory from the config) ->
 * check that every link has a driver -> Create(). Start errors (duplicate id between plugins,
 * wrong API version, a file without plugin symbols, an unknown driver in a link) are returned as
 * one error listing every problem and logged separately (Error).
 *
 * Plugin libraries are never unloaded (no dlclose): drivers, factories in the registry and error
 * codes refer to their code, so the destruction order of objects does not matter.
 *
 * The registry and the loader are used from one thread at a time (the daemon start).
 */
#ifndef ARA_COM_MAVLINK_RADIO_DRIVER_LOADER_H_
#define ARA_COM_MAVLINK_RADIO_DRIVER_LOADER_H_

#include <map>
#include <set>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ara/com/mavlink/mavlink_log.h"
#include "ara/com/mavlink/radio_driver.h"
#include "ara/core/result.h"

namespace ara {
namespace com {
namespace mavlink {

class RadioDriverRegistry final : public IRadioDriverRegistry {
 public:
  explicit RadioDriverRegistry(LogFn log = {}) : log_{std::move(log)} {}

  /// Built-in driver (source "built-in"); a duplicate id is an error.
  ara::core::Result<void> AddBuiltin(const std::string& id, RadioDriverFactory factory);

  /// IRadioDriverRegistry: registration from the plugin set by BeginSource().
  ara::core::Result<void> Register(const std::string& id, RadioDriverFactory factory) noexcept override;

  /// Sets the source of the following Register() calls (the plugin path).
  void BeginSource(const std::string& source);
  /// Registration errors collected since the last call (duplicate ids); the loader uses it to
  /// notice a plugin that ignored a failed Register().
  std::vector<std::string> TakeErrors();
  std::size_t size() const { return entries_.size(); }
  /// Creates a driver (nullptr from the factory = error). Unknown id = kPluginError.
  ara::core::Result<std::shared_ptr<IRadioDriver>> Create(const std::string& id,
                                                          const std::string& config_path) const;

 private:
  struct Entry {
    RadioDriverFactory factory;
    std::string source;
    bool builtin{false};
  };
  ara::core::Result<void> Add(const std::string& id, RadioDriverFactory factory, bool builtin);
  void Log(LogLevel l, const std::string& m) const;

  LogFn log_;
  std::map<std::string, Entry> entries_;
  std::string current_source_{"built-in"};
  std::vector<std::string> errors_;
};

class RadioDriverLoader {
 public:
  explicit RadioDriverLoader(RadioDriverRegistry* registry, LogFn log = {})
      : registry_{registry}, log_{std::move(log)} {}
  ~RadioDriverLoader();
  RadioDriverLoader(const RadioDriverLoader&) = delete;
  RadioDriverLoader& operator=(const RadioDriverLoader&) = delete;

  /// Loads every `*.so` of the directory (alphabetical order); plugins that are already loaded
  /// are skipped. An empty directory is OK; a missing directory is an error. The error lists all
  /// problems (each also in the log); valid plugins are loaded despite the failure of others
  /// (the start of the daemon is governed by the error anyway).
  ara::core::Result<void> LoadDirectory(const std::string& dir);
  /// Loads one plugin.
  ara::core::Result<void> LoadPlugin(const std::string& path);

 private:
  void Log(LogLevel l, const std::string& m) const;

  RadioDriverRegistry* registry_;
  LogFn log_;
  std::set<std::string> loaded_;  // plugin paths already loaded (the libraries are never closed)
};

}  // namespace mavlink
}  // namespace com
}  // namespace ara

#endif  // ARA_COM_MAVLINK_RADIO_DRIVER_LOADER_H_
