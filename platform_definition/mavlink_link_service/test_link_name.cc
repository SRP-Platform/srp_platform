/**
 * @file test_link_name.cc
 * @copyright Copyright (c) 2024
 */
#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "platform_definition/mavlink_link_service/link_name.h"

namespace {
using srp::platform::mavlink::PackLinkName;
using srp::platform::mavlink::UnpackLinkName;

TEST(LinkName, RoundTripsNamesUpToSixteenCharacters) {
  for (const std::string name : {"", "a", "radio_b", "12345678", "123456789", "0123456789abcdef"}) {
    std::uint64_t lo = 1;
    std::uint64_t hi = 1;
    PackLinkName(name, &lo, &hi);
    EXPECT_EQ(UnpackLinkName(lo, hi), name);
  }
}

TEST(LinkName, LongerNamesAreCutAndTheByteOrderIsFixed) {
  std::uint64_t lo = 0;
  std::uint64_t hi = 0;
  PackLinkName("0123456789abcdefXYZ", &lo, &hi);
  EXPECT_EQ(UnpackLinkName(lo, hi), "0123456789abcdef");
  PackLinkName("AB", &lo, &hi);
  EXPECT_EQ(lo, 0x4241ULL);  // character 0 = least significant byte
  EXPECT_EQ(hi, 0ULL);
}
}  // namespace
