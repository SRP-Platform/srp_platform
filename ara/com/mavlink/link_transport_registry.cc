/**
 * @file link_transport_registry.cc
 * @copyright Copyright (c) 2024
 */
#include "ara/com/mavlink/link_transport_registry.h"

#include <utility>

#include "ara/com/mavlink/transport_error.h"
#include "ara/core/model/mavlink_model_item.h"
#include "ara/core/model_db.h"

namespace ara {
namespace com {
namespace mavlink {

ara::core::Result<void> LinkTransportRegistry::Register(
    const std::string& name, std::shared_ptr<IByteTransport> transport) {
  if (!transport) {
    return MakeTransportError(TransportErrc::kInvalidArgument,
                              "transport == nullptr (łącze '" + name + "')");
  }
  if (validator_) {
    const auto v = validator_(name);
    if (!v.HasValue()) {
      return v.Error();
    }
  }
  {
    std::lock_guard<std::mutex> lock{mutex_};
    if (items_.find(name) != items_.end()) {
      return MakeTransportError(TransportErrc::kAlreadyRegistered,
                                "łącze '" + name + "' ma już transport");
    }
    items_.emplace(name, std::move(transport));
  }
  cv_.notify_all();
  return {};
}

ara::core::Result<void> LinkTransportRegistry::Unregister(
    const std::string& name) {
  std::shared_ptr<IByteTransport> tr;
  UnregisterHook hook;
  {
    std::lock_guard<std::mutex> lock{mutex_};
    const auto it = items_.find(name);
    if (it == items_.end()) {
      return MakeTransportError(TransportErrc::kUnknownLink,
                                "łącze '" + name + "' nie ma transportu");
    }
    tr = std::move(it->second);
    items_.erase(it);
    hook = unregister_hook_;
  }
  tr->Close();
  if (hook) {
    hook(name);
  }
  return {};
}

std::shared_ptr<IByteTransport> LinkTransportRegistry::Get(
    const std::string& name) const {
  std::lock_guard<std::mutex> lock{mutex_};
  const auto it = items_.find(name);
  return it == items_.end() ? nullptr : it->second;
}

std::shared_ptr<IByteTransport> LinkTransportRegistry::WaitFor(
    const std::string& name, std::chrono::milliseconds timeout,
    std::stop_token token) {
  std::unique_lock<std::mutex> lock{mutex_};
  cv_.wait_for(lock, token, timeout,
               [&] { return items_.find(name) != items_.end(); });
  const auto it = items_.find(name);
  return it == items_.end() ? nullptr : it->second;
}

void LinkTransportRegistry::Clear() {
  std::lock_guard<std::mutex> lock{mutex_};
  items_.clear();
}

void LinkTransportRegistry::SetUnregisterHook(UnregisterHook hook) {
  std::lock_guard<std::mutex> lock{mutex_};
  unregister_hook_ = std::move(hook);
}

namespace {
ara::core::Result<void> ValidateAgainstModel(const std::string& name) {
  bool found = false;
  ara::core::ModelDataBase::GetInstance().ForEach(
      [&](const ara::core::StringView&, const ara::core::ModelItem& item) {
        const auto m = item.GetValue<ara::core::model::ModelMavlink>();
        if (!m.has_value()) {
          return;
        }
        for (const auto& l : m->links_) {
          if (l.name == name) {
            found = true;
          }
        }
      });
  if (!found) {
    return MakeTransportError(TransportErrc::kUnknownLink,
                              "łącze '" + name + "' nie występuje w modelu");
  }
  return {};
}
}  // namespace

LinkTransportRegistry& GlobalLinkTransportRegistry() {
  static LinkTransportRegistry registry{ValidateAgainstModel};
  return registry;
}

ara::core::Result<void> RegisterLinkTransport(
    const std::string& name, std::shared_ptr<IByteTransport> transport) {
  return GlobalLinkTransportRegistry().Register(name, std::move(transport));
}

ara::core::Result<void> UnregisterLinkTransport(const std::string& name) {
  return GlobalLinkTransportRegistry().Unregister(name);
}

void ClearLinkTransports() { GlobalLinkTransportRegistry().Clear(); }

}  // namespace mavlink
}  // namespace com
}  // namespace ara
