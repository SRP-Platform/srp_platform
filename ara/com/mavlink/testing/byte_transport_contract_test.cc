/**
 * @file byte_transport_contract_test.cc
 * @copyright Copyright (c) 2024
 */
#include "ara/com/mavlink/testing/byte_transport_contract_test.h"

#include <atomic>
#include <chrono>  // NOLINT
#include <thread>  // NOLINT

#include "ara/com/mavlink/transport_error.h"

namespace ara {
namespace com {
namespace mavlink {
namespace testing {
namespace {
using Clock = std::chrono::steady_clock;

std::vector<std::uint8_t> Pattern(std::size_t n, std::uint8_t seed = 1) {
  std::vector<std::uint8_t> v(n);
  for (std::size_t i = 0; i < n; ++i) {
    v[i] = static_cast<std::uint8_t>(seed + i * 7);
  }
  return v;
}

/// Czyta z transportu dokładnie `want` bajtów (w pętli) albo do upływu `total_ms`.
std::vector<std::uint8_t> ReadExactly(IByteTransport* t, std::size_t want,
                                      std::uint32_t total_ms) {
  std::vector<std::uint8_t> out;
  const auto deadline = Clock::now() + std::chrono::milliseconds(total_ms);
  std::uint8_t buf[256];
  while (out.size() < want && Clock::now() < deadline) {
    const auto r = t->Read(std::span<std::uint8_t>{buf, sizeof(buf)}, 50, {});
    if (!r.HasValue()) {
      break;
    }
    out.insert(out.end(), buf, buf + r.Value());
  }
  return out;
}

std::vector<std::uint8_t> PeerReadAll(const ByteTransportHarness& h,
                                      std::size_t want, std::uint32_t total_ms) {
  std::vector<std::uint8_t> out;
  const auto deadline = Clock::now() + std::chrono::milliseconds(total_ms);
  while (out.size() < want && Clock::now() < deadline) {
    const auto part = h.peer_read(want - out.size(), 50);
    out.insert(out.end(), part.begin(), part.end());
  }
  return out;
}
}  // namespace

std::string ContractParamName(
    const ::testing::TestParamInfo<ContractParam>& info) {
  return info.param.name;
}

TEST_P(ByteTransportContractTest, NameIsStable) {
  EXPECT_FALSE(h_.transport->Name().empty());
  EXPECT_EQ(h_.transport->Name(), h_.transport->Name());
}

TEST_P(ByteTransportContractTest, OpenThenReadWriteRoundTrip) {
  ASSERT_TRUE(h_.transport->Open({}).HasValue());
  const auto in = Pattern(100);
  h_.peer_write(in);
  EXPECT_EQ(ReadExactly(h_.transport.get(), in.size(), 2000), in);

  const auto out = Pattern(64, 9);
  const auto w = h_.transport->Write(std::span<const std::uint8_t>{out}, 1000, {});
  ASSERT_TRUE(w.HasValue());
  EXPECT_EQ(w.Value(), out.size());
  EXPECT_EQ(PeerReadAll(h_, out.size(), 2000), out);
}

TEST_P(ByteTransportContractTest, ReadTimeoutReturnsZero) {
  ASSERT_TRUE(h_.transport->Open({}).HasValue());
  std::uint8_t buf[16];
  const auto t0 = Clock::now();
  const auto r = h_.transport->Read(std::span<std::uint8_t>{buf, sizeof(buf)}, 150, {});
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      Clock::now() - t0).count();
  ASSERT_TRUE(r.HasValue());
  EXPECT_EQ(r.Value(), 0U);
  EXPECT_GE(ms, 140);
  EXPECT_LT(ms, 1500);
}

TEST_P(ByteTransportContractTest, CloseUnblocksRead) {
  ASSERT_TRUE(h_.transport->Open({}).HasValue());
  std::atomic<bool> done{false};
  std::atomic<bool> got_error{false};
  std::thread reader{[&] {
    std::uint8_t buf[16];
    const auto r = h_.transport->Read(std::span<std::uint8_t>{buf, sizeof(buf)}, 10000, {});
    got_error = !r.HasValue();
    done = true;
  }};
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  EXPECT_FALSE(done.load());
  const auto t0 = Clock::now();
  h_.transport->Close();
  reader.join();
  EXPECT_TRUE(done.load());
  EXPECT_TRUE(got_error.load());
  EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count(), 2000);
}

TEST_P(ByteTransportContractTest, StopTokenUnblocksRead) {
  ASSERT_TRUE(h_.transport->Open({}).HasValue());
  std::stop_source src;
  std::atomic<bool> done{false};
  std::thread reader{[&] {
    std::uint8_t buf[16];
    std::ignore = h_.transport->Read(std::span<std::uint8_t>{buf, sizeof(buf)}, 10000, src.get_token());
    done = true;
  }};
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  EXPECT_FALSE(done.load());
  src.request_stop();
  reader.join();
  EXPECT_TRUE(done.load());
}

TEST_P(ByteTransportContractTest, ReadAfterCloseIsError) {
  ASSERT_TRUE(h_.transport->Open({}).HasValue());
  h_.transport->Close();
  std::uint8_t buf[8];
  const auto r = h_.transport->Read(std::span<std::uint8_t>{buf, sizeof(buf)}, 50, {});
  EXPECT_FALSE(r.HasValue());
}

TEST_P(ByteTransportContractTest, CloseIsIdempotentAndSafeWithoutOpen) {
  h_.transport->Close();
  ASSERT_TRUE(h_.transport->Open({}).HasValue());
  h_.transport->Close();
  h_.transport->Close();
}

TEST_P(ByteTransportContractTest, OpenAfterCloseWorks) {
  ASSERT_TRUE(h_.transport->Open({}).HasValue());
  h_.transport->Close();
  ASSERT_TRUE(h_.transport->Open({}).HasValue());
  const auto in = Pattern(40, 3);
  h_.peer_write(in);
  EXPECT_EQ(ReadExactly(h_.transport.get(), in.size(), 2000), in);
  const auto out = Pattern(30, 5);
  ASSERT_TRUE(h_.transport->Write(std::span<const std::uint8_t>{out}, 1000, {}).HasValue());
  EXPECT_EQ(PeerReadAll(h_, out.size(), 2000), out);
}

TEST_P(ByteTransportContractTest, ConcurrentReadAndWrite) {
  ASSERT_TRUE(h_.transport->Open({}).HasValue());
  const auto in = Pattern(2000, 11);
  const auto out = Pattern(2000, 13);
  std::vector<std::uint8_t> received;
  std::thread reader{[&] { received = ReadExactly(h_.transport.get(), in.size(), 5000); }};
  std::thread feeder{[&] {
    for (std::size_t i = 0; i < in.size(); i += 100) {
      h_.peer_write(std::vector<std::uint8_t>{in.begin() + i, in.begin() + i + 100});
    }
  }};
  std::size_t sent = 0;
  while (sent < out.size()) {
    const auto w = h_.transport->Write(
        std::span<const std::uint8_t>{out.data() + sent, 100}, 1000, {});
    ASSERT_TRUE(w.HasValue());
    sent += w.Value();
  }
  feeder.join();
  reader.join();
  EXPECT_EQ(received, in);
  EXPECT_EQ(PeerReadAll(h_, out.size(), 5000), out);
}

TEST_P(ByteTransportContractTest, WriteTimeoutInTheMiddleThenNextWriteIsIntact) {
  if (h_.overflow_bytes == 0) {
    GTEST_SKIP() << "harness nie umie przepełnić przewodu";
  }
  ASSERT_TRUE(h_.transport->Open({}).HasValue());
  const auto big = Pattern(h_.overflow_bytes, 21);
  const auto w = h_.transport->Write(std::span<const std::uint8_t>{big}, 200, {});
  ASSERT_TRUE(w.HasValue());                 // zapis częściowy, nie błąd
  const std::size_t accepted = w.Value();
  EXPECT_LT(accepted, big.size());

  // Druga strona opróżnia przewód; kolejny zapis musi dotrzeć w całości.
  std::vector<std::uint8_t> drained;
  const auto t_end = Clock::now() + std::chrono::milliseconds(3000);
  while (drained.size() < accepted && Clock::now() < t_end) {
    const auto part = h_.peer_read(65536, 100);
    drained.insert(drained.end(), part.begin(), part.end());
  }
  ASSERT_EQ(drained.size(), accepted);
  EXPECT_TRUE(std::equal(drained.begin(), drained.end(), big.begin()));

  const auto marker = Pattern(32, 77);
  const auto w2 = h_.transport->Write(std::span<const std::uint8_t>{marker}, 1000, {});
  ASSERT_TRUE(w2.HasValue());
  ASSERT_EQ(w2.Value(), marker.size());
  EXPECT_EQ(PeerReadAll(h_, marker.size(), 2000), marker);
}

TEST_P(ByteTransportContractTest, OpenAfterCloseWorksWhileReadIsStillBlocked) {
  ASSERT_TRUE(h_.transport->Open({}).HasValue());
  std::atomic<bool> done{false};
  std::thread reader{[&] {
    std::uint8_t buf[16];
    std::ignore = h_.transport->Read(std::span<std::uint8_t>{buf, sizeof(buf)}, 20000, {});
    done = true;
  }};
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  EXPECT_FALSE(done.load());
  h_.transport->Close();
  // Natychmiast po Close() (zablokowany Read może jeszcze nie wrócić) Open() MUSI działać.
  const auto reopened = h_.transport->Open({});
  EXPECT_TRUE(reopened.HasValue());
  reader.join();
  EXPECT_TRUE(done.load());
  if (reopened.HasValue()) {
    const auto in = Pattern(20, 41);
    h_.peer_write(in);
    EXPECT_EQ(ReadExactly(h_.transport.get(), in.size(), 2000), in);
  }
}

TEST_P(ByteTransportContractTest, WriteAfterCloseIsError) {
  ASSERT_TRUE(h_.transport->Open({}).HasValue());
  h_.transport->Close();
  const auto data = Pattern(8);
  const auto w = h_.transport->Write(std::span<const std::uint8_t>{data}, 100, {});
  EXPECT_FALSE(w.HasValue());
  if (!w.HasValue()) {
    EXPECT_TRUE(IsTransportError(w.Error(), TransportErrc::kClosed));
  }
}

TEST_P(ByteTransportContractTest, ReadAfterCloseReportsClosedCode) {
  ASSERT_TRUE(h_.transport->Open({}).HasValue());
  h_.transport->Close();
  std::uint8_t buf[8];
  const auto r = h_.transport->Read(std::span<std::uint8_t>{buf, sizeof(buf)}, 50, {});
  ASSERT_FALSE(r.HasValue());
  EXPECT_TRUE(IsTransportError(r.Error(), TransportErrc::kClosed));
}

TEST_P(ByteTransportContractTest, StopTokenUnblocksBlockedWrite) {
  if (h_.overflow_bytes == 0) {
    GTEST_SKIP() << "harness nie umie przepełnić przewodu";
  }
  ASSERT_TRUE(h_.transport->Open({}).HasValue());
  std::stop_source src;
  std::atomic<bool> done{false};
  std::thread writer{[&] {
    const auto big = Pattern(h_.overflow_bytes, 51);
    std::ignore = h_.transport->Write(std::span<const std::uint8_t>{big}, 20000, src.get_token());
    done = true;
  }};
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  EXPECT_FALSE(done.load());
  src.request_stop();
  writer.join();
  EXPECT_TRUE(done.load());
}

TEST_P(ByteTransportContractTest, CloseUnblocksBlockedWrite) {
  if (h_.overflow_bytes == 0) {
    GTEST_SKIP() << "harness nie umie przepełnić przewodu";
  }
  ASSERT_TRUE(h_.transport->Open({}).HasValue());
  std::atomic<bool> done{false};
  std::thread writer{[&] {
    const auto big = Pattern(h_.overflow_bytes, 31);
    std::ignore = h_.transport->Write(std::span<const std::uint8_t>{big}, 20000, {});
    done = true;
  }};
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  EXPECT_FALSE(done.load());
  h_.transport->Close();
  writer.join();
  EXPECT_TRUE(done.load());
}

}  // namespace testing
}  // namespace mavlink
}  // namespace com
}  // namespace ara
