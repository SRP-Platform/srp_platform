/**
 * @file service_item.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-11-26
 *
 * @copyright Copyright (c) 2024
 *
 */
#ifndef PLATFORM_COMMON_SOMEIP_DEMON_CODE_DB_SERVICE_ITEM_H_
#define PLATFORM_COMMON_SOMEIP_DEMON_CODE_DB_SERVICE_ITEM_H_
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ara/com/com_error_domain.h"
#include "ara/core/result.h"

namespace srp {
namespace someip_demon {
namespace db {
class EventSubscription {
 private:
  std::unordered_set<uint16_t> events_;
  uint16_t session_id_;
  uint16_t client_id_;
  uint8_t counter_{0U};
  static const uint8_t kCounterVal{2U};

 public:
  const uint32_t ip_;
  const uint16_t port_;

  EventSubscription(const uint32_t ip, const uint16_t port)
      : ip_{ip}, port_{port}, session_id_{0} {}
  void AddEvent(const uint16_t event) { events_.insert(event); }
  bool HaveEvent(const uint16_t event) const { return events_.contains(event); }
  uint16_t GetSessionId() const { return session_id_; }
  void SetSessionId(uint16_t session_id) { session_id_ = session_id; }
  uint16_t GetClientId() const { return client_id_; }
  void SetClientId(uint16_t client_id) { client_id_ = client_id; }
  void ResetCounter() { counter_ = kCounterVal; }
  bool IsSubscriptionValid() const { return counter_ > 0; }
  ~EventSubscription() = default;
};

class ServiceItem {
 public:
  const uint16_t service_id_;
  const uint16_t instance_id_;
  const std::uint8_t major_version_;
  const std::uint32_t minor_version_;
  const std::uint32_t ip_;
  const std::uint16_t port_;
  std::unordered_map<uint64_t, EventSubscription> subscription_list_;
  ServiceItem(const uint16_t service_id, const uint16_t instance_id,
              const std::uint8_t major_version,
              const std::uint32_t minor_version, const std::uint32_t ip,
              const std::uint16_t port)
      : service_id_{service_id},
        instance_id_{instance_id},
        major_version_{major_version},
        minor_version_{minor_version},
        ip_{ip},
        port_{port},
        subscription_list_{} {}

  ara::core::Result<void> AddSubscription(const uint32_t& ip,
                                          const uint16_t port,
                                          const uint16_t client_id,
                                          const uint16_t session_id,
                                          const uint16_t event) {
    const uint64_t key{(ip << 16) + port};
    auto iter = subscription_list_.find(key);
    if (iter == subscription_list_.end()) {
      const auto& res =
          subscription_list_.insert({key, EventSubscription(ip, port)});
      iter = res.first;
    }
    iter->second.SetClientId(client_id);
    iter->second.SetSessionId(session_id);
    iter->second.ResetCounter();
    iter->second.AddEvent(event);

    return ara::core::Result<void>{};
  }
  ~ServiceItem() = default;
};
}  // namespace db
}  // namespace someip_demon
}  // namespace srp

#endif  // PLATFORM_COMMON_SOMEIP_DEMON_CODE_DB_SERVICE_ITEM_H_
