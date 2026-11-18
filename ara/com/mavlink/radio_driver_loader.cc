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
    return PluginError("driver '" + id + "': exception during registration");
  }
}

ara::core::Result<void> RadioDriverRegistry::Add(const std::string& id, RadioDriverFactory factory,
                                                 bool builtin) {
  std::string error;
  {
    const std::string source = builtin ? "built-in" : current_source_;
    if (id.empty() || !factory) {
      error = source + ": empty driver id or no factory";
    } else {
      const auto it = entries_.find(id);
      if (it == entries_.end()) {
        entries_[id] = Entry{std::move(factory), source, builtin};
        return {};
      }
      if (!builtin && it->second.builtin) {
        Log(LogLevel::kInfo, "plugin " + source + " replaces the built-in driver '" + id + "'");
        it->second = Entry{std::move(factory), source, false};
        return {};
      }
      error = "duplicate driver id '" + id + "': " + source + " and " + it->second.source;
    }
    errors_.push_back(error);
  }
  Log(LogLevel::kError, error);
  return PluginError(error);
}

void RadioDriverRegistry::BeginSource(const std::string& source) { current_source_ = source; }

std::vector<std::string> RadioDriverRegistry::TakeErrors() {
  std::vector<std::string> out;
  out.swap(errors_);
  return out;
}

ara::core::Result<std::shared_ptr<IRadioDriver>> RadioDriverRegistry::Create(
    const std::string& id, const std::string& config_path) const {
  const auto it = entries_.find(id);
  if (it == entries_.end()) {
    std::string known;
    for (const auto& kv : entries_) {
      known += (known.empty() ? "" : ", ") + kv.first;
    }
    return PluginError("unknown driver '" + id + "' (known: " + (known.empty() ? "none" : known) + ")");
  }
  const RadioDriverFactory& factory = it->second.factory;
  std::shared_ptr<IRadioDriver> driver;
  try {
    driver = factory(config_path);
  } catch (const std::exception& e) {
    return PluginError("driver '" + id + "': exception while creating: " + e.what());
  } catch (...) {
    return PluginError("driver '" + id + "': exception while creating");
  }
  if (!driver) {
    return PluginError("driver '" + id + "' was not created (config file '" + config_path + "')");
  }
  return driver;
}

// ============================================================== RadioDriverLoader

RadioDriverLoader::~RadioDriverLoader() {
  // Deliberately no dlclose(): plugin code backs the std::function in the registry, ErrorCode
  // (a reference to the error domain) and the driver threads — unloading it before they are
  // destroyed would crash. The libraries live until the process exits (usual for plugins).
}

void RadioDriverLoader::Log(LogLevel l, const std::string& m) const {
  if (log_) {
    log_(l, "[mavlink] " + m);
  }
}

ara::core::Result<void> RadioDriverLoader::LoadPlugin(const std::string& path) {
  if (loaded_.count(path) != 0) {
    return {};  // already loaded (a daemon restart)
  }
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
  // dlsym returns void*; casting it to a function pointer is what POSIX requires.
  using VersionFn = std::uint32_t (*)();
  using RegisterFn = void (*)(IRadioDriverRegistry*);
  const auto version_fn = reinterpret_cast<VersionFn>(::dlsym(h, kPluginVersionSymbol));  // NOLINT
  if (version_fn == nullptr) {
    return fail(std::string("not a driver plugin (symbol ") + kPluginVersionSymbol + " is missing)");
  }
  const std::uint32_t version = version_fn();
  if (version != kRadioDriverApiVersion) {
    return fail("API version " + std::to_string(version) + " != " + std::to_string(kRadioDriverApiVersion) +
                " required by the daemon");
  }
  const auto register_fn = reinterpret_cast<RegisterFn>(::dlsym(h, kPluginRegisterSymbol));  // NOLINT
  if (register_fn == nullptr) {
    return fail(std::string("missing symbol ") + kPluginRegisterSymbol);
  }
  // From now on factories of the plugin may reach the registry: the library stays loaded.
  registry_->TakeErrors();
  const std::size_t ids_before = registry_->size();
  registry_->BeginSource(path);
  try {
    register_fn(registry_);
  } catch (const std::exception& e) {
    const std::string msg = "plugin " + path + ": exception during registration: " + e.what();
    Log(LogLevel::kError, msg);
    return PluginError(msg);
  } catch (...) {
    const std::string msg = "plugin " + path + ": exception during registration";
    Log(LogLevel::kError, msg);
    return PluginError(msg);
  }
  const auto errors = registry_->TakeErrors();
  if (!errors.empty()) {
    std::string msg = "plugin " + path + ":";
    for (const auto& e : errors) {
      msg += " " + e + ";";
    }
    return PluginError(msg);  // the registry logged the details
  }
  if (registry_->size() == ids_before) {
    Log(LogLevel::kWarn, "plugin " + path + " registered no driver (or only replaced a built-in one)");
  }
  loaded_.insert(path);  // only a plugin that registered cleanly is skipped on a later start
  Log(LogLevel::kInfo, "loaded plugin " + path);
  return {};
}

ara::core::Result<void> RadioDriverLoader::LoadDirectory(const std::string& dir) {
  DIR* d = ::opendir(dir.c_str());
  if (d == nullptr) {
    const std::string msg = "driver directory '" + dir + "': " + std::strerror(errno);
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
