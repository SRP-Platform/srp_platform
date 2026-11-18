/**
 * Copyright (c) 2024
 *
 * @file test_mavlink_frame.cc
 * @brief CRC, SHA-256/HMAC, budowanie i parsowanie ramek MAVLink v1/v2.
 */
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <vector>

#include "ara/com/mavlink/mavlink_frame.h"
#include "ara/com/mavlink/sha256.h"

namespace ara {
namespace com {
namespace mavlink {
namespace {

std::vector<std::uint8_t> Bytes(const std::string& s) {
  return {s.begin(), s.end()};
}

std::string Hex(const std::uint8_t* p, std::size_t n) {
  static const char* d = "0123456789abcdef";
  std::string o;
  for (std::size_t i = 0; i < n; ++i) {
    o += d[p[i] >> 4];
    o += d[p[i] & 15];
  }
  return o;
}

FrameParser MakeParser(std::uint8_t extra = 42) {
  return FrameParser{[extra](std::uint32_t id) -> std::optional<std::uint8_t> {
    if (id == 0 || id == 200 || id == 70000) {
      return extra;
    }
    return std::nullopt;
  }};
}

TEST(MavlinkCrc, X25CheckValue) {
  EXPECT_EQ(CrcCalculate(Bytes("123456789")), 0x6F91);
  EXPECT_EQ(CrcCalculate({}), 0xFFFF);
}

TEST(Sha256, KnownVectors) {
  const auto empty = Sha256::Hash({});
  EXPECT_EQ(Hex(empty.data(), 32),
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  const auto abc = Sha256::Hash(Bytes("abc"));
  EXPECT_EQ(Hex(abc.data(), 32),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  const auto two = Sha256::Hash(
      Bytes("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"));
  EXPECT_EQ(Hex(two.data(), 32),
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST(Sha256, StreamingEqualsOneShot) {
  const auto data = Bytes(std::string(1000, 'x'));
  Sha256 s;
  for (std::size_t i = 0; i < data.size(); i += 7) {
    s.Update(std::span<const std::uint8_t>{data.data() + i, std::min<std::size_t>(7, data.size() - i)});
  }
  EXPECT_EQ(s.Finalize(), Sha256::Hash(data));
}

TEST(Sha256, MillionA) {
  Sha256 s;
  const std::vector<std::uint8_t> chunk(1000, 'a');
  for (int i = 0; i < 1000; ++i) {
    s.Update(chunk);
  }
  const auto d = s.Finalize();
  EXPECT_EQ(Hex(d.data(), 32),
            "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST(HmacSha256, Rfc4231Case1And2) {
  const std::vector<std::uint8_t> key1(20, 0x0b);
  const auto m1 = HmacSha256::Mac(key1, Bytes("Hi There"));
  EXPECT_EQ(Hex(m1.data(), 32),
            "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
  const auto m2 = HmacSha256::Mac(Bytes("Jefe"), Bytes("what do ya want for nothing?"));
  EXPECT_EQ(Hex(m2.data(), 32),
            "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
}

TEST(HmacSha256, LongKeyIsHashed) {
  const std::vector<std::uint8_t> key(131, 0xaa);
  const auto m = HmacSha256::Mac(key, Bytes("Test Using Larger Than Block-Size Key - Hash Key First"));
  EXPECT_EQ(Hex(m.data(), 32),
            "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
}

TEST(BuildFrame, V1LayoutAndCrc) {
  const std::vector<std::uint8_t> payload{1, 2, 3, 4};
  const auto f = BuildFrame(1, 7, 1, 2, 200, payload, 42);
  ASSERT_EQ(f.size(), 6U + 4 + 2);
  EXPECT_EQ(f[0], 0xFE);
  EXPECT_EQ(f[1], 4);
  EXPECT_EQ(f[2], 7);
  EXPECT_EQ(f[3], 1);
  EXPECT_EQ(f[4], 2);
  EXPECT_EQ(f[5], 200);
  std::uint16_t crc = CrcCalculate(std::span<const std::uint8_t>{f.data() + 1, 9});
  crc = CrcAccumulate(crc, 42);
  EXPECT_EQ(f[10], crc & 0xFF);
  EXPECT_EQ(f[11], crc >> 8);
}

TEST(BuildFrame, V2LayoutMsgid24Bit) {
  const std::vector<std::uint8_t> payload{9, 8, 7};
  const auto f = BuildFrame(2, 250, 1, 1, 70000, payload, 42);
  ASSERT_EQ(f.size(), 10U + 3 + 2);
  EXPECT_EQ(f[0], 0xFD);
  EXPECT_EQ(f[1], 3);
  EXPECT_EQ(f[2], 0);  // incompat
  EXPECT_EQ(f[4], 250);
  EXPECT_EQ(f[7] | (f[8] << 8) | (f[9] << 16), 70000);
}

TEST(BuildFrame, V2TruncatesTrailingZerosKeepsOneByte) {
  const auto a = BuildFrame(2, 0, 1, 1, 200, std::vector<std::uint8_t>{5, 0, 0, 0}, 42);
  EXPECT_EQ(a[1], 1);
  const auto b = BuildFrame(2, 0, 1, 1, 200, std::vector<std::uint8_t>{0, 0, 0, 0}, 42);
  EXPECT_EQ(b[1], 1);
  const auto c = BuildFrame(2, 0, 1, 1, 200, std::vector<std::uint8_t>{0, 7, 0, 9, 0}, 42);
  EXPECT_EQ(c[1], 4);
}

TEST(BuildFrame, V1DoesNotTruncate) {
  const auto f = BuildFrame(1, 0, 1, 1, 200, std::vector<std::uint8_t>{5, 0, 0, 0}, 42);
  EXPECT_EQ(f[1], 4);
}

TEST(BuildFrame, ExpandPayloadRestoresLength) {
  EXPECT_EQ(ExpandPayload({5}, 4), (std::vector<std::uint8_t>{5, 0, 0, 0}));
  EXPECT_EQ(ExpandPayload({1, 2, 3, 4, 5}, 4).size(), 5U);
}

TEST(BuildFrame, SignedFrameLayout) {
  std::array<std::uint8_t, 32> key{};
  key[0] = 1;
  const SigningParams sp{key, 3, 0x0000'1122'3344'5566ULL};
  const auto f = BuildFrame(2, 1, 1, 1, 200, std::vector<std::uint8_t>{1, 2}, 42, &sp);
  ASSERT_EQ(f.size(), 10U + 2 + 2 + 13);
  EXPECT_EQ(f[2], 1);  // incompat: signed
  EXPECT_EQ(f[14], 3);  // link_id
  EXPECT_EQ(f[15], 0x66);  // timestamp LE
  EXPECT_EQ(f[20], 0x11);
  const auto sig = ComputeSignature(
      key, std::span<const std::uint8_t>{f.data(), f.size() - 13}, 3, sp.timestamp);
  EXPECT_TRUE(std::equal(sig.begin(), sig.end(), f.end() - 6));
}

TEST(BuildFrame, SignatureDependsOnKeyTimestampAndLinkId) {
  std::array<std::uint8_t, 32> k1{};
  std::array<std::uint8_t, 32> k2{};
  k2[5] = 1;
  const std::vector<std::uint8_t> body{1, 2, 3};
  const auto a = ComputeSignature(k1, body, 1, 100);
  EXPECT_NE(a, ComputeSignature(k2, body, 1, 100));
  EXPECT_NE(a, ComputeSignature(k1, body, 2, 100));
  EXPECT_NE(a, ComputeSignature(k1, body, 1, 101));
  EXPECT_EQ(a, ComputeSignature(k1, body, 1, 100));
}

TEST(FrameParser, RoundTripV1AndV2) {
  auto parser = MakeParser();
  std::vector<Frame> out;
  const auto f1 = BuildFrame(1, 3, 10, 20, 200, std::vector<std::uint8_t>{1, 2, 3}, 42);
  const auto f2 = BuildFrame(2, 4, 11, 21, 70000, std::vector<std::uint8_t>{4, 5, 6, 0, 0}, 42);
  parser.Feed(f1, &out);
  parser.Feed(f2, &out);
  ASSERT_EQ(out.size(), 2U);
  EXPECT_EQ(out[0].version, 1);
  EXPECT_EQ(out[0].seq, 3);
  EXPECT_EQ(out[0].sysid, 10);
  EXPECT_EQ(out[0].compid, 20);
  EXPECT_EQ(out[0].msgid, 200U);
  EXPECT_EQ(out[0].payload, (std::vector<std::uint8_t>{1, 2, 3}));
  EXPECT_EQ(out[1].version, 2);
  EXPECT_EQ(out[1].msgid, 70000U);
  EXPECT_EQ(out[1].payload, (std::vector<std::uint8_t>{4, 5, 6}));  // obcięte zera
}

TEST(FrameParser, ByteByByteFeed) {
  auto parser = MakeParser();
  std::vector<Frame> out;
  const auto f = BuildFrame(2, 1, 1, 1, 200, std::vector<std::uint8_t>{1, 2, 3, 4, 5}, 42);
  for (const auto b : f) {
    parser.Feed(std::span<const std::uint8_t>{&b, 1}, &out);
  }
  ASSERT_EQ(out.size(), 1U);
  EXPECT_EQ(out[0].payload.size(), 5U);
}

TEST(FrameParser, GarbageBetweenFramesIsSkipped) {
  auto parser = MakeParser();
  std::vector<Frame> out;
  std::vector<std::uint8_t> stream{0x00, 0x11, 0x22};
  const auto f = BuildFrame(2, 1, 1, 1, 200, std::vector<std::uint8_t>{1}, 42);
  stream.insert(stream.end(), f.begin(), f.end());
  stream.insert(stream.end(), {0x55, 0x66});
  stream.insert(stream.end(), f.begin(), f.end());
  parser.Feed(stream, &out);
  EXPECT_EQ(out.size(), 2U);
  EXPECT_GE(parser.stats().garbage_bytes, 5U);
}

TEST(FrameParser, FalseStartThenValidFrameIsRecovered) {
  auto parser = MakeParser();
  std::vector<Frame> out;
  // 0xFD + nagłówek udający ramkę (len=3), potem prawdziwa ramka.
  std::vector<std::uint8_t> stream{0xFD, 0x03, 0x00, 0x00, 0x01, 0x01, 0x01, 200, 0, 0};
  const auto f = BuildFrame(2, 9, 1, 1, 200, std::vector<std::uint8_t>{1, 2, 3}, 42);
  stream.insert(stream.end(), f.begin(), f.end());
  stream.insert(stream.end(), f.begin(), f.end());
  parser.Feed(stream, &out);
  ASSERT_GE(out.size(), 1U);
  EXPECT_EQ(out.back().seq, 9);
  EXPECT_GE(parser.stats().crc_errors, 1U);
}

TEST(FrameParser, FalseStartWithHugeLenDoesNotBlockFollowingFrames) {
  auto parser = MakeParser();
  std::vector<Frame> out;
  // 0xFE (v1) + len=119 udaje długą ramkę; za nim dwie prawdziwe, krótkie ramki.
  std::vector<std::uint8_t> stream{0x00, 0xFE, 0x77};
  const auto f = BuildFrame(2, 9, 1, 1, 200, std::vector<std::uint8_t>{1, 2}, 42);
  stream.insert(stream.end(), f.begin(), f.end());
  stream.insert(stream.end(), f.begin(), f.end());
  parser.Feed(stream, &out);
  EXPECT_EQ(out.size(), 2U);
}

TEST(FrameParser, GenuineLongFrameStillWaitsForItsTail) {
  auto parser = MakeParser();
  std::vector<Frame> out;
  std::vector<std::uint8_t> p(200, 0x33);
  const auto f = BuildFrame(2, 1, 1, 1, 200, p, 42);
  parser.Feed(std::span<const std::uint8_t>{f.data(), 100}, &out);
  EXPECT_TRUE(out.empty());
  parser.Feed(std::span<const std::uint8_t>{f.data() + 100, f.size() - 100}, &out);
  EXPECT_EQ(out.size(), 1U);
}

TEST(FrameParser, BadCrcIsCountedAndNotDelivered) {
  auto parser = MakeParser();
  std::vector<Frame> out;
  auto f = BuildFrame(2, 1, 1, 1, 200, std::vector<std::uint8_t>{1, 2, 3}, 42);
  f[11] ^= 0x01;  // bit w payloadzie
  parser.Feed(f, &out);
  EXPECT_TRUE(out.empty());
  EXPECT_EQ(parser.stats().crc_errors, 1U);
}

TEST(FrameParser, WrongCrcExtraRejected) {
  auto parser = MakeParser(43);
  std::vector<Frame> out;
  parser.Feed(BuildFrame(2, 1, 1, 1, 200, std::vector<std::uint8_t>{1}, 42), &out);
  EXPECT_TRUE(out.empty());
  EXPECT_EQ(parser.stats().crc_errors, 1U);
}

TEST(FrameParser, UnknownMsgidIsDroppedWithoutBlockingStream) {
  auto parser = MakeParser();
  std::vector<Frame> out;
  std::vector<std::uint8_t> stream =
      BuildFrame(2, 1, 1, 1, 999, std::vector<std::uint8_t>{1, 2, 3}, 42);
  const auto good = BuildFrame(2, 2, 1, 1, 200, std::vector<std::uint8_t>{1}, 42);
  stream.insert(stream.end(), good.begin(), good.end());
  parser.Feed(stream, &out);
  ASSERT_EQ(out.size(), 1U);
  EXPECT_EQ(out[0].seq, 2);
  EXPECT_EQ(parser.stats().unknown_msgid, 1U);
}

TEST(FrameParser, BadFrameThenTwoGlueGoodFramesDeliversBoth) {
  auto parser = MakeParser();
  std::vector<Frame> out;
  auto bad = BuildFrame(2, 1, 1, 1, 200, std::vector<std::uint8_t>{1, 2, 3, 4}, 42);
  bad[bad.size() - 1] ^= 0x01;  // zly CRC
  auto stream = bad;
  const auto g1 = BuildFrame(2, 2, 1, 1, 200, std::vector<std::uint8_t>{5, 6}, 42);
  const auto g2 = BuildFrame(2, 3, 1, 1, 200, std::vector<std::uint8_t>{7}, 42);
  stream.insert(stream.end(), g1.begin(), g1.end());
  stream.insert(stream.end(), g2.begin(), g2.end());
  parser.Feed(stream, &out);
  ASSERT_EQ(out.size(), 2U);
  EXPECT_EQ(out[0].seq, 2);
  EXPECT_EQ(out[1].seq, 3);
  EXPECT_EQ(parser.stats().crc_errors, 1U);
}

TEST(FrameParser, BadLengthByteThenGoodFramesResynchronizesByteByByte) {
  auto parser = MakeParser();
  std::vector<Frame> out;
  auto bad = BuildFrame(2, 1, 1, 1, 200, std::vector<std::uint8_t>{1, 2, 3, 4}, 42);
  bad[1] = 200;  // zdeklarowana dlugosc ogromna (bitflip w LEN)
  auto stream = bad;
  for (std::uint8_t i = 2; i < 12; ++i) {
    const auto g = BuildFrame(2, i, 1, 1, 200, std::vector<std::uint8_t>{i, i}, 42);
    stream.insert(stream.end(), g.begin(), g.end());
  }
  parser.Feed(stream, &out);
  ASSERT_EQ(out.size(), 10U);
  EXPECT_EQ(out.front().seq, 2);
  EXPECT_EQ(out.back().seq, 11);
}

TEST(FrameParser, FalseHeaderInsideBadFramePayloadDoesNotSwallowFollowingFrames) {
  // zla ramka niesie w payloadzie falszywy naglowek v2 (nieznany msgid, len=100): po bledzie CRC
  // resynchronizacja bajt po bajcie nie moze "przeskoczyc" kolejnych dobrych ramek (T2 #1).
  auto parser = MakeParser();
  std::vector<Frame> out;
  const std::vector<std::uint8_t> fake = {0xFD, 100, 0, 0, 0, 1, 1, 0xE7, 0x03, 0x00, 9, 9, 9};
  auto bad = BuildFrame(2, 1, 1, 1, 200, fake, 42);
  bad[bad.size() - 1] ^= 0x01;
  auto stream = bad;
  for (std::uint8_t i = 2; i < 12; ++i) {
    const auto g = BuildFrame(2, i, 1, 1, 200, std::vector<std::uint8_t>{i, i, i}, 42);
    stream.insert(stream.end(), g.begin(), g.end());
  }
  for (const std::size_t chunk : {std::size_t{1}, std::size_t{9}, stream.size()}) {
    auto p = MakeParser();
    std::vector<Frame> got;
    for (std::size_t off = 0; off < stream.size(); off += chunk) {
      p.Feed(std::span<const std::uint8_t>{stream.data() + off, std::min(chunk, stream.size() - off)}, &got);
    }
    ASSERT_EQ(got.size(), 10U) << "chunk=" << chunk;
  }
}

TEST(FrameParser, SingleBitFlipAnywhereLosesAtMostThatFrameForAnyChunking) {
  // wyczerpujaco: kazdy bit ramki nr 10 z 30 (v2 i v2 podpisane), karmienie kawalkami 1/7/64/all
  std::array<std::uint8_t, 32> key{};
  const SigningParams sp{key, 1, 777};
  for (const bool signed_frames : {false, true}) {
    std::vector<std::vector<std::uint8_t>> frames;
    for (std::uint8_t i = 0; i < 30; ++i) {
      std::vector<std::uint8_t> payload(5 + i % 17, static_cast<std::uint8_t>(0xFD ^ i));  // 0xFD w danych
      frames.push_back(BuildFrame(2, i, 1, 1, 200, payload, 42, signed_frames ? &sp : nullptr));
    }
    const std::size_t victim = 10;
    for (std::size_t bit = 0; bit < frames[victim].size() * 8; ++bit) {
      std::vector<std::uint8_t> stream;
      for (std::size_t k = 0; k < frames.size(); ++k) {
        auto f = frames[k];
        if (k == victim) f[bit / 8] ^= static_cast<std::uint8_t>(1U << (bit % 8));
        stream.insert(stream.end(), f.begin(), f.end());
      }
      for (const std::size_t chunk : {std::size_t{1}, std::size_t{7}, std::size_t{64}, stream.size()}) {
        auto parser = MakeParser();
        std::vector<Frame> out;
        for (std::size_t off = 0; off < stream.size(); off += chunk) {
          const std::size_t n = std::min(chunk, stream.size() - off);
          parser.Feed(std::span<const std::uint8_t>{stream.data() + off, n}, &out);
        }
        // ramki poza ofiara musza dojsc; sama ofiara moze (podpis/flaga) zginac
        std::size_t others = 0;
        for (const auto& f : out) {
          if (f.seq != victim) ++others;
        }
        ASSERT_EQ(others, 29U) << "signed=" << signed_frames << " bit=" << bit << " chunk=" << chunk;
      }
    }
  }
}

TEST(FrameParser, UnknownIncompatFlagRejected) {
  auto parser = MakeParser();
  std::vector<Frame> out;
  auto f = BuildFrame(2, 1, 1, 1, 200, std::vector<std::uint8_t>{1}, 42);
  f[2] = 0x02;  // nieznana flaga
  parser.Feed(f, &out);
  EXPECT_TRUE(out.empty());
  EXPECT_GE(parser.stats().bad_incompat_flags, 1U);
}

TEST(FrameParser, SignedFrameFieldsParsed) {
  auto parser = MakeParser();
  std::vector<Frame> out;
  std::array<std::uint8_t, 32> key{};
  const SigningParams sp{key, 5, 0x0102'0304'0506ULL};
  const auto f = BuildFrame(2, 1, 1, 1, 200, std::vector<std::uint8_t>{1, 2}, 42, &sp);
  parser.Feed(f, &out);
  ASSERT_EQ(out.size(), 1U);
  EXPECT_TRUE(out[0].has_signature);
  EXPECT_EQ(out[0].link_id, 5);
  EXPECT_EQ(out[0].timestamp, 0x0102'0304'0506ULL);
  EXPECT_EQ(out[0].raw, f);
}

TEST(FrameParser, MaxPayload255) {
  auto parser = MakeParser();
  std::vector<Frame> out;
  std::vector<std::uint8_t> p(255, 0xAB);
  parser.Feed(BuildFrame(2, 1, 1, 1, 200, p, 42), &out);
  ASSERT_EQ(out.size(), 1U);
  EXPECT_EQ(out[0].payload.size(), 255U);
}

TEST(FrameParser, ManyFramesInOneChunk) {
  auto parser = MakeParser();
  std::vector<Frame> out;
  std::vector<std::uint8_t> stream;
  for (int i = 0; i < 50; ++i) {
    const auto f = BuildFrame(i % 2 == 0 ? 1 : 2, static_cast<std::uint8_t>(i), 1, 1, 200,
                              std::vector<std::uint8_t>{static_cast<std::uint8_t>(i)}, 42);
    stream.insert(stream.end(), f.begin(), f.end());
  }
  parser.Feed(stream, &out);
  ASSERT_EQ(out.size(), 50U);
  for (int i = 0; i < 50; ++i) {
    EXPECT_EQ(out[i].seq, i);
  }
}

TEST(FrameParser, TruncatedFrameWaitsForRest) {
  auto parser = MakeParser();
  std::vector<Frame> out;
  const auto f = BuildFrame(2, 1, 1, 1, 200, std::vector<std::uint8_t>{1, 2, 3, 4}, 42);
  parser.Feed(std::span<const std::uint8_t>{f.data(), f.size() - 3}, &out);
  EXPECT_TRUE(out.empty());
  parser.Feed(std::span<const std::uint8_t>{f.data() + f.size() - 3, 3}, &out);
  EXPECT_EQ(out.size(), 1U);
}

TEST(FrameParser, HeartbeatBuiltinConstants) {
  EXPECT_EQ(kHeartbeatMsgId, 0U);
  EXPECT_EQ(kHeartbeatCrcExtra, 50);
  EXPECT_EQ(kHeartbeatPayloadLen, 9U);
}

}  // namespace
}  // namespace mavlink
}  // namespace com
}  // namespace ara
