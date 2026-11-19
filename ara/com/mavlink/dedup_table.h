/**
 * Copyright (c) 2024
 *
 * @file dedup_table.h
 * @brief Deduplication of message copies from several links (redundancy): key
 * (sysid, compid, msgid, instance[, target]); a duplicate = the same `seq` and payload CRC32
 * within the TTL window. TestAndInsert is atomic.
 */
#ifndef ARA_COM_MAVLINK_DEDUP_TABLE_H_
#define ARA_COM_MAVLINK_DEDUP_TABLE_H_

#include <chrono>  // NOLINT
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>  // NOLINT
#include <span>  // NOLINT
#include <tuple>

namespace ara {
namespace com {
namespace mavlink {

/// CRC-32 (IEEE 802.3, reflected 0xEDB88320).
inline std::uint32_t Crc32(std::span<const std::uint8_t> data) {
  std::uint32_t crc = 0xFFFFFFFFU;
  for (const auto b : data) {
    crc ^= b;
    for (int i = 0; i < 8; ++i) {
      crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
  }
  return ~crc;
}

class DedupTable {
 public:
  using Clock = std::chrono::steady_clock;
  /// (sysid, compid, msgid, instance, target_system, target_component)
  using Key = std::tuple<std::uint8_t, std::uint8_t, std::uint32_t, std::uint16_t,
                         std::uint8_t, std::uint8_t>;
  static constexpr std::size_t kWindow = 4096;  // memory limit per key; entries mostly expire by time (TTL)

  /// true = duplicate (message already seen); otherwise records the entry.
  bool TestAndInsert(const Key& key, std::uint16_t seq, std::uint32_t crc,
                     std::chrono::milliseconds ttl, Clock::time_point now) {
    std::lock_guard<std::mutex> lock{mutex_};
    auto& entries = table_[key];
    while (!entries.empty() && entries.front().expiry <= now) {
      entries.pop_front();
    }
    for (const auto& e : entries) {
      if (e.expiry > now && e.seq == seq && e.crc == crc) {
        return true;
      }
    }
    entries.push_back(Entry{seq, crc, now + ttl});
    if (entries.size() > kWindow) {
      entries.pop_front();
    }
    return false;
  }

  std::size_t size() const {
    std::lock_guard<std::mutex> lock{mutex_};
    std::size_t n = 0;
    for (const auto& kv : table_) {
      n += kv.second.size();
    }
    return n;
  }

 private:
  struct Entry {
    std::uint16_t seq;
    std::uint32_t crc;
    Clock::time_point expiry;
  };
  mutable std::mutex mutex_;
  std::map<Key, std::deque<Entry>> table_;
};

}  // namespace mavlink
}  // namespace com
}  // namespace ara

#endif  // ARA_COM_MAVLINK_DEDUP_TABLE_H_
