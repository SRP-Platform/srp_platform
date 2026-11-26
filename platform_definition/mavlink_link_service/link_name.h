/**
 * Copyright (c) 2024
 *
 * @file link_name.h
 * @brief `link_name` of the MavlinkLinkService data (LinkStatsData): up to 16 characters carried as two
 * uint64 (`link_name_lo` = characters 0..7, `link_name_hi` = 8..15; character i is byte i of the value,
 * least significant first; the rest is zero padded).
 *
 * Why not a `string`: the model generator treats a `string` inside a structure as a field of zero
 * length, so such a structure does not decode (a string works only as the whole payload of a
 * method or event).
 */
#ifndef PLATFORM_DEFINITION_MAVLINK_LINK_SERVICE_LINK_NAME_H_
#define PLATFORM_DEFINITION_MAVLINK_LINK_SERVICE_LINK_NAME_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>

namespace srp {
namespace platform {
namespace mavlink {

constexpr std::size_t kLinkNameMaxLen = 16;

/// Packs the first 16 characters of `name`.
inline void PackLinkName(const std::string& name, std::uint64_t* lo, std::uint64_t* hi) {
  *lo = 0;
  *hi = 0;
  const std::size_t n = std::min(name.size(), kLinkNameMaxLen);
  for (std::size_t i = 0; i < n; ++i) {
    const auto c = static_cast<std::uint64_t>(static_cast<unsigned char>(name[i]));
    if (i < 8) {
      *lo |= c << (8 * i);
    } else {
      *hi |= c << (8 * (i - 8));
    }
  }
}

inline std::string UnpackLinkName(std::uint64_t lo, std::uint64_t hi) {
  std::string out;
  for (std::size_t i = 0; i < kLinkNameMaxLen; ++i) {
    const std::uint64_t word = i < 8 ? lo : hi;
    const auto c = static_cast<char>((word >> (8 * (i % 8))) & 0xFF);
    if (c == 0) {
      break;
    }
    out.push_back(c);
  }
  return out;
}

}  // namespace mavlink
}  // namespace platform
}  // namespace srp

#endif  // PLATFORM_DEFINITION_MAVLINK_LINK_SERVICE_LINK_NAME_H_
