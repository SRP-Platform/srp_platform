/**
 * Copyright (c) 2024
 *
 * @file radio_driver_loader.h
 * @brief Rejestr driverów radia (fabryki po id) i loader pluginów `.so` (dlopen).
 *
 * Kolejność w daemonie: AddBuiltin(serial_radio) -> LoadDirectory(katalog z configu) ->
 * sprawdzenie, że każde łącze ma driver -> Create(). Błędy startu (duplikat id między
 * pluginami, zła wersja API, plik bez symboli pluginu, nieznany driver w łączu) są
 * zwracane jako jeden błąd z pełną listą problemów i logowane osobno (Error).
 *
 * Biblioteki pluginów nie są zdejmowane z pamięci (bez dlclose): drivery, fabryki w rejestrze
 * i kody błędów odwołują się do ich kodu, więc kolejność niszczenia obiektów nie ma znaczenia.
 */
#ifndef ARA_COM_MAVLINK_RADIO_DRIVER_LOADER_H_
#define ARA_COM_MAVLINK_RADIO_DRIVER_LOADER_H_

#include <map>
#include <memory>
#include <mutex>  // NOLINT
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

  /// Wbudowany driver (źródło "built-in"); duplikat id = błąd.
  ara::core::Result<void> AddBuiltin(const std::string& id, RadioDriverFactory factory);

  /// IRadioDriverRegistry: rejestracja z pluginu ustawionego przez BeginSource().
  ara::core::Result<void> Register(const std::string& id, RadioDriverFactory factory) noexcept override;

  /// Ustawia źródło kolejnych Register() (ścieżka pluginu); sekwencyjnie, z jednego wątku.
  void BeginSource(const std::string& source);
  /// Błędy rejestracji zebrane od początku (duplikaty id).
  std::vector<std::string> Errors() const;

  bool Has(const std::string& id) const;
  std::vector<std::string> Ids() const;
  /// Skąd pochodzi driver ("built-in" albo ścieżka pluginu); "" = brak.
  std::string SourceOf(const std::string& id) const;
  /// Tworzy driver (nullptr z fabryki = błąd). Nieznane id = kPluginError.
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
  mutable std::mutex mutex_;
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

  /// Ładuje wszystkie `*.so` z katalogu (kolejność alfabetyczna). Pusty katalog = OK;
  /// brak katalogu = błąd. Błąd zawiera wszystkie problemy (każdy też w logu); pluginy
  /// poprawne są załadowane mimo błędu innych (startem daemona i tak steruje błąd).
  ara::core::Result<void> LoadDirectory(const std::string& dir);
  /// Ładuje jeden plugin.
  ara::core::Result<void> LoadPlugin(const std::string& path);

 private:
  void Log(LogLevel l, const std::string& m) const;

  RadioDriverRegistry* registry_;
  LogFn log_;
  std::vector<void*> handles_;
};

}  // namespace mavlink
}  // namespace com
}  // namespace ara

#endif  // ARA_COM_MAVLINK_RADIO_DRIVER_LOADER_H_
