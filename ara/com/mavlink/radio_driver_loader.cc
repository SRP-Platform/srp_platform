/**
 * Copyright (c) 2024
 *
 * @file radio_driver_loader.cc
 */
#include "ara/com/mavlink/radio_driver_loader.h"

#include <dirent.h>
#include <dlfcn.h>
#include <sys/stat.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <exception>
#include <utility>

namespace ara {
namespace com {
namespace mavlink {
namespace {

ara::core::ErrorCode PluginError(const std::string& what) {
  return MakeTransportError(TransportErrc::kPluginError, what);
}

bool EndsWith(const std::string& s, const std::string& suffix) {
  return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

}  // namespace

// ============================================================== RadioDriverRegistry

void RadioDriverRegistry::Log(LogLevel l, const std::string& m) const {
  if (log_) {
    log_(l, "[mavlink] " + m);
  }
}

ara::core::Result<void> RadioDriverRegistry::AddBuiltin(const std::string& id,
                                                        RadioDriverFactory factory) {
  return Add(id, std::move(factory), true);
}

ara::core::Result<void> RadioDriverRegistry::Register(const std::string& id,
                                                      RadioDriverFactory factory) noexcept {
  try {
    return Add(id, std::move(factory), false);
  } catch (...) {
    return PluginError("driver '" + id + "': wyjątek przy rejestracji");
  }
}

ara::core::Result<void> RadioDriverRegistry::Add(const std::string& id, RadioDriverFactory factory,
                                                 bool builtin) {
  std::string error;
  {
    std::lock_guard<std::mutex> lock{mutex_};
    const std::string source = builtin ? "built-in" : current_source_;
    if (id.empty() || !factory) {
      error = source + ": pusty id drivera albo brak fabryki";
    } else {
      const auto it = entries_.find(id);
      if (it == entries_.end()) {
        entries_[id] = Entry{std::move(factory), source, builtin};
        return {};
      }
      if (!builtin && it->second.builtin) {
        Log(LogLevel::kInfo, "plugin " + source + " zastępuje wbudowany driver '" + id + "'");
        it->second = Entry{std::move(factory), source, false};
        return {};
      }
      error = "duplikat id drivera '" + id + "': " + source + " oraz " + it->second.source;
    }
    errors_.push_back(error);
  }
  Log(LogLevel::kError, error);
  return PluginError(error);
}

void RadioDriverRegistry::BeginSource(const std::string& source) {
  std::lock_guard<std::mutex> lock{mutex_};
  current_source_ = source;
}

std::vector<std::string> RadioDriverRegistry::Errors() const {
  std::lock_guard<std::mutex> lock{mutex_};
  return errors_;
}

bool RadioDriverRegistry::Has(const std::string& id) const {
  std::lock_guard<std::mutex> lock{mutex_};
  return entries_.count(id) != 0;
}

std::vector<std::string> RadioDriverRegistry::Ids() const {
  std::lock_guard<std::mutex> lock{mutex_};
  std::vector<std::string> ids;
  for (const auto& kv : entries_) {
    ids.push_back(kv.first);
  }
  return ids;
}

std::string RadioDriverRegistry::SourceOf(const std::string& id) const {
  std::lock_guard<std::mutex> lock{mutex_};
  const auto it = entries_.find(id);
  return it == entries_.end() ? std::string{} : it->second.source;
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
      return PluginError("nieznany driver '" + id + "' (znane: " + (known.empty() ? "brak" : known) + ")");
    }
    factory = it->second.factory;
  }
  std::shared_ptr<IRadioDriver> driver;
  try {
    driver = factory(config_path);
  } catch (const std::exception& e) {
    return PluginError("driver '" + id + "': wyjątek przy tworzeniu: " + e.what());
  } catch (...) {
    return PluginError("driver '" + id + "': wyjątek przy tworzeniu");
  }
  if (!driver) {
    return PluginError("driver '" + id + "' nie powstał (plik konfiguracji '" + config_path + "')");
  }
  return driver;
}

// ============================================================== RadioDriverLoader

RadioDriverLoader::~RadioDriverLoader() {
  // Celowo bez dlclose(): kod pluginu obsługuje std::function w rejestrze, ErrorCode (referencja
  // do domeny błędów) i wątki drivera — zdjęcie go z pamięci przed ich zniszczeniem to crash.
  // Biblioteki żyją do końca procesu (standardowa praktyka dla pluginów).
  handles_.clear();
}

void RadioDriverLoader::Log(LogLevel l, const std::string& m) const {
  if (log_) {
    log_(l, "[mavlink] " + m);
  }
}

ara::core::Result<void> RadioDriverLoader::LoadPlugin(const std::string& path) {
  void* h = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (h == nullptr) {
    const char* why = ::dlerror();
    const std::string msg = "plugin " + path + ": dlopen: " + (why != nullptr ? why : "?");
    Log(LogLevel::kError, msg);
    return PluginError(msg);
  }
  const auto fail = [&](const std::string& what) {
    const std::string msg = "plugin " + path + ": " + what;
    Log(LogLevel::kError, msg);
    ::dlclose(h);
    return PluginError(msg);
  };
  // dlsym zwraca void*; rzutowanie na wskaźnik funkcji jest wymagane przez POSIX.
  using VersionFn = std::uint32_t (*)();
  using RegisterFn = void (*)(IRadioDriverRegistry*);
  const auto version_fn = reinterpret_cast<VersionFn>(::dlsym(h, kPluginVersionSymbol));  // NOLINT
  if (version_fn == nullptr) {
    return fail(std::string("to nie jest plugin drivera (brak symbolu ") + kPluginVersionSymbol + ")");
  }
  const std::uint32_t version = version_fn();
  if (version != kRadioDriverApiVersion) {
    return fail("wersja API " + std::to_string(version) + " != " + std::to_string(kRadioDriverApiVersion) +
                " wymagana przez daemon");
  }
  const auto register_fn = reinterpret_cast<RegisterFn>(::dlsym(h, kPluginRegisterSymbol));  // NOLINT
  if (register_fn == nullptr) {
    return fail(std::string("brak symbolu ") + kPluginRegisterSymbol);
  }
  // Od tej chwili fabryki z pluginu mogą trafić do rejestru: uchwyt zostaje do końca loadera.
  handles_.push_back(h);
  const std::size_t errors_before = registry_->Errors().size();
  const std::size_t ids_before = registry_->Ids().size();
  registry_->BeginSource(path);
  try {
    register_fn(registry_);
  } catch (const std::exception& e) {
    const std::string msg = "plugin " + path + ": wyjątek przy rejestracji: " + e.what();
    Log(LogLevel::kError, msg);
    return PluginError(msg);
  } catch (...) {
    const std::string msg = "plugin " + path + ": wyjątek przy rejestracji";
    Log(LogLevel::kError, msg);
    return PluginError(msg);
  }
  const auto errors = registry_->Errors();
  if (errors.size() > errors_before) {
    std::string msg = "plugin " + path + ":";
    for (std::size_t i = errors_before; i < errors.size(); ++i) {
      msg += " " + errors[i] + ";";
    }
    return PluginError(msg);  // szczegóły zalogował rejestr
  }
  if (registry_->Ids().size() == ids_before) {
    Log(LogLevel::kWarn, "plugin " + path + " nie zarejestrował żadnego drivera (albo tylko podmienił wbudowany)");
  }
  Log(LogLevel::kInfo, "załadowano plugin " + path);
  return {};
}

ara::core::Result<void> RadioDriverLoader::LoadDirectory(const std::string& dir) {
  DIR* d = ::opendir(dir.c_str());
  if (d == nullptr) {
    const std::string msg = "katalog driverów '" + dir + "': " + std::strerror(errno);
    Log(LogLevel::kError, msg);
    return PluginError(msg);
  }
  std::vector<std::string> files;
  while (const dirent* e = ::readdir(d)) {
    const std::string name = e->d_name;
    if (!EndsWith(name, ".so")) {
      continue;
    }
    const std::string path = dir + (EndsWith(dir, "/") ? "" : "/") + name;
    struct stat st {};
    if (::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
      files.push_back(path);
    }
  }
  ::closedir(d);
  std::sort(files.begin(), files.end());
  std::string problems;
  for (const auto& f : files) {
    const auto r = LoadPlugin(f);
    if (!r.HasValue()) {
      problems += (problems.empty() ? "" : "\n") + r.Error().SupportData();
    }
  }
  if (!problems.empty()) {
    return PluginError(problems);
  }
  return {};
}

}  // namespace mavlink
}  // namespace com
}  // namespace ara
