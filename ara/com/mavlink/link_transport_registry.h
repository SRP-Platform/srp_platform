/**
 * @file link_transport_registry.h
 * @brief Rejestr transportów łączy (RegisterLinkTransport).
 * @copyright Copyright (c) 2024
 */
#ifndef ARA_COM_MAVLINK_LINK_TRANSPORT_REGISTRY_H_
#define ARA_COM_MAVLINK_LINK_TRANSPORT_REGISTRY_H_

#include <chrono>  // NOLINT
#include <condition_variable>  // NOLINT
#include <functional>
#include <map>
#include <memory>
#include <mutex>  // NOLINT
#include <stop_token>  // NOLINT
#include <string>
#include <utility>

#include "ara/com/mavlink/i_byte_transport.h"
#include "ara/core/result.h"

namespace ara {
namespace com {
namespace mavlink {

class LinkTransportRegistry {
 public:
  /// Zwraca sukces tylko dla znanego łącza z modelu.
  using LinkValidator =
      std::function<ara::core::Result<void>(const std::string&)>;
  using UnregisterHook = std::function<void(const std::string&)>;

  explicit LinkTransportRegistry(LinkValidator validator)
      : validator_{std::move(validator)} {}

  /// Błąd: nullptr, nieznana nazwa łącza, duplikat.
  ara::core::Result<void> Register(const std::string& name,
                                   std::shared_ptr<IByteTransport> transport);
  /// Zamyka transport (Close()) i usuwa wpis; nieznana nazwa -> błąd.
  ara::core::Result<void> Unregister(const std::string& name);
  std::shared_ptr<IByteTransport> Get(const std::string& name) const;
  /// Czeka na rejestrację (do timeout), przerywalne stop_tokenem.
  std::shared_ptr<IByteTransport> WaitFor(const std::string& name,
                                          std::chrono::milliseconds timeout,
                                          std::stop_token token);
  /// Usuwa wszystkie wpisy (Deinitialize); transporty NIE są zamykane tutaj.
  void Clear();
  void SetUnregisterHook(UnregisterHook hook);

 private:
  LinkValidator validator_;
  mutable std::mutex mutex_;
  std::condition_variable_any cv_;
  std::map<std::string, std::shared_ptr<IByteTransport>> items_;
  UnregisterHook unregister_hook_;
};

/// Rejestr procesu; walidator czyta łącza z ModelDataBase.
LinkTransportRegistry& GlobalLinkTransportRegistry();

/// Kolejność w aplikacji: Initialize() (model jest wtedy w ModelDataBase, więc
/// nazwa łącza jest walidowana) -> RegisterLinkTransport() -> StartOffer() albo
/// użycie proxy. Wołane po StartOffer() też działa: łącze czeka na
/// transport (ruch trafia do kolejek zgodnie z polityką TX, Warn po 5 s).
ara::core::Result<void> RegisterLinkTransport(
    const std::string& name, std::shared_ptr<IByteTransport> transport);
ara::core::Result<void> UnregisterLinkTransport(const std::string& name);
/// Wołane przez Deinitialize().
void ClearLinkTransports();

}  // namespace mavlink
}  // namespace com
}  // namespace ara

#endif  // ARA_COM_MAVLINK_LINK_TRANSPORT_REGISTRY_H_
