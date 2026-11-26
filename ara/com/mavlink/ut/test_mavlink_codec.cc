/**
 * Copyright (c) 2024
 *
 * @file test_mavlink_codec.cc
 * @brief SOME/IP <-> MAVLink codec and crc_extra conformant with mavgen.
 */
#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "ara/com/mavlink/mavlink_codec.h"
#include "ara/com/mavlink/mavlink_frame.h"

namespace ara {
namespace com {
namespace mavlink {
namespace {
using Field = ara::core::model::ModelMavlinkField;
using Msg = ara::core::model::ModelMavlinkMessage;
using Type = Field::Type;
using Role = Field::Role;

Field F(const std::string& name, Type t, Role r = Role::kPayload, std::uint16_t n = 0,
        bool big = false) {
  Field f;
  f.name = name;
  f.type = t;
  f.role = r;
  f.array_len = n;
  f.big_endian = big;
  return f;
}

Msg MixedWidths() {
  Msg m;
  m.msg_id = 1;
  m.name = "MIXED_WIDTHS";
  m.fields = {F("type", Type::kU8), F("autopilot", Type::kU8), F("base_mode", Type::kU8),
              F("custom_mode", Type::kU32), F("system_status", Type::kU8),
              F("mavlink_version", Type::kU8)};
  return m;
}

Msg Attitude() {
  Msg m;
  m.msg_id = 30;
  m.name = "ATTITUDE";
  m.fields = {F("time_boot_ms", Type::kU32), F("roll", Type::kF32), F("pitch", Type::kF32),
              F("yaw", Type::kF32), F("rollspeed", Type::kF32), F("pitchspeed", Type::kF32),
              F("yawspeed", Type::kF32)};
  return m;
}

Msg Position(bool big = false) {
  Msg m;
  m.msg_id = 42000;
  m.name = "SRP_RADIO_POSITION";
  m.fields = {F("seq", Type::kU16, Role::kSeq), F("instance", Type::kU16, Role::kInstance),
              F("lat", Type::kF32, Role::kPayload, 0, big), F("lon", Type::kF32, Role::kPayload, 0, big),
              F("fix_type", Type::kU8)};
  return m;
}

std::vector<std::uint8_t> LeBytes(float v) {
  std::vector<std::uint8_t> b(4);
  std::memcpy(b.data(), &v, 4);
  return b;
}

TEST(CrcExtra, MatchesMavgenForKnownMessages) {
  EXPECT_EQ(ComputeCrcExtra(Attitude()), 39);   // common.xml ATTITUDE
}

TEST(CrcExtra, ArrayLengthAndNameChangeResult) {
  Msg a;
  a.name = "A";
  a.fields = {F("s", Type::kChar, Role::kPayload, 10)};
  Msg b = a;
  b.fields[0].array_len = 11;
  Msg c = a;
  c.name = "B";
  EXPECT_NE(ComputeCrcExtra(a), ComputeCrcExtra(b));
  EXPECT_NE(ComputeCrcExtra(a), ComputeCrcExtra(c));
}

TEST(MessageCodec, WireLayoutPutsLargestFieldFirst) {
  MessageCodec c{MixedWidths()};
  EXPECT_EQ(c.full_payload_len(), 9U);
  // SOME/IP: type, autopilot, base_mode, custom_mode(4), system_status, mavlink_version
  std::vector<std::uint8_t> someip{18, 8, 0, 0x78, 0x56, 0x34, 0x12, 4, 3};
  const auto enc = c.Encode({}, someip);
  ASSERT_TRUE(enc.HasValue());
  // MAVLink: custom_mode LE | type | autopilot | base_mode | system_status | mavlink_version
  EXPECT_EQ(enc.Value(), (std::vector<std::uint8_t>{0x78, 0x56, 0x34, 0x12, 18, 8, 0, 4, 3}));
  const auto dec = c.Decode(enc.Value());
  ASSERT_TRUE(dec.HasValue());
  EXPECT_EQ(dec.Value().someip, someip);
}

TEST(MessageCodec, HeaderFieldsGoToRolesNotToSomeip) {
  MessageCodec c{Position()};
  std::vector<std::uint8_t> someip = LeBytes(1.5f);
  const auto lon = LeBytes(-2.5f);
  someip.insert(someip.end(), lon.begin(), lon.end());
  someip.push_back(3);
  MessageHeader h;
  h.seq = 0xABCD;
  h.instance = 0x1234;
  const auto enc = c.Encode(h, someip);
  ASSERT_TRUE(enc.HasValue());
  ASSERT_EQ(enc.Value().size(), 13U);
  // wire: lat(4) lon(4) seq(2) instance(2) fix(1)
  EXPECT_EQ(enc.Value()[8], 0xCD);
  EXPECT_EQ(enc.Value()[9], 0xAB);
  EXPECT_EQ(enc.Value()[10], 0x34);
  EXPECT_EQ(enc.Value()[11], 0x12);
  EXPECT_EQ(enc.Value()[12], 3);
  const auto dec = c.Decode(enc.Value());
  ASSERT_TRUE(dec.HasValue());
  EXPECT_EQ(dec.Value().header.seq, 0xABCD);
  EXPECT_EQ(dec.Value().header.instance, 0x1234);
  EXPECT_EQ(dec.Value().someip, someip);
}

TEST(MessageCodec, BigEndianSomeipFieldsAreSwapped) {
  MessageCodec c{Position(true)};
  // SOME/IP big endian: 0x3FC00000 (1.5f) as 3F C0 00 00
  std::vector<std::uint8_t> someip{0x3F, 0xC0, 0x00, 0x00, 0xC0, 0x20, 0x00, 0x00, 7};
  const auto enc = c.Encode({}, someip);
  ASSERT_TRUE(enc.HasValue());
  EXPECT_EQ(enc.Value()[0], 0x00);  // MAVLink little endian
  EXPECT_EQ(enc.Value()[3], 0x3F);
  const auto dec = c.Decode(enc.Value());
  ASSERT_TRUE(dec.HasValue());
  EXPECT_EQ(dec.Value().someip, someip);
}

TEST(MessageCodec, EncodeRejectsTooShortSomeipPayload) {
  MessageCodec c{Position()};
  const auto enc = c.Encode({}, std::vector<std::uint8_t>(5, 0));
  EXPECT_FALSE(enc.HasValue());
}

TEST(MessageCodec, DecodeTruncatedV2PayloadIsZeroPadded) {
  MessageCodec c{MixedWidths()};
  const auto dec = c.Decode(std::vector<std::uint8_t>{0, 0, 0, 0, 18});  // truncated zeros
  ASSERT_TRUE(dec.HasValue());
  EXPECT_EQ(dec.Value().someip, (std::vector<std::uint8_t>{18, 0, 0, 0, 0, 0, 0, 0, 0}));
}

TEST(MessageCodec, DecodeRejectsOverlongPayload) {
  MessageCodec c{MixedWidths()};
  EXPECT_FALSE(c.Decode(std::vector<std::uint8_t>(10, 0)).HasValue());
}

TEST(MessageCodec, TopLevelStringIsRawBytesAndCutAtNul) {
  Msg m;
  m.msg_id = 1;
  m.name = "S";
  m.fields = {F("seq", Type::kU16, Role::kSeq), F("instance", Type::kU16, Role::kInstance),
              F("value", Type::kChar, Role::kPayload, 8)};
  MessageCodec c{m};
  const auto enc = c.Encode({}, std::vector<std::uint8_t>{'a', 'b', 'c'});
  ASSERT_TRUE(enc.HasValue());
  EXPECT_EQ(enc.Value().size(), 12U);
  const auto dec = c.Decode(enc.Value());
  ASSERT_TRUE(dec.HasValue());
  EXPECT_EQ(dec.Value().someip, (std::vector<std::uint8_t>{'a', 'b', 'c'}));
}

TEST(MessageCodec, StringExactlyMaxLenWithoutNulIsAllowed) {
  Msg m;
  m.name = "S";
  m.fields = {F("value", Type::kChar, Role::kPayload, 4)};
  MessageCodec c{m};
  const auto enc = c.Encode({}, std::vector<std::uint8_t>{'w', 'x', 'y', 'z'});
  ASSERT_TRUE(enc.HasValue());
  const auto dec = c.Decode(enc.Value());
  ASSERT_TRUE(dec.HasValue());
  EXPECT_EQ(dec.Value().someip, (std::vector<std::uint8_t>{'w', 'x', 'y', 'z'}));
}

TEST(MessageCodec, StringLongerThanMaxLenIsRejected) {
  Msg m;
  m.name = "S";
  m.fields = {F("value", Type::kChar, Role::kPayload, 4)};
  MessageCodec c{m};
  EXPECT_FALSE(c.Encode({}, std::vector<std::uint8_t>(5, 'a')).HasValue());
}

TEST(MessageCodec, StringInsideStructIsFixedWidthPadded) {
  Msg m;
  m.name = "T";
  m.fields = {F("n", Type::kU8), F("label", Type::kChar, Role::kPayload, 3)};
  MessageCodec c{m};
  // SOME/IP: n, label[3]
  const auto enc = c.Encode({}, std::vector<std::uint8_t>{9, 'o', 'k', 0});
  ASSERT_TRUE(enc.HasValue());
  const auto dec = c.Decode(enc.Value());
  ASSERT_TRUE(dec.HasValue());
  EXPECT_EQ(dec.Value().someip, (std::vector<std::uint8_t>{9, 'o', 'k', 0}));
}

TEST(MessageCodec, ResponseStatusRole) {
  Msg m;
  m.name = "R";
  m.fields = {F("seq", Type::kU16, Role::kSeq), F("instance", Type::kU16, Role::kInstance),
              F("target_system", Type::kU8, Role::kTargetSystem),
              F("target_component", Type::kU8, Role::kTargetComponent),
              F("status", Type::kU8, Role::kStatus), F("value", Type::kU8)};
  MessageCodec c{m};
  MessageHeader h;
  h.seq = 7;
  h.instance = 3;
  h.target_system = 1;
  h.target_component = 2;
  h.status = 2;
  const auto enc = c.Encode(h, std::vector<std::uint8_t>{1});
  ASSERT_TRUE(enc.HasValue());
  const auto dec = c.Decode(enc.Value());
  ASSERT_TRUE(dec.HasValue());
  EXPECT_EQ(dec.Value().header.status, 2);
  EXPECT_EQ(dec.Value().header.target_system, 1);
  EXPECT_EQ(dec.Value().header.target_component, 2);
  EXPECT_EQ(dec.Value().header.seq, 7);
  EXPECT_EQ(dec.Value().header.instance, 3);
}

TEST(MessageCodec, VoidRequestHasOnlyHeader) {
  Msg m;
  m.name = "V";
  m.fields = {F("seq", Type::kU16, Role::kSeq), F("instance", Type::kU16, Role::kInstance),
              F("target_system", Type::kU8, Role::kTargetSystem),
              F("target_component", Type::kU8, Role::kTargetComponent)};
  MessageCodec c{m};
  EXPECT_EQ(c.full_payload_len(), 6U);
  const auto enc = c.Encode({}, {});
  ASSERT_TRUE(enc.HasValue());
  const auto dec = c.Decode(enc.Value());
  ASSERT_TRUE(dec.HasValue());
  EXPECT_TRUE(dec.Value().someip.empty());
}

TEST(MessageCodec, SixtyFourBitAndSignedFieldsRoundTrip) {
  Msg m;
  m.name = "N";
  m.fields = {F("a", Type::kI8), F("b", Type::kI16), F("c", Type::kI32), F("d", Type::kU64),
              F("e", Type::kF64), F("f", Type::kI64)};
  MessageCodec c{m};
  std::vector<std::uint8_t> someip(1 + 2 + 4 + 8 + 8 + 8);
  for (std::size_t i = 0; i < someip.size(); ++i) {
    someip[i] = static_cast<std::uint8_t>(0x10 + i);
  }
  const auto enc = c.Encode({}, someip);
  ASSERT_TRUE(enc.HasValue());
  EXPECT_EQ(enc.Value().size(), 31U);
  const auto dec = c.Decode(enc.Value());
  ASSERT_TRUE(dec.HasValue());
  EXPECT_EQ(dec.Value().someip, someip);
}

TEST(MessageCodec, FullFrameThroughBuildAndParse) {
  MessageCodec c{Position()};
  std::vector<std::uint8_t> someip = LeBytes(10.0f);
  const auto lon = LeBytes(20.0f);
  someip.insert(someip.end(), lon.begin(), lon.end());
  someip.push_back(1);
  MessageHeader h;
  h.seq = 5;
  h.instance = 3;
  const auto payload = c.Encode(h, someip);
  ASSERT_TRUE(payload.HasValue());
  const auto bytes = BuildFrame(9, 1, 1, c.msg_id(), payload.Value(), c.crc_extra());
  FrameParser parser{[&c](std::uint32_t id) -> std::optional<std::uint8_t> {
    return id == c.msg_id() ? std::optional<std::uint8_t>{c.crc_extra()} : std::nullopt;
  }};
  std::vector<Frame> frames;
  parser.Feed(bytes, &frames);
  ASSERT_EQ(frames.size(), 1U);
  const auto dec = c.Decode(frames[0].payload);
  ASSERT_TRUE(dec.HasValue());
  EXPECT_EQ(dec.Value().someip, someip);
  EXPECT_EQ(dec.Value().header.seq, 5);
}

}  // namespace
}  // namespace mavlink
}  // namespace com
}  // namespace ara
