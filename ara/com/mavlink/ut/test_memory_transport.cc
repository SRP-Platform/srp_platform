/**
 * @file test_memory_transport.cc
 * @brief MemoryTransport: testy kontraktu oraz mechanizmy wstrzykiwania błędów.
 * @copyright Copyright (c) 2024
 */
#include <gtest/gtest.h>

#include <chrono>  // NOLINT
#include <memory>
#include <vector>

#include "ara/com/mavlink/testing/byte_transport_contract_test.h"
#include "ara/com/mavlink/testing/memory_transport.h"
#include "ara/com/mavlink/transport_error.h"

namespace ara {
namespace com {
namespace mavlink {
namespace {
using testing::MemoryTransport;
using testing::ByteTransportContractTest;

testing::ByteTransportHarness MakeMemoryHarness() {
  auto pair = MemoryTransport::MakePair("mem", "mem-peer", 8 * 1024);
  auto peer = pair.second;
  testing::ByteTransportHarness h;
  h.transport = pair.first;
  // Peer musi być otwarty, by Write/Read działały.
  EXPECT_TRUE(peer->Open({}).HasValue());
  h.peer_write = [peer](const std::vector<std::uint8_t>& v) {
    std::size_t done = 0;
    while (done < v.size()) {
      const auto w = peer->Write(
          std::span<const std::uint8_t>{v.data() + done, v.size() - done}, 1000, {});
      ASSERT_TRUE(w.HasValue());
      done += w.Value();
    }
  };
  h.peer_read = [peer](std::size_t want, std::uint32_t timeout_ms) {
    std::vector<std::uint8_t> buf(want);
    const auto r = peer->Read(std::span<std::uint8_t>{buf}, timeout_ms, {});
    if (!r.HasValue()) {
      return std::vector<std::uint8_t>{};
    }
    buf.resize(r.Value());
    return buf;
  };
  h.overflow_bytes = 64 * 1024;
  return h;
}

INSTANTIATE_TEST_SUITE_P(
    Memory, ByteTransportContractTest,
    ::testing::Values(testing::ContractParam{"memory", MakeMemoryHarness}),
    testing::ContractParamName);

TEST(MemoryTransport, PairIsConnectedBothWays) {
  auto [a, b] = MemoryTransport::MakePair();
  ASSERT_TRUE(a->Open({}).HasValue());
  ASSERT_TRUE(b->Open({}).HasValue());
  const std::uint8_t d1[] = {1, 2, 3};
  ASSERT_TRUE(a->Write(std::span<const std::uint8_t>{d1}, 100, {}).HasValue());
  std::uint8_t buf[8];
  const auto r1 = b->Read(std::span<std::uint8_t>{buf}, 500, {});
  ASSERT_TRUE(r1.HasValue());
  EXPECT_EQ(r1.Value(), 3U);
  const std::uint8_t d2[] = {9, 8};
  ASSERT_TRUE(b->Write(std::span<const std::uint8_t>{d2}, 100, {}).HasValue());
  const auto r2 = a->Read(std::span<std::uint8_t>{buf}, 500, {});
  ASSERT_TRUE(r2.HasValue());
  EXPECT_EQ(r2.Value(), 2U);
  EXPECT_EQ(buf[0], 9);
}

TEST(MemoryTransport, OpenFailuresAreCountedAndTyped) {
  auto [a, b] = MemoryTransport::MakePair();
  a->FailNextOpens(2, /*persistent=*/true);
  auto r1 = a->Open({});
  ASSERT_FALSE(r1.HasValue());
  EXPECT_TRUE(IsTransportError(r1.Error(), TransportErrc::kPersistent));
  EXPECT_FALSE(a->Open({}).HasValue());
  EXPECT_TRUE(a->Open({}).HasValue());
  EXPECT_EQ(a->open_calls(), 3);
  a->FailNextOpens(1, false);
  a->Close();
  auto r2 = a->Open({});
  ASSERT_FALSE(r2.HasValue());
  EXPECT_TRUE(IsTransportError(r2.Error(), TransportErrc::kRetryable));
}

TEST(MemoryTransport, DeliveryDelayHoldsBytesBack) {
  auto [a, b] = MemoryTransport::MakePair();
  ASSERT_TRUE(a->Open({}).HasValue());
  ASSERT_TRUE(b->Open({}).HasValue());
  a->SetDeliveryDelayMs(300);
  const std::uint8_t d[] = {7};
  ASSERT_TRUE(a->Write(std::span<const std::uint8_t>{d}, 100, {}).HasValue());
  std::uint8_t buf[4];
  const auto t0 = std::chrono::steady_clock::now();
  const auto r = b->Read(std::span<std::uint8_t>{buf}, 2000, {});
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - t0).count();
  ASSERT_TRUE(r.HasValue());
  EXPECT_EQ(r.Value(), 1U);
  EXPECT_GE(ms, 250);
}

TEST(MemoryTransport, WriteFilterCanDropAndCorrupt) {
  auto [a, b] = MemoryTransport::MakePair();
  ASSERT_TRUE(a->Open({}).HasValue());
  ASSERT_TRUE(b->Open({}).HasValue());
  int n = 0;
  a->SetWriteFilter([&n](const std::vector<std::uint8_t>& in) {
    ++n;
    if (n == 1) {
      return std::vector<std::uint8_t>{};  // utrata
    }
    auto out = in;
    out[0] ^= 0xFF;  // uszkodzenie
    return out;
  });
  const std::uint8_t d[] = {0x10, 0x20};
  ASSERT_TRUE(a->Write(std::span<const std::uint8_t>{d}, 100, {}).HasValue());
  ASSERT_TRUE(a->Write(std::span<const std::uint8_t>{d}, 100, {}).HasValue());
  std::uint8_t buf[8];
  const auto r = b->Read(std::span<std::uint8_t>{buf}, 500, {});
  ASSERT_TRUE(r.HasValue());
  ASSERT_EQ(r.Value(), 2U);
  EXPECT_EQ(buf[0], 0xEF);
  EXPECT_EQ(buf[1], 0x20);
}

TEST(MemoryTransport, SmallCapacityGivesPartialWriteOnTimeout) {
  auto [a, b] = MemoryTransport::MakePair("a", "b", 16);
  ASSERT_TRUE(a->Open({}).HasValue());
  ASSERT_TRUE(b->Open({}).HasValue());
  const std::vector<std::uint8_t> big(100, 1);
  const auto w = a->Write(std::span<const std::uint8_t>{big}, 100, {});
  ASSERT_TRUE(w.HasValue());
  EXPECT_EQ(w.Value(), 16U);
}

TEST(MemoryTransport, FailIoGivesPersistentErrors) {
  auto [a, b] = MemoryTransport::MakePair();
  ASSERT_TRUE(a->Open({}).HasValue());
  a->SetFailIo(true);
  std::uint8_t buf[2];
  const auto r = a->Read(std::span<std::uint8_t>{buf}, 10, {});
  ASSERT_FALSE(r.HasValue());
  EXPECT_TRUE(IsTransportError(r.Error(), TransportErrc::kPersistent));
  const auto w = a->Write(std::span<const std::uint8_t>{buf}, 10, {});
  ASSERT_FALSE(w.HasValue());
}

}  // namespace
}  // namespace mavlink
}  // namespace com
}  // namespace ara
