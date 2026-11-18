/**
 * Copyright (c) 2024
 *
 * @file test_mavlink_signing.cc
 * @brief Signing key, timestamps, signature verification and replay protection.
 */
#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "ara/com/mavlink/mavlink_signing.h"
#include "ara/com/mavlink/transport_error.h"

namespace ara {
namespace com {
namespace mavlink {
namespace {

std::string TmpPath(const std::string& name) {
  const char* d = std::getenv("TEST_TMPDIR");
  return std::string(d != nullptr ? d : "/tmp") + "/" + name;
}

void WriteFile(const std::string& path, const std::string& content) {
  std::ofstream out{path, std::ios::trunc};
  out << content;
}

SigningKey Key(std::uint8_t seed) {
  SigningKey k{};
  for (std::size_t i = 0; i < k.size(); ++i) {
    k[i] = static_cast<std::uint8_t>(seed + i);
  }
  return k;
}

struct Clock {
  std::uint64_t now{0};
  LinkSigner::NowFn fn() {
    return [this] { return now; };
  }
};

LinkSigner::Config Cfg(std::uint8_t seed = 1, std::uint8_t link = 1, bool accept = false,
                       const std::string& state = "") {
  LinkSigner::Config c;
  c.key = Key(seed);
  c.link_id = link;
  c.accept_unsigned = accept;
  c.state_file = state;
  return c;
}

/// A parsed frame together with its bytes as received.
struct Parsed {
  Frame frame;
  std::vector<std::uint8_t> raw;
};

Parsed Parse(const std::vector<std::uint8_t>& bytes) {
  FrameParser p{[](std::uint32_t) -> std::optional<std::uint8_t> { return 42; }};
  std::vector<Frame> out;
  p.Feed(bytes, &out);
  EXPECT_EQ(out.size(), 1U);
  return Parsed{out.empty() ? Frame{} : out[0], bytes};
}

/// Full receive path as the daemon runs it: signature, replay check, then commit.
SignatureVerdict Verify(LinkSigner& signer, const Parsed& p) {
  auto v = signer.VerifySignature(p.frame, p.raw);
  if (v != SignatureVerdict::kOk) {
    return v;
  }
  v = signer.CheckReplay(p.frame);
  if (v == SignatureVerdict::kOk) {
    signer.Commit(p.frame);
  }
  return v;
}

Parsed Signed(const SigningKey& key, std::uint8_t link, std::uint64_t ts, std::uint8_t sys = 1,
             std::uint8_t comp = 1) {
  const SigningParams sp{key, link, ts};
  return Parse(BuildFrame(1, sys, comp, 200, std::vector<std::uint8_t>{1, 2, 3}, 42, &sp));
}

// ---------------------------------------------------------------- key

TEST(SigningKeyFile, LoadsHexWithNewlineAndUppercase) {
  const auto path = TmpPath("key_ok");
  WriteFile(path, "000102030405060708090A0b0c0d0e0f101112131415161718191a1b1c1d1e1f\n");
  const auto k = LoadSigningKey(path);
  ASSERT_TRUE(k.HasValue());
  EXPECT_EQ(k.Value()[0], 0x00);
  EXPECT_EQ(k.Value()[10], 0x0A);
  EXPECT_EQ(k.Value()[11], 0x0b);
  EXPECT_EQ(k.Value()[31], 0x1f);
  std::remove(path.c_str());
}

TEST(SigningKeyFile, MissingFileIsClearError) {
  const auto k = LoadSigningKey(TmpPath("missing_key"));
  ASSERT_FALSE(k.HasValue());
  EXPECT_TRUE(IsTransportError(k.Error(), TransportErrc::kPersistent));
  EXPECT_NE(k.Error().SupportData().find("missing_key"), std::string::npos);
}

TEST(SigningKeyFile, WrongLengthIsError) {
  const auto path = TmpPath("key_short");
  WriteFile(path, "00ff\n");
  const auto k = LoadSigningKey(path);
  ASSERT_FALSE(k.HasValue());
  EXPECT_NE(k.Error().SupportData().find("64"), std::string::npos);
  std::remove(path.c_str());
}

TEST(SigningKeyFile, NonHexIsError) {
  const auto path = TmpPath("key_bad");
  WriteFile(path, std::string(63, 'a') + "g");
  EXPECT_FALSE(LoadSigningKey(path).HasValue());
  std::remove(path.c_str());
}

// ---------------------------------------------------------------- timestamp

TEST(LinkSigner, TimestampIsStrictlyMonotonicEvenWithFrozenClock) {
  Clock c;
  c.now = 1000;
  LinkSigner s{Cfg(), c.fn()};
  const auto a = s.NextTimestamp();
  const auto b = s.NextTimestamp();
  const auto d = s.NextTimestamp();
  EXPECT_EQ(a, 1000U);
  EXPECT_EQ(b, 1001U);
  EXPECT_EQ(d, 1002U);
}

TEST(LinkSigner, TimestampFollowsClockWhenAhead) {
  Clock c;
  c.now = 1000;
  LinkSigner s{Cfg(), c.fn()};
  s.NextTimestamp();
  c.now = 5000;
  EXPECT_EQ(s.NextTimestamp(), 5000U);
}

TEST(LinkSigner, PersistedFloorIsPlus60SecondsOnce) {
  const auto path = TmpPath("state_floor");
  WriteFile(path, "123456\n");
  Clock c;
  c.now = 0;  // BBB without an RTC
  LinkSigner s{Cfg(1, 1, false, path), c.fn()};
  s.LoadPersisted();
  EXPECT_EQ(s.NextTimestamp(), 123456U + kStartupFloor + 1);
  EXPECT_EQ(s.NextTimestamp(), 123456U + kStartupFloor + 2);  // the floor applies only once
  std::remove(path.c_str());
}

TEST(LinkSigner, ClockOf1970WithoutStateFileStillStartsAtBuildDateFloor) {
  Clock c;
  c.now = 0;
  auto cfg = Cfg();
  cfg.min_timestamp = BuildTimestamp10us();
  LinkSigner s{cfg, c.fn()};
  ASSERT_GT(BuildTimestamp10us(), kClockSanityThreshold);
  EXPECT_GE(s.NextTimestamp(), BuildTimestamp10us());
  EXPECT_EQ(s.NextTimestamp(), BuildTimestamp10us() + 1);
}

TEST(LinkSigner, WarnsOnceWhenClockUnsetAndNoState) {
  Clock c;
  c.now = 0;
  int warns = 0;
  LinkSigner s{Cfg(), c.fn(), [&warns](const std::string&) { ++warns; }};
  s.NextTimestamp();
  s.NextTimestamp();
  s.NextTimestamp();
  EXPECT_EQ(warns, 1);
}

TEST(LinkSigner, NoWarningWhenClockIsSane) {
  Clock c;
  c.now = kClockSanityThreshold + 5;
  int warns = 0;
  LinkSigner s{Cfg(), c.fn(), [&warns](const std::string&) { ++warns; }};
  s.NextTimestamp();
  EXPECT_EQ(warns, 0);
}

TEST(LinkSigner, PeerTimestampRaisesOwnTimestamp) {
  Clock c;
  c.now = 100;
  LinkSigner s{Cfg(), c.fn()};
  ASSERT_EQ(Verify(s, Signed(Key(1), 2, 9000)), SignatureVerdict::kOk);
  EXPECT_EQ(s.NextTimestamp(), 9001U);  // the BBB adopts the GS timestamp
}

TEST(LinkSigner, PersistsLastTxAndReloadsAfterRestart) {
  const auto path = TmpPath("state_roundtrip");
  std::remove(path.c_str());
  Clock c;
  c.now = 5000;
  {
    LinkSigner s{Cfg(1, 1, false, path), c.fn()};
    for (int i = 0; i < 5; ++i) s.NextTimestamp();
    s.PersistIfDue(1);
  }
  std::ifstream in{path};
  std::uint64_t saved = 0;
  in >> saved;
  EXPECT_EQ(saved, 5004U);
  Clock c2;
  c2.now = 0;
  LinkSigner restarted{Cfg(1, 1, false, path), c2.fn()};
  restarted.LoadPersisted();
  EXPECT_GT(restarted.NextTimestamp(), 5004U + kStartupFloor - 1);
  std::remove(path.c_str());
}

TEST(LinkSigner, PersistIsThrottledTo10Seconds) {
  const auto path = TmpPath("state_throttle");
  std::remove(path.c_str());
  Clock c;
  c.now = 100;
  LinkSigner s{Cfg(1, 1, false, path), c.fn()};
  s.NextTimestamp();
  s.PersistIfDue(1000);
  c.now = 200;
  s.NextTimestamp();
  s.PersistIfDue(5000);  // < 10 s: no write
  std::ifstream in{path};
  std::uint64_t v = 0;
  in >> v;
  EXPECT_EQ(v, 100U);
  s.PersistIfDue(11500);
  std::ifstream in2{path};
  in2 >> v;
  EXPECT_EQ(v, 200U);
  std::remove(path.c_str());
}

TEST(LinkSigner, MissingStateDirectoryWarnsOnceAndKeepsWorking) {
  Clock c;
  c.now = kClockSanityThreshold + 100;  // clock is sane: only the state warning
  int warns = 0;
  LinkSigner s{Cfg(1, 1, false, "/no/such/directory/state.ts"), c.fn(),
               [&warns](const std::string&) { ++warns; }};
  s.LoadPersisted();  // no file -> Warn (once)
  s.NextTimestamp();
  s.PersistIfDue(1);
  s.PersistIfDue(20000);
  EXPECT_EQ(warns, 1);
  EXPECT_EQ(s.NextTimestamp(), kClockSanityThreshold + 101);
}

// ---------------------------------------------------------------- verification

TEST(LinkSignerVerify, AcceptsCorrectSignature) {
  Clock c;
  LinkSigner s{Cfg(1), c.fn()};
  EXPECT_EQ(Verify(s, Signed(Key(1), 3, 100)), SignatureVerdict::kOk);
}

TEST(LinkSignerVerify, RejectsWrongKey) {
  Clock c;
  LinkSigner s{Cfg(1), c.fn()};
  EXPECT_EQ(Verify(s, Signed(Key(2), 3, 100)), SignatureVerdict::kBadSignature);
}

TEST(LinkSignerVerify, RejectsTamperedPayload) {
  Clock c;
  LinkSigner s{Cfg(1), c.fn()};
  auto f = Signed(Key(1), 3, 100);
  f.raw[11] ^= 0x40;  // a payload bit (the signature covers the raw frame)
  EXPECT_EQ(Verify(s, f), SignatureVerdict::kBadSignature);
}

TEST(LinkSignerVerify, RejectsTamperedTimestamp) {
  Clock c;
  LinkSigner s{Cfg(1), c.fn()};
  auto f = Signed(Key(1), 3, 100);
  f.frame.timestamp = 101;
  EXPECT_EQ(Verify(s, f), SignatureVerdict::kBadSignature);
}

TEST(LinkSignerVerify, ReplayWithSameOrOlderTimestampRejected) {
  Clock c;
  LinkSigner s{Cfg(1), c.fn()};
  ASSERT_EQ(Verify(s, Signed(Key(1), 3, 100)), SignatureVerdict::kOk);
  EXPECT_EQ(Verify(s, Signed(Key(1), 3, 100)), SignatureVerdict::kReplay);
  EXPECT_EQ(Verify(s, Signed(Key(1), 3, 99)), SignatureVerdict::kReplay);
  EXPECT_EQ(Verify(s, Signed(Key(1), 3, 101)), SignatureVerdict::kOk);
}

TEST(LinkSignerVerify, StreamsAreIndependentPerSysCompLink) {
  Clock c;
  LinkSigner s{Cfg(1), c.fn()};
  ASSERT_EQ(Verify(s, Signed(Key(1), 3, 500, 1, 1)), SignatureVerdict::kOk);
  EXPECT_EQ(Verify(s, Signed(Key(1), 4, 500, 1, 1)), SignatureVerdict::kOk);  // different link_id
  EXPECT_EQ(Verify(s, Signed(Key(1), 3, 500, 2, 1)), SignatureVerdict::kOk);  // different sysid
  EXPECT_EQ(Verify(s, Signed(Key(1), 3, 500, 1, 2)), SignatureVerdict::kOk);  // different compid
}

TEST(LinkSignerVerify, NewStreamMuchOlderThanKnownIsRejected) {
  Clock c;
  LinkSigner s{Cfg(1), c.fn()};
  const std::uint64_t big = 10 * kStartupFloor;
  ASSERT_EQ(Verify(s, Signed(Key(1), 3, big)), SignatureVerdict::kOk);
  EXPECT_EQ(Verify(s, Signed(Key(1), 4, big - 2 * kStartupFloor)), SignatureVerdict::kReplay);
  EXPECT_EQ(Verify(s, Signed(Key(1), 4, big - kStartupFloor / 2)), SignatureVerdict::kOk);
}

TEST(LinkSignerVerify, RecordedFramesAreReplayRejectedAfterReceiverRestart) {
  const auto path = TmpPath("state_replay_restart");
  std::remove(path.c_str());
  Clock c;
  c.now = 1000000;
  std::vector<Parsed> recorded;
  {
    LinkSigner s{Cfg(1, 1, false, path), c.fn()};
    s.LoadPersisted();
    for (int i = 0; i < 3; ++i) {
      c.now += 100;
      recorded.push_back(Signed(Key(1), 3, c.now));
      ASSERT_EQ(Verify(s, recorded.back()), SignatureVerdict::kOk);
    }
    s.NextTimestamp();
    s.PersistIfDue(1);
  }
  // receiver restarts after > 60 s: recorded frames (new stream, no last_peer_) -> replay
  c.now += 10 * kStartupFloor;
  LinkSigner restarted{Cfg(1, 1, false, path), c.fn()};
  restarted.LoadPersisted();
  for (const auto& f : recorded) {
    EXPECT_EQ(Verify(restarted, f), SignatureVerdict::kReplay);
  }
  // a fresh GS frame (current clock) passes
  EXPECT_EQ(Verify(restarted, Signed(Key(1), 3, c.now + 10)), SignatureVerdict::kOk);
  std::remove(path.c_str());
}

TEST(LinkSignerVerify, LastValidPeerTimestampIsPersistedAndOlderFramesRejectedAfterRestart) {
  // a receiver that never transmitted (last_tx_ = 0): only the persisted peer timestamp protects it
  const auto path = TmpPath("state_peer");
  std::remove(path.c_str());
  Clock c;
  const std::uint64_t t1 = 10 * kStartupFloor;
  const std::uint64_t t3 = t1 + 5 * kStartupFloor;
  {
    LinkSigner s{Cfg(1, 1, false, path), c.fn()};
    s.LoadPersisted();
    ASSERT_EQ(Verify(s, Signed(Key(1), 3, t1)), SignatureVerdict::kOk);
    ASSERT_EQ(Verify(s, Signed(Key(1), 3, t3)), SignatureVerdict::kOk);
    s.PersistIfDue(1);
  }
  c.now = 0;  // clock unset after the restart
  LinkSigner restarted{Cfg(1, 1, false, path), c.fn()};
  restarted.LoadPersisted();
  // recorded frames older than 60 s relative to the last valid one -> rejected
  EXPECT_EQ(Verify(restarted, Signed(Key(1), 3, t1)), SignatureVerdict::kReplay);
  EXPECT_EQ(Verify(restarted, Signed(Key(1), 4, t1 + 1)), SignatureVerdict::kReplay);
  // a fresh frame (larger ts) passes
  EXPECT_EQ(Verify(restarted, Signed(Key(1), 3, t3 + 10)), SignatureVerdict::kOk);
  std::remove(path.c_str());
}

TEST(LinkSigner, PersistRewritesWhenOnlyPeerTimestampChanged) {
  const auto path = TmpPath("state_peer_only");
  std::remove(path.c_str());
  Clock c;
  c.now = 100;
  LinkSigner s{Cfg(1, 1, false, path), c.fn()};
  s.NextTimestamp();
  s.PersistIfDue(1000);
  ASSERT_EQ(Verify(s, Signed(Key(1), 3, 50)), SignatureVerdict::kOk);
  s.PersistIfDue(20000);  // only last_peer_ changed
  std::ifstream in{path};
  std::uint64_t tx = 0, peer = 0;
  in >> tx >> peer;
  EXPECT_EQ(tx, 100U);
  EXPECT_EQ(peer, 50U);
  std::remove(path.c_str());
}

TEST(LinkSignerVerify, TruncatedSignedFrameDoesNotSwallowTheNextValidFrame) {
  // A truncated signed frame can still have a valid CRC; the parser must not swallow the
  // next frame — after a bad signature it rescans from the byte after STX.
  const auto key = Key(1);
  const SigningParams sp1{key, 1, 1000};
  const SigningParams sp2{key, 1, 2000};
  const auto a = BuildFrame(1, 1, 1, 200, std::vector<std::uint8_t>{1, 2, 3, 4, 5}, 42, &sp1);
  const auto b = BuildFrame(2, 1, 1, 200, std::vector<std::uint8_t>{9, 8, 7}, 42, &sp2);
  for (std::size_t cut = 1; cut <= 20; ++cut) {
    Clock c;
    LinkSigner signer{Cfg(1), c.fn()};
    FrameParser parser{[](std::uint32_t) -> std::optional<std::uint8_t> { return 42; }};
    parser.SetFilter([&signer](Frame& f, std::span<const std::uint8_t> raw) {
      const auto v = signer.VerifySignature(f, raw);
      f.sig_verdict = static_cast<std::uint8_t>(v);
      return v != SignatureVerdict::kBadSignature;
    });
    std::vector<std::uint8_t> stream(a.begin(), a.end() - static_cast<std::ptrdiff_t>(cut));
    stream.insert(stream.end(), b.begin(), b.end());
    std::vector<Frame> out;
    parser.Feed(stream, &out);
    ASSERT_EQ(out.size(), 1U) << "cut=" << cut;
    EXPECT_EQ(out[0].seq, 2) << "cut=" << cut;
    EXPECT_EQ(static_cast<SignatureVerdict>(out[0].sig_verdict), SignatureVerdict::kOk);
  }
}

TEST(LinkSigner, PersistNowWritesStateIgnoringThrottleAndOnlyWhenChanged) {
  const auto path = TmpPath("state_now");
  std::remove(path.c_str());
  Clock c;
  c.now = 300;
  LinkSigner s{Cfg(1, 1, false, path), c.fn()};
  s.NextTimestamp();
  s.PersistIfDue(1000);
  c.now = 400;
  s.NextTimestamp();
  s.PersistIfDue(2000);  // < 10 s: no write
  s.PersistNow();        // link stop: written despite the limit
  std::ifstream in{path};
  std::uint64_t tx = 0;
  in >> tx;
  EXPECT_EQ(tx, 400U);
  std::remove(path.c_str());
}

TEST(LinkSigner, PersistSkippedWhenNothingChanged) {
  const auto path = TmpPath("state_nochange");
  std::remove(path.c_str());
  Clock c;
  c.now = 100;
  LinkSigner s{Cfg(1, 1, false, path), c.fn()};
  s.NextTimestamp();
  s.PersistIfDue(1000);
  std::remove(path.c_str());
  s.PersistIfDue(20000);  // last_tx_ unchanged: no write
  std::ifstream in{path};
  EXPECT_FALSE(in.good());
}

TEST(LinkSignerVerify, UnsignedRejectedUnlessAccepted) {
  Clock c;
  LinkSigner strict{Cfg(1, 1, false), c.fn()};
  LinkSigner lax{Cfg(1, 1, true), c.fn()};
  const auto f = Parse(BuildFrame(1, 1, 1, 200, std::vector<std::uint8_t>{1}, 42));
  EXPECT_EQ(Verify(strict, f), SignatureVerdict::kUnsigned);
  EXPECT_EQ(Verify(lax, f), SignatureVerdict::kOk);
}

TEST(LinkSignerVerify, StateOnlyUpdatedByValidFrames) {
  Clock c;
  LinkSigner s{Cfg(1), c.fn()};
  EXPECT_EQ(Verify(s, Signed(Key(9), 3, 777)), SignatureVerdict::kBadSignature);
  EXPECT_EQ(s.NextTimestamp(), 1U);  // the bad frame did not raise the own timestamp
  ASSERT_EQ(Verify(s, Signed(Key(1), 3, 50)), SignatureVerdict::kOk);  // and did not block 50
  EXPECT_EQ(s.NextTimestamp(), 51U);
}

TEST(LinkSignerVerify, SenderRestartScenario) {
  // The sender (BBB) ran up to ts=1500, saved its state and restarts without an RTC (now=0).
  const auto path = TmpPath("state_restart");
  WriteFile(path, "1500\n");
  Clock sender_clock;
  sender_clock.now = 0;
  LinkSigner sender{Cfg(1, 1, false, path), sender_clock.fn()};
  sender.LoadPersisted();
  Clock rx_clock;
  LinkSigner receiver{Cfg(1), rx_clock.fn()};
  // The receiver knew the sender stream up to 1500.
  ASSERT_EQ(Verify(receiver, Signed(Key(1), 1, 1500)), SignatureVerdict::kOk);
  // After the restart the first frame has a timestamp > 1500 thanks to the +60 s floor.
  const auto ts = sender.NextTimestamp();
  EXPECT_GT(ts, 1500U);
  EXPECT_EQ(Verify(receiver, Signed(Key(1), 1, ts)), SignatureVerdict::kOk);
  std::remove(path.c_str());
}

TEST(LinkSignerVerify, BothSidesRestartPeerTimestampAdoptedAfterFirstFrame) {
  // The BBB restarts without state while the GS still has a large timestamp. Until the first
  // GS frame the BBB transmits with a small ts (the GS rejects it); after receiving a GS
  // frame it adopts its timestamp.
  Clock c;
  c.now = 0;
  LinkSigner bbb{Cfg(1), c.fn()};
  const auto first = bbb.NextTimestamp();
  EXPECT_LT(first, 100U);
  ASSERT_EQ(Verify(bbb, Signed(Key(1), 7, 9'000'000)), SignatureVerdict::kOk);
  EXPECT_EQ(bbb.NextTimestamp(), 9'000'001U);
}

}  // namespace
}  // namespace mavlink
}  // namespace com
}  // namespace ara
