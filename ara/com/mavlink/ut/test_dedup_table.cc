/**
 * Copyright (c) 2024
 *
 * @file test_dedup_table.cc
 */
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>  // NOLINT
#include <thread>  // NOLINT
#include <vector>

#include "ara/com/mavlink/dedup_table.h"

namespace ara {
namespace com {
namespace mavlink {
namespace {
using std::chrono::milliseconds;
using Clock = DedupTable::Clock;

DedupTable::Key K(std::uint8_t sys = 1, std::uint8_t comp = 1, std::uint32_t msg = 200,
                  std::uint16_t inst = 3, std::uint8_t ts = 0, std::uint8_t tc = 0) {
  return DedupTable::Key{sys, comp, msg, inst, ts, tc};
}

TEST(Crc32, KnownVector) {
  const std::vector<std::uint8_t> v{'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  EXPECT_EQ(Crc32(v), 0xCBF43926U);
  EXPECT_EQ(Crc32({}), 0U);
}

TEST(DedupTable, FirstIsNewSecondIsDuplicate) {
  DedupTable t;
  const auto now = Clock::now();
  EXPECT_FALSE(t.TestAndInsert(K(), 5, 111, milliseconds(1000), now));
  EXPECT_TRUE(t.TestAndInsert(K(), 5, 111, milliseconds(1000), now));
}

TEST(DedupTable, SameSeqDifferentCrcIsNotDuplicate) {
  // restart nadawcy: ten sam seq, inna treść -> nie wolno zgubić (R2)
  DedupTable t;
  const auto now = Clock::now();
  EXPECT_FALSE(t.TestAndInsert(K(), 5, 111, milliseconds(1000), now));
  EXPECT_FALSE(t.TestAndInsert(K(), 5, 222, milliseconds(1000), now));
}

TEST(DedupTable, DifferentSeqSameCrcIsNotDuplicate) {
  DedupTable t;
  const auto now = Clock::now();
  EXPECT_FALSE(t.TestAndInsert(K(), 5, 111, milliseconds(1000), now));
  EXPECT_FALSE(t.TestAndInsert(K(), 6, 111, milliseconds(1000), now));
}

TEST(DedupTable, EntryExpiresAfterTtl) {
  DedupTable t;
  const auto t0 = Clock::now();
  EXPECT_FALSE(t.TestAndInsert(K(), 5, 111, milliseconds(100), t0));
  EXPECT_TRUE(t.TestAndInsert(K(), 5, 111, milliseconds(100), t0 + milliseconds(50)));
  EXPECT_FALSE(t.TestAndInsert(K(), 5, 111, milliseconds(100), t0 + milliseconds(250)));
}

TEST(DedupTable, DelayedCopyWithinTtlStillDeduplicated) {
  // R2: jedno łącze opóźnione o 3 s, TTL 10 s -> druga kopia to duplikat
  DedupTable t;
  const auto t0 = Clock::now();
  EXPECT_FALSE(t.TestAndInsert(K(), 77, 5, milliseconds(10000), t0));
  EXPECT_TRUE(t.TestAndInsert(K(), 77, 5, milliseconds(10000), t0 + milliseconds(3000)));
}

TEST(DedupTable, KeysAreIndependent) {
  DedupTable t;
  const auto now = Clock::now();
  EXPECT_FALSE(t.TestAndInsert(K(1, 1, 200, 3), 5, 1, milliseconds(1000), now));
  EXPECT_FALSE(t.TestAndInsert(K(2, 1, 200, 3), 5, 1, milliseconds(1000), now));   // sysid
  EXPECT_FALSE(t.TestAndInsert(K(1, 2, 200, 3), 5, 1, milliseconds(1000), now));   // compid
  EXPECT_FALSE(t.TestAndInsert(K(1, 1, 201, 3), 5, 1, milliseconds(1000), now));   // msgid
  EXPECT_FALSE(t.TestAndInsert(K(1, 1, 200, 4), 5, 1, milliseconds(1000), now));   // instance
  EXPECT_FALSE(t.TestAndInsert(K(1, 1, 200, 3, 7, 7), 5, 1, milliseconds(1000), now));  // target
}

TEST(DedupTable, SequenceWrapAroundIsHandled) {
  DedupTable t;
  const auto now = Clock::now();
  EXPECT_FALSE(t.TestAndInsert(K(), 65535, 9, milliseconds(1000), now));
  EXPECT_FALSE(t.TestAndInsert(K(), 0, 10, milliseconds(1000), now));
  EXPECT_TRUE(t.TestAndInsert(K(), 65535, 9, milliseconds(1000), now));
  EXPECT_TRUE(t.TestAndInsert(K(), 0, 10, milliseconds(1000), now));
}

TEST(DedupTable, WindowCoversRateTimesTtl) {
  // 20 Hz x TTL 10 s = 200 wpisow: kopia spozniona o > 64 ramek nadal jest wykryta (QA #3)
  DedupTable t;
  const auto now = Clock::now();
  for (std::uint16_t i = 0; i < 200; ++i) {
    EXPECT_FALSE(t.TestAndInsert(K(), i, i, milliseconds(10000), now));
  }
  EXPECT_TRUE(t.TestAndInsert(K(), 0, 0, milliseconds(10000), now));
}

TEST(DedupTable, WindowIsBoundedInMemory) {
  DedupTable t;
  const auto now = Clock::now();
  const std::uint32_t n = static_cast<std::uint32_t>(DedupTable::kWindow) + 10;
  for (std::uint32_t i = 0; i < n; ++i) {
    EXPECT_FALSE(t.TestAndInsert(K(), static_cast<std::uint16_t>(i), i, milliseconds(10000), now));
  }
  EXPECT_LE(t.size(), DedupTable::kWindow);
  EXPECT_FALSE(t.TestAndInsert(K(), 0, 0, milliseconds(10000), now));  // wypadl z okna
}

TEST(DedupTable, LossOfMoreThan64DoesNotBreakNewMessages) {
  DedupTable t;
  const auto now = Clock::now();
  EXPECT_FALSE(t.TestAndInsert(K(), 10, 1, milliseconds(10000), now));
  EXPECT_FALSE(t.TestAndInsert(K(), 300, 2, milliseconds(10000), now));  // luka > 64
  EXPECT_TRUE(t.TestAndInsert(K(), 300, 2, milliseconds(10000), now));
}

TEST(DedupTable, ConcurrentCopiesYieldExactlyOneWinner) {
  // 2 łącza dostarczają tę samą wiadomość równocześnie -> dokładnie 1 "nowa"
  for (int round = 0; round < 200; ++round) {
    DedupTable t;
    std::atomic<int> fresh{0};
    std::atomic<bool> go{false};
    const auto now = Clock::now();
    auto worker = [&] {
      while (!go.load()) {
      }
      if (!t.TestAndInsert(K(), 42, 4242, milliseconds(1000), now)) {
        fresh++;
      }
    };
    std::thread a{worker};
    std::thread b{worker};
    std::thread c{worker};
    go = true;
    a.join();
    b.join();
    c.join();
    ASSERT_EQ(fresh.load(), 1) << "runda " << round;
  }
}

TEST(DedupTable, SizeReflectsEntries) {
  DedupTable t;
  const auto now = Clock::now();
  t.TestAndInsert(K(), 1, 1, milliseconds(1000), now);
  t.TestAndInsert(K(), 2, 2, milliseconds(1000), now);
  t.TestAndInsert(K(2), 1, 1, milliseconds(1000), now);
  EXPECT_EQ(t.size(), 3U);
}

}  // namespace
}  // namespace mavlink
}  // namespace com
}  // namespace ara
