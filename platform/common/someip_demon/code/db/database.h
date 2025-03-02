/**
 * @file database.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-11-26
 *
 * @copyright Copyright (c) 2024
 *
 */
#ifndef PLATFORM_COMMON_SOMEIP_DEMON_CODE_DB_DATABASE_H_
#define PLATFORM_COMMON_SOMEIP_DEMON_CODE_DB_DATABASE_H_

#include <condition_variable>  // NOLINT
#include <memory_resource>
#include <stop_token>  // NOLINT
#include <unordered_map>
#include <unordered_set>

#include "platform/common/someip_demon/code/db/find_service_item.h"
#include "platform/common/someip_demon/code/db/service_item.h"
namespace srp {
namespace someip_demon {
namespace db {
class Database {
 public:
  // List with service withch are provide by this ECU
  using OfferedServiceTable = std::pmr::unordered_map<uint32_t, ServiceItem>;
  // List with Service withch are consume by this ECU
  using FindServiceTable = std::pmr::unordered_map<uint32_t, FindServiceItem>;
  // List with Service witch we need
  using ReqServiceTable =
      std::pmr::unordered_map<uint32_t, std::pmr::unordered_set<uint32_t>>;

  using NetworkSubscriberTable =
      std::pmr::unordered_map<uint32_t, std::unordered_set<uint32_t>>;
  using LocalSubscriberTable =
      std::pmr::unordered_map<uint32_t, std::unordered_set<uint32_t>>;

 private:
  OfferedServiceTable offered_service_;
  FindServiceTable find_service_;
  ReqServiceTable req_table_;
  NetworkSubscriberTable network_sub_;
  LocalSubscriberTable local_sub_;
  std::condition_variable_any cv_;
  std::mutex mutex_;

 public:
  Database(/* args */)
      : offered_service_{std::pmr::new_delete_resource()},
        find_service_{std::pmr::new_delete_resource()},
        network_sub_{std::pmr::new_delete_resource()},
        local_sub_{std::pmr::new_delete_resource()} {}

  ~Database() = default;

  bool AddNewProvideService(const ServiceItem& item) {
    return AddService(offered_service_, item);
  }

  bool AddNewConsumeService(const FindServiceItem& item) {
    return AddService(find_service_, item);
  }
  bool AddNewLocalSubscriber(const uint32_t endpoint, uint32_t peer_id) {
    auto item = local_sub_.find(endpoint);
    if (item != local_sub_.end()) {
      return item->second.insert(peer_id).second;
    }
    return local_sub_.insert({endpoint, {peer_id}}).second;
  }
  template <typename TableSection, typename ItemType>
  bool AddService(TableSection& section, const ItemType& item) {  // NOLINT
    const uint32_t key{(static_cast<uint32_t>(item.instance_id_) << 16) +
                       item.service_id_};
    return section.insert({key, item}).second;
  }
  const std::optional<std::reference_wrapper<FindServiceTable::mapped_type>>
  FindInFindService(const uint16_t service_id, const uint16_t instance_id) {
    const uint32_t key{(static_cast<uint32_t>(instance_id) << 16) + service_id};
    const auto& iter = find_service_.find(key);
    if (iter == find_service_.cend()) {
      return std::nullopt;
    }
    return std::reference_wrapper<FindServiceTable::mapped_type>(iter->second);
  }
  const OfferedServiceTable& GetProvideList() const { return offered_service_; }
  const FindServiceTable& GetConsumeList() const { return find_service_; }
  void WaitForNewService(std::stop_token token) noexcept {
    uint8_t i{0};
    std::unique_lock<std::mutex> ul_{mutex_};
    const std::stop_callback stop_wait{token, [this]() { cv_.notify_one(); }};
    cv_.wait(ul_,
             [&token, &i]() { return token.stop_requested() || (i++ != 0); });
  }
  const std::optional<std::reference_wrapper<std::unordered_set<uint32_t>>>
  GetSubList(const uint32_t endpoint) {
    const auto item = local_sub_.find(endpoint);
    if (item == local_sub_.end()) {
      return std::nullopt;
    }
    return std::reference_wrapper<std::unordered_set<uint32_t>>(item->second);
  }
  void NotifyAboutNewService() {
    std::lock_guard<std::mutex> lk(mutex_);
    cv_.notify_all();
    cv_.notify_all();
  }
};
}  // namespace db
}  // namespace someip_demon
}  // namespace srp

#endif  // PLATFORM_COMMON_SOMEIP_DEMON_CODE_DB_DATABASE_H_
