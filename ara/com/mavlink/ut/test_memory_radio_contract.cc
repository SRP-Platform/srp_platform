/**
 * @file test_memory_radio_contract.cc
 * @copyright Copyright (c) 2024
 * @brief The IRadioDriver contract suite on MemoryRadio (the in-memory test driver), including
 * the OnLinkStats rules: only from the driver thread, never after Stop().
 */
#include <chrono>  // NOLINT
#include <deque>
#include <memory>
#include <mutex>  // NOLINT
#include <thread>  // NOLINT
#include <vector>

#include "ara/com/mavlink/testing/memory_radio.h"
#include "ara/com/mavlink/testing/radio_driver_contract_test.h"

namespace {
using ara::com::mavlink::testing::MemoryRadio;
using ara::com::mavlink::testing::RadioContractParam;
using ara::com::mavlink::testing::RadioDriverHarness;

RadioDriverHarness MakeHarness() {
  auto radio = std::make_shared<MemoryRadio>("memory_contract");
  auto consumed = std::make_shared<std::size_t>(0);
  auto pending = std::make_shared<std::deque<std::uint8_t>>();
  RadioDriverHarness h;
  h.driver = radio;
  h.peer_write = [radio](const std::vector<std::uint8_t>& b) { radio->Inject(b); };
  h.peer_read = [radio, consumed, pending](std::size_t want, std::uint32_t timeout_ms) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    for (;;) {
      const auto sent = radio->sent();
      while (*consumed < sent.size()) {
        pending->insert(pending->end(), sent[*consumed].begin(), sent[*consumed].end());
        ++*consumed;
      }
      if (!pending->empty() || std::chrono::steady_clock::now() >= deadline) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    std::vector<std::uint8_t> out;
    while (out.size() < want && !pending->empty()) {
      out.push_back(pending->front());
      pending->pop_front();
    }
    return out;
  };
  h.emit_link_stats = [radio](const ara::com::mavlink::RadioLinkStats& s) { radio->LinkStats(s); };
  return h;
}
}  // namespace

using ara::com::mavlink::testing::RadioDriverContractTest;
INSTANTIATE_TEST_SUITE_P(MemoryRadio, RadioDriverContractTest,
                         ::testing::Values(RadioContractParam{"memory_radio", [] { return MakeHarness(); }}),
                         ara::com::mavlink::testing::RadioContractParamName);

TEST(MemoryRadioMaxFrameSize, DefaultIsUnlimitedAndSettable) {
  MemoryRadio r;
  EXPECT_EQ(r.MaxFrameSize(), 0U);
  r.SetMaxFrameSize(255);
  const ara::com::mavlink::IRadioDriver& d = r;
  EXPECT_EQ(d.MaxFrameSize(), 255U);
}
