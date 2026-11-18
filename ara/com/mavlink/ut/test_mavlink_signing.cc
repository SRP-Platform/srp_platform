/**
 * Copyright (c) 2024
 *
 * @file test_mavlink_signing.cc
 * @brief Klucz, znacznik czasu (R1), weryfikacja i anty-replay.
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

Frame Parse(const std::vector<std::uint8_t>& bytes) {
  FrameParser p{[](std::uint32_t) -> std::optional<std::uint8_t> { return 42; }};
  std::vector<Frame> out;
  p.Feed(bytes, &out);
  EXPECT_EQ(out.size(), 1U);
  return out.empty() ? Frame{} : out[0];
}

Frame Signed(const SigningKey& key, std::uint8_t link, std::uint64_t ts, std::uint8_t sys = 1,
             std::uint8_t comp = 1) {
  const SigningParams sp{key, link, ts};
  return Parse(BuildFrame(2, 1, sys, comp, 200, std::vector<std::uint8_t>{1, 2, 3}, 42, &sp));
}

// ---------------------------------------------------------------- klucz

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
  const auto k = LoadSigningKey(TmpPath("brak_klucza"));
  ASSERT_FALSE(k.HasValue());
  EXPECT_TRUE(IsTransportError(k.Error(), TransportErrc::kPersistent));
  EXPECT_NE(k.Error().SupportData().find("brak_klucza"), std::string::npos);
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
  c.now = 0;  // BBB bez RTC
  LinkSigner s{Cfg(1, 1, false, path), c.fn()};
  s.LoadPersisted();
  EXPECT_EQ(s.NextTimestamp(), 123456U + kStartupFloor + 1);
  EXPECT_EQ(s.NextTimestamp(), 123456U + kStartupFloor + 2);  // podłoga tylko raz
  std::remove(path.c_str());
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
  ASSERT_EQ(s.Verify(Signed(Key(1), 2, 9000)), SignatureVerdict::kOk);
  EXPECT_EQ(s.NextTimestamp(), 9001U);  // BBB przejmuje znacznik GS
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
  s.PersistIfDue(5000);  // < 10 s: bez zapisu
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
  c.now = kClockSanityThreshold + 100;  // zegar poprawny: tylko ostrzeżenie o stanie
  int warns = 0;
  LinkSigner s{Cfg(1, 1, false, "/nie/ma/takiego/katalogu/state.ts"), c.fn(),
               [&warns](const std::string&) { ++warns; }};
  s.LoadPersisted();  // brak pliku -> Warn (raz)
  s.NextTimestamp();
  s.PersistIfDue(1);
  s.PersistIfDue(20000);
  EXPECT_EQ(warns, 1);
  EXPECT_EQ(s.NextTimestamp(), kClockSanityThreshold + 101);
}

// ---------------------------------------------------------------- weryfikacja

TEST(LinkSignerVerify, AcceptsCorrectSignature) {
  Clock c;
  LinkSigner s{Cfg(1), c.fn()};
  EXPECT_EQ(s.Verify(Signed(Key(1), 3, 100)), SignatureVerdict::kOk);
}

TEST(LinkSignerVerify, RejectsWrongKey) {
  Clock c;
  LinkSigner s{Cfg(1), c.fn()};
  EXPECT_EQ(s.Verify(Signed(Key(2), 3, 100)), SignatureVerdict::kBadSignature);
}

TEST(LinkSignerVerify, RejectsTamperedPayload) {
  Clock c;
  LinkSigner s{Cfg(1), c.fn()};
  auto f = Signed(Key(1), 3, 100);
  f.raw[11] ^= 0x40;  // bit w payloadzie (podpis liczony na surowej ramce)
  EXPECT_EQ(s.Verify(f), SignatureVerdict::kBadSignature);
}

TEST(LinkSignerVerify, RejectsTamperedTimestamp) {
  Clock c;
  LinkSigner s{Cfg(1), c.fn()};
  auto f = Signed(Key(1), 3, 100);
  f.timestamp = 101;
  EXPECT_EQ(s.Verify(f), SignatureVerdict::kBadSignature);
}

TEST(LinkSignerVerify, ReplayWithSameOrOlderTimestampRejected) {
  Clock c;
  LinkSigner s{Cfg(1), c.fn()};
  ASSERT_EQ(s.Verify(Signed(Key(1), 3, 100)), SignatureVerdict::kOk);
  EXPECT_EQ(s.Verify(Signed(Key(1), 3, 100)), SignatureVerdict::kReplay);
  EXPECT_EQ(s.Verify(Signed(Key(1), 3, 99)), SignatureVerdict::kReplay);
  EXPECT_EQ(s.Verify(Signed(Key(1), 3, 101)), SignatureVerdict::kOk);
}

TEST(LinkSignerVerify, StreamsAreIndependentPerSysCompLink) {
  Clock c;
  LinkSigner s{Cfg(1), c.fn()};
  ASSERT_EQ(s.Verify(Signed(Key(1), 3, 500, 1, 1)), SignatureVerdict::kOk);
  EXPECT_EQ(s.Verify(Signed(Key(1), 4, 500, 1, 1)), SignatureVerdict::kOk);  // inny link_id
  EXPECT_EQ(s.Verify(Signed(Key(1), 3, 500, 2, 1)), SignatureVerdict::kOk);  // inny sysid
  EXPECT_EQ(s.Verify(Signed(Key(1), 3, 500, 1, 2)), SignatureVerdict::kOk);  // inny compid
}

TEST(LinkSignerVerify, NewStreamMuchOlderThanKnownIsRejected) {
  Clock c;
  LinkSigner s{Cfg(1), c.fn()};
  const std::uint64_t big = 10 * kStartupFloor;
  ASSERT_EQ(s.Verify(Signed(Key(1), 3, big)), SignatureVerdict::kOk);
  EXPECT_EQ(s.Verify(Signed(Key(1), 4, big - 2 * kStartupFloor)), SignatureVerdict::kReplay);
  EXPECT_EQ(s.Verify(Signed(Key(1), 4, big - kStartupFloor / 2)), SignatureVerdict::kOk);
}

TEST(LinkSignerVerify, RecordedFramesAreReplayRejectedAfterReceiverRestart) {
  const auto path = TmpPath("state_replay_restart");
  std::remove(path.c_str());
  Clock c;
  c.now = 1000000;
  std::vector<Frame> recorded;
  {
    LinkSigner s{Cfg(1, 1, false, path), c.fn()};
    s.LoadPersisted();
    for (int i = 0; i < 3; ++i) {
      c.now += 100;
      recorded.push_back(Signed(Key(1), 3, c.now));
      ASSERT_EQ(s.Verify(recorded.back()), SignatureVerdict::kOk);
    }
    s.NextTimestamp();
    s.PersistIfDue(1);
  }
  // restart odbiorcy po > 60 s: nagrane ramki (nowy strumien, brak last_peer_) -> replay
  c.now += 10 * kStartupFloor;
  LinkSigner restarted{Cfg(1, 1, false, path), c.fn()};
  restarted.LoadPersisted();
  for (const auto& f : recorded) {
    EXPECT_EQ(restarted.Verify(f), SignatureVerdict::kReplay);
  }
  // swieza ramka GS (zegar biezacy) przechodzi
  EXPECT_EQ(restarted.Verify(Signed(Key(1), 3, c.now + 10)), SignatureVerdict::kOk);
  std::remove(path.c_str());
}

TEST(LinkSignerVerify, LastValidPeerTimestampIsPersistedAndOlderFramesRejectedAfterRestart) {
  // odbiornik, ktory nigdy nic nie nadal (last_tx_ = 0): chroni go tylko persist peera (rev. 6)
  const auto path = TmpPath("state_peer");
  std::remove(path.c_str());
  Clock c;
  const std::uint64_t t1 = 10 * kStartupFloor;
  const std::uint64_t t3 = t1 + 5 * kStartupFloor;
  {
    LinkSigner s{Cfg(1, 1, false, path), c.fn()};
    s.LoadPersisted();
    ASSERT_EQ(s.Verify(Signed(Key(1), 3, t1)), SignatureVerdict::kOk);
    ASSERT_EQ(s.Verify(Signed(Key(1), 3, t3)), SignatureVerdict::kOk);
    s.PersistIfDue(1);
  }
  c.now = 0;  // zegar po restarcie nieustawiony
  LinkSigner restarted{Cfg(1, 1, false, path), c.fn()};
  restarted.LoadPersisted();
  EXPECT_EQ(restarted.last_peer(), t3);
  // nagrane ramki starsze niz 60 s wzgledem ostatniego waznego -> odrzucone
  EXPECT_EQ(restarted.Verify(Signed(Key(1), 3, t1)), SignatureVerdict::kReplay);
  EXPECT_EQ(restarted.Verify(Signed(Key(1), 4, t1 + 1)), SignatureVerdict::kReplay);
  // swieza ramka (wiekszy ts) przechodzi
  EXPECT_EQ(restarted.Verify(Signed(Key(1), 3, t3 + 10)), SignatureVerdict::kOk);
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
  ASSERT_EQ(s.Verify(Signed(Key(1), 3, 50)), SignatureVerdict::kOk);
  s.PersistIfDue(20000);  // zmienil sie tylko last_peer_
  std::ifstream in{path};
  std::uint64_t tx = 0, peer = 0;
  in >> tx >> peer;
  EXPECT_EQ(tx, 100U);
  EXPECT_EQ(peer, 50U);
  std::remove(path.c_str());
}

TEST(LinkSignerVerify, TruncatedSignedFrameDoesNotSwallowTheNextValidFrame) {
  // tester M2a-2: urwana podpisana ramka (np. tx_partial) ma poprawne CRC; parser nie moze
  // polknac nastepnej ramki - po zlym podpisie rescan od bajtu po STX.
  const auto key = Key(1);
  const SigningParams sp1{key, 1, 1000};
  const SigningParams sp2{key, 1, 2000};
  const auto a = BuildFrame(2, 1, 1, 1, 200, std::vector<std::uint8_t>{1, 2, 3, 4, 5}, 42, &sp1);
  const auto b = BuildFrame(2, 2, 1, 1, 200, std::vector<std::uint8_t>{9, 8, 7}, 42, &sp2);
  for (std::size_t cut = 1; cut <= 20; ++cut) {
    Clock c;
    LinkSigner signer{Cfg(1), c.fn()};
    FrameParser parser{[](std::uint32_t) -> std::optional<std::uint8_t> { return 42; }};
    parser.SetFilter([&signer](Frame& f) {
      const auto v = signer.Verify(f);
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
  s.PersistIfDue(2000);  // < 10 s: bez zapisu
  s.PersistNow();        // zatrzymanie: zapis mimo limitu
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
  s.PersistIfDue(20000);  // last_tx_ bez zmian: brak zapisu
  std::ifstream in{path};
  EXPECT_FALSE(in.good());
}

TEST(LinkSignerVerify, UnsignedRejectedUnlessAccepted) {
  Clock c;
  LinkSigner strict{Cfg(1, 1, false), c.fn()};
  LinkSigner lax{Cfg(1, 1, true), c.fn()};
  const auto f = Parse(BuildFrame(2, 1, 1, 1, 200, std::vector<std::uint8_t>{1}, 42));
  EXPECT_EQ(strict.Verify(f), SignatureVerdict::kUnsigned);
  EXPECT_EQ(lax.Verify(f), SignatureVerdict::kOk);
}

TEST(LinkSignerVerify, StateOnlyUpdatedByValidFrames) {
  Clock c;
  LinkSigner s{Cfg(1), c.fn()};
  EXPECT_EQ(s.Verify(Signed(Key(9), 3, 777)), SignatureVerdict::kBadSignature);
  EXPECT_EQ(s.last_peer(), 0U);
  ASSERT_EQ(s.Verify(Signed(Key(1), 3, 50)), SignatureVerdict::kOk);  // zły podpis nie zablokował 50
  EXPECT_EQ(s.last_peer(), 50U);
}

TEST(LinkSignerVerify, SenderRestartScenarioR1) {
  // Nadawca (BBB) działał do ts=1500, zapisał stan i restartuje się bez RTC (now=0).
  const auto path = TmpPath("state_restart");
  WriteFile(path, "1500\n");
  Clock sender_clock;
  sender_clock.now = 0;
  LinkSigner sender{Cfg(1, 1, false, path), sender_clock.fn()};
  sender.LoadPersisted();
  Clock rx_clock;
  LinkSigner receiver{Cfg(1), rx_clock.fn()};
  // Odbiorca znał strumień nadawcy do 1500.
  ASSERT_EQ(receiver.Verify(Signed(Key(1), 1, 1500)), SignatureVerdict::kOk);
  // Po restarcie pierwsza ramka ma znacznik > 1500 dzięki podłodze +60 s.
  const auto ts = sender.NextTimestamp();
  EXPECT_GT(ts, 1500U);
  EXPECT_EQ(receiver.Verify(Signed(Key(1), 1, ts)), SignatureVerdict::kOk);
  std::remove(path.c_str());
}

TEST(LinkSignerVerify, BothSidesRestartPeerTimestampAdoptedAfterFirstFrame) {
  // BBB restartuje bez stanu; GS wciąż ma duży znacznik. BBB do pierwszej ramki GS
  // nadaje z małym ts (GS odrzuci), po odebraniu ramki GS przejmuje jej znacznik.
  Clock c;
  c.now = 0;
  LinkSigner bbb{Cfg(1), c.fn()};
  const auto first = bbb.NextTimestamp();
  EXPECT_LT(first, 100U);
  ASSERT_EQ(bbb.Verify(Signed(Key(1), 7, 9'000'000)), SignatureVerdict::kOk);
  EXPECT_EQ(bbb.NextTimestamp(), 9'000'001U);
}

}  // namespace
}  // namespace mavlink
}  // namespace com
}  // namespace ara
