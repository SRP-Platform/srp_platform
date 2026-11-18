/**
 * Copyright (c) 2024
 *
 * @file test_link_manager.cc
 * @brief E2E w procesie: skeleton (manager A) <-> proxy (manager B) przez pary
 * transportów w pamięci: eventy, metody, wykluczenia, redundancja, filtry, podpis.
 */
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>  // NOLINT
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>  // NOLINT
#include <string>
#include <optional>
#include <thread>  // NOLINT
#include <tuple>  // NOLINT
#include <vector>

#include "ara/com/com_error_domain.h"
#include "ara/com/mavlink/link_manager.h"
#include "ara/com/mavlink/testing/memory_transport.h"

namespace ara {
namespace com {
namespace mavlink {
namespace {
using ara::com::ComErrc;
using ara::core::model::ModelMavlink;
using ara::core::model::ModelMavlinkElement;
using ara::core::model::ModelMavlinkField;
using ara::core::model::ModelMavlinkLink;
using ara::core::model::ModelMavlinkMessage;
using ara::core::model::ModelMavlinkOptions;
using testing::MemoryTransport;
using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;
using Type = ModelMavlinkField::Type;
using Role = ModelMavlinkField::Role;

constexpr std::uint16_t kPosition = 0x8001;
constexpr std::uint16_t kStatus = 0x8002;  // wyłączony w service.json
constexpr std::uint16_t kSetMode = 1;
constexpr std::uint16_t kReboot = 2;
constexpr std::uint16_t kName = 3;

ModelMavlinkField F(const std::string& n, Type t, Role r = Role::kPayload, std::uint16_t arr = 0) {
  ModelMavlinkField f;
  f.name = n;
  f.type = t;
  f.role = r;
  f.array_len = arr;
  return f;
}

std::vector<ModelMavlinkField> Hdr(bool method) {
  std::vector<ModelMavlinkField> v{F("seq", Type::kU16, Role::kSeq),
                                   F("instance", Type::kU16, Role::kInstance)};
  if (method) {
    v.push_back(F("target_system", Type::kU8, Role::kTargetSystem));
    v.push_back(F("target_component", Type::kU8, Role::kTargetComponent));
  }
  return v;
}

ModelMavlinkMessage M(std::uint32_t id, const std::string& name, std::vector<ModelMavlinkField> f) {
  ModelMavlinkMessage m;
  m.msg_id = id;
  m.name = name;
  m.fields = std::move(f);
  m.crc_extra = ComputeCrcExtra(m);
  return m;
}

std::vector<ModelMavlinkElement> Elements(double position_rate = 0.0) {
  std::vector<ModelMavlinkElement> out;
  {
    ModelMavlinkElement e;
    e.id = kPosition;
    e.is_event = true;
    e.enabled = true;
    e.name = "Position";
    auto f = Hdr(false);
    f.push_back(F("lat", Type::kF32));
    f.push_back(F("lon", Type::kF32));
    f.push_back(F("fix_type", Type::kU8));
    e.req = M(200, "T_POSITION", f);
    e.max_rate_hz = position_rate;
    out.push_back(e);
  }
  {
    ModelMavlinkElement e;
    e.id = kStatus;
    e.is_event = true;
    e.enabled = false;
    e.name = "Status";
    out.push_back(e);
  }
  {
    ModelMavlinkElement e;
    e.id = kSetMode;
    e.name = "SetMode";
    e.enabled = true;
    auto rq = Hdr(true);
    rq.push_back(F("value", Type::kU8));
    e.req = M(210, "T_SET_MODE_REQ", rq);
    auto rs = Hdr(true);
    rs.push_back(F("status", Type::kU8, Role::kStatus));
    rs.push_back(F("value", Type::kU8));
    e.has_resp = true;
    e.resp = M(211, "T_SET_MODE_RESP", rs);
    e.timeout_ms = 300;
    out.push_back(e);
  }
  {
    ModelMavlinkElement e;
    e.id = kReboot;
    e.name = "Reboot";
    e.enabled = true;
    e.req = M(212, "T_REBOOT_REQ", Hdr(true));
    out.push_back(e);
  }
  {
    ModelMavlinkElement e;
    e.id = kName;
    e.name = "SetName";
    e.enabled = true;
    auto rq = Hdr(true);
    rq.push_back(F("value", Type::kChar, Role::kPayload, 8));
    e.req = M(213, "T_SET_NAME_REQ", rq);
    auto rs = Hdr(true);
    rs.push_back(F("status", Type::kU8, Role::kStatus));
    rs.push_back(F("value", Type::kU32));
    e.has_resp = true;
    e.resp = M(214, "T_SET_NAME_RESP", rs);
    e.timeout_ms = 300;
    out.push_back(e);
  }
  return out;
}

ModelMavlinkLink Link(const std::string& name, bool signing = false, const std::string& key = "") {
  ModelMavlinkLink l;
  l.name = name;
  l.version = 2;
  l.heartbeat_hz = 0;
  l.signing_enabled = signing;
  l.signing_key_file = key;
  l.signing_link_id = 1;
  return l;
}

std::vector<std::uint8_t> LeF(float v) {
  std::vector<std::uint8_t> b(4);
  std::memcpy(b.data(), &v, 4);
  return b;
}

std::vector<std::uint8_t> PositionPayload(float lat, float lon, std::uint8_t fix) {
  auto p = LeF(lat);
  const auto l2 = LeF(lon);
  p.insert(p.end(), l2.begin(), l2.end());
  p.push_back(fix);
  return p;
}

class ManagerTest : public ::testing::Test {
 protected:
  struct Net {
    std::map<std::string, std::shared_ptr<MemoryTransport>> a;  // strona skeletonu
    std::map<std::string, std::shared_ptr<MemoryTransport>> b;  // strona proxy
  };

  void SetUp() override {
    mgr_a_ = MakeManager(&net_.a);
    mgr_b_ = MakeManager(&net_.b);
  }
  void TearDown() override {
    if (ep_a_) mgr_a_->DetachEndpoint(ep_a_);
    if (ep_b_) mgr_b_->DetachEndpoint(ep_b_);
    mgr_a_->Shutdown();
    mgr_b_->Shutdown();
  }

  void AddLink(const std::string& name, std::size_t capacity = 64 * 1024) {
    auto p = MemoryTransport::MakePair(name + "-a", name + "-b", capacity);
    net_.a[name] = p.first;
    net_.b[name] = p.second;
  }

  std::unique_ptr<LinkManager> MakeManager(std::map<std::string, std::shared_ptr<MemoryTransport>>* m) {
    LinkManager::Options o;
    o.transport_factory = [m](const ModelMavlinkLink& l) {
      std::shared_ptr<MemoryTransport> t = m->at(l.name);
      return MavlinkLink::TransportProvider{
          [t](milliseconds, std::stop_token) { return std::shared_ptr<IByteTransport>{t}; }};
    };
    o.test_initial_seq = initial_seq_;
    o.retry_interval = milliseconds(40);
    o.read_timeout_ms = 20;
    o.write_timeout_ms = 100;
    o.now = [] { return kClockSanityThreshold + 5000; };
    o.log = [this](LogLevel l, const std::string& s) {
      std::lock_guard<std::mutex> lock{log_mu_};
      logs_.emplace_back(l, s);
    };
    return std::make_unique<LinkManager>(o);
  }

  ModelMavlink Model(std::uint16_t instance, std::uint8_t sys, std::uint8_t comp,
                     const std::vector<std::string>& links, ModelMavlinkOptions opts,
                     std::uint8_t target_sys = 0, std::uint8_t target_comp = 0) {
    std::vector<ModelMavlinkLink> l;
    for (const auto& n : links) l.push_back(Link(n, signing_, key_));
    if (opts.elements.empty()) opts.elements = Elements();
    return ModelMavlink::CreatSkeletonItem("c" + std::to_string(instance), instance, 1, 0, sys, comp,
                                           target_sys, target_comp, "test", l, opts)
        .Value();
  }

  void Attach(const std::vector<std::string>& links, ModelMavlinkOptions skel = {},
              ModelMavlinkOptions prox = {}, std::uint16_t inst_a = 3, std::uint16_t inst_b = 3) {
    ep_a_ = mgr_a_->AttachEndpoint(Model(inst_a, 1, 1, links, skel), true);
    ep_b_ = mgr_b_->AttachEndpoint(Model(inst_b, 2, 1, links, prox, 1, 1), false);
    ep_a_->SetMethodHandler([this](std::uint16_t id, const std::vector<std::uint8_t>& p) {
      calls_++;
      {
        std::lock_guard<std::mutex> lock{mu_};
        last_method_ = id;
        last_payload_ = p;
      }
      if (delay_ms_ > 0) std::this_thread::sleep_for(milliseconds(delay_ms_));
      return handler_(id, p);
    });
    ep_b_->SetEventHandler([this](std::uint16_t id, const std::vector<std::uint8_t>& p) {
      std::lock_guard<std::mutex> lock{mu_};
      events_.emplace_back(id, p);
    });
    WaitReady();
  }

  void WaitReady() {
    const auto end = Clock::now() + milliseconds(2000);
    while (Clock::now() < end && !(ep_a_->AnyLinkReady() && ep_b_->AnyLinkReady())) {
      std::this_thread::sleep_for(milliseconds(5));
    }
    ASSERT_TRUE(ep_a_->AnyLinkReady());
    ASSERT_TRUE(ep_b_->AnyLinkReady());
  }

  bool WaitEvents(std::size_t n, std::uint32_t ms = 2000) {
    const auto end = Clock::now() + milliseconds(ms);
    while (Clock::now() < end) {
      if (EventCount() >= n) return true;
      std::this_thread::sleep_for(milliseconds(5));
    }
    return EventCount() >= n;
  }
  std::size_t EventCount() {
    std::lock_guard<std::mutex> lock{mu_};
    return events_.size();
  }
  int LogCount(LogLevel l, const std::string& needle) {
    std::lock_guard<std::mutex> lock{log_mu_};
    int n = 0;
    for (const auto& [lv, s] : logs_) {
      if (lv == l && s.find(needle) != std::string::npos) ++n;
    }
    return n;
  }
  void WriteKey(const std::string& path, std::uint8_t seed) {
    std::ofstream out{path, std::ios::trunc};
    static const char* hex = "0123456789abcdef";
    for (int i = 0; i < 32; ++i) {
      const std::uint8_t b = static_cast<std::uint8_t>(seed + i);
      out << hex[b >> 4] << hex[b & 15];
    }
  }

  Net net_;
  std::unique_ptr<LinkManager> mgr_a_;
  std::unique_ptr<LinkManager> mgr_b_;
  std::shared_ptr<MavlinkEndpoint> ep_a_;
  std::shared_ptr<MavlinkEndpoint> ep_b_;
  std::mutex mu_;
  std::vector<std::pair<std::uint16_t, std::vector<std::uint8_t>>> events_;
  std::atomic<int> calls_{0};
  std::atomic<int> delay_ms_{0};
  std::atomic<std::uint16_t> last_method_{0};
  std::vector<std::uint8_t> last_payload_;
  std::function<ara::core::Result<std::vector<std::uint8_t>>(
      std::uint16_t, const std::vector<std::uint8_t>&)>
      handler_{[](std::uint16_t, const std::vector<std::uint8_t>& p) {
        return ara::core::Result<std::vector<std::uint8_t>>{
            std::vector<std::uint8_t>{static_cast<std::uint8_t>(p.empty() ? 0 : p[0] + 1)}};
      }};
  std::mutex log_mu_;
  std::vector<std::pair<LogLevel, std::string>> logs_;
  bool signing_{false};
  std::string key_;
  std::optional<std::uint16_t> initial_seq_;
};

// ---------------------------------------------------------------- eventy

TEST_F(ManagerTest, EventIsDeliveredWithIdenticalPayload) {
  AddLink("l1");
  Attach({"l1"});
  const auto payload = PositionPayload(1.5f, -2.5f, 3);
  ASSERT_TRUE(ep_a_->PublishEvent(kPosition, payload).HasValue());
  ASSERT_TRUE(WaitEvents(1));
  EXPECT_EQ(events_[0].first, kPosition);
  EXPECT_EQ(events_[0].second, payload);
  EXPECT_EQ(ep_a_->counters().tx_events, 1U);
  EXPECT_EQ(ep_b_->counters().rx_events, 1U);
}

TEST_F(ManagerTest, ManyEventsKeepOrderAndValues) {
  AddLink("l1");
  Attach({"l1"});
  for (int i = 0; i < 20; ++i) {
    ASSERT_TRUE(ep_a_->PublishEvent(kPosition, PositionPayload(static_cast<float>(i), 0, 1)).HasValue());
    ASSERT_TRUE(WaitEvents(static_cast<std::size_t>(i + 1)));  // bez koalescencji (czekamy na każdy)
  }
  for (int i = 0; i < 20; ++i) {
    EXPECT_EQ(events_[i].second, PositionPayload(static_cast<float>(i), 0, 1));
  }
}

TEST_F(ManagerTest, EventTooShortPayloadIsRejectedWithError) {
  AddLink("l1");
  Attach({"l1"});
  const auto r = ep_a_->PublishEvent(kPosition, std::vector<std::uint8_t>(3, 0));
  EXPECT_FALSE(r.HasValue());
}

TEST_F(ManagerTest, UnknownEventIdIsError) {
  AddLink("l1");
  Attach({"l1"});
  EXPECT_FALSE(ep_a_->PublishEvent(0x8123, {}).HasValue());
}

TEST_F(ManagerTest, DisabledEventIsNoOpWithCounterAndZeroBytesOnWire) {
  AddLink("l1");
  Attach({"l1"});
  const auto bytes_before = net_.a["l1"]->bytes_written();
  for (int i = 0; i < 5; ++i) {
    EXPECT_TRUE(ep_a_->PublishEvent(kStatus, {}).HasValue());
  }
  std::this_thread::sleep_for(milliseconds(150));
  EXPECT_EQ(ep_a_->counters().tx_excluded, 5U);
  EXPECT_EQ(net_.a["l1"]->bytes_written(), bytes_before);  // 0 bajtów na łączu
  EXPECT_EQ(LogCount(LogLevel::kDebug, "wyłączony"), 1);    // jeden log
}

TEST_F(ManagerTest, InstanceExcludeSilencesEvent) {
  AddLink("l1");
  ModelMavlinkOptions o;
  o.exclude = {"Position"};
  Attach({"l1"}, o);
  EXPECT_TRUE(ep_a_->PublishEvent(kPosition, PositionPayload(1, 2, 3)).HasValue());
  std::this_thread::sleep_for(milliseconds(150));
  EXPECT_EQ(ep_a_->counters().tx_excluded, 1U);
  EXPECT_EQ(EventCount(), 0U);
}

TEST_F(ManagerTest, ProxySubscriptionAvailabilityFollowsExclusion) {
  AddLink("l1");
  ModelMavlinkOptions o;
  o.exclude = {"Position"};
  Attach({"l1"}, {}, o);
  EXPECT_FALSE(ep_b_->IsEventAvailable(kPosition));
  EXPECT_FALSE(ep_b_->IsEventAvailable(kStatus));   // wyłączony w service.json
  EXPECT_FALSE(ep_b_->IsEventAvailable(kSetMode));  // metoda, nie event
  EXPECT_FALSE(ep_b_->IsEventAvailable(0x8777));
  EXPECT_TRUE(ep_a_->IsEventAvailable(kPosition));
}

TEST_F(ManagerTest, EventRateLimitFromServiceDefinition) {
  AddLink("l1");
  ModelMavlinkOptions o;
  o.elements = Elements(10.0);  // 10 Hz z service.json
  Attach({"l1"}, o);
  const auto t0 = Clock::now();
  std::uint8_t v = 0;
  while (Clock::now() - t0 < milliseconds(1000)) {
    ASSERT_TRUE(ep_a_->PublishEvent(kPosition, PositionPayload(0, 0, ++v)).HasValue());
    std::this_thread::sleep_for(milliseconds(10));
  }
  std::this_thread::sleep_for(milliseconds(250));
  EXPECT_GE(EventCount(), 8U);   // 10 Hz przez 1 s = 10 +- 20%
  EXPECT_LE(EventCount(), 12U);
}

TEST_F(ManagerTest, InstanceRateOverrideBeatsServiceAndZeroMeansUnlimited) {
  AddLink("l1");
  ModelMavlinkOptions o;
  o.elements = Elements(2.0);       // service.json: 2 Hz
  o.max_rate_hz["Position"] = 0.0;  // nadpisanie: bez limitu
  Attach({"l1"}, o);
  for (int i = 0; i < 30; ++i) {
    ASSERT_TRUE(ep_a_->PublishEvent(kPosition, PositionPayload(0, 0, static_cast<std::uint8_t>(i))).HasValue());
    std::this_thread::sleep_for(milliseconds(12));
  }
  std::this_thread::sleep_for(milliseconds(150));
  EXPECT_GE(EventCount(), 22U);  // gdyby limit 2 Hz obowiązywał, byłoby ~1
}

TEST_F(ManagerTest, InstanceRateOverrideLowerThanService) {
  AddLink("l1");
  ModelMavlinkOptions o;
  o.elements = Elements(50.0);
  o.max_rate_hz["Position"] = 5.0;
  Attach({"l1"}, o);
  const auto t0 = Clock::now();
  while (Clock::now() - t0 < milliseconds(1000)) {
    ASSERT_TRUE(ep_a_->PublishEvent(kPosition, PositionPayload(0, 0, 1)).HasValue());
    std::this_thread::sleep_for(milliseconds(8));
  }
  std::this_thread::sleep_for(milliseconds(250));
  EXPECT_GE(EventCount(), 4U);
  EXPECT_LE(EventCount(), 8U);
}

// ---------------------------------------------------------------- metody

TEST_F(ManagerTest, MethodRoundTrip) {
  AddLink("l1");
  Attach({"l1"});
  const auto r = ep_b_->CallMethod(kSetMode, {5});
  ASSERT_TRUE(r.HasValue());
  EXPECT_EQ(r.Value(), (std::vector<std::uint8_t>{6}));
  EXPECT_EQ(calls_.load(), 1);
  EXPECT_EQ(last_method_, kSetMode);
  EXPECT_EQ(last_payload_, (std::vector<std::uint8_t>{5}));
  EXPECT_EQ(ep_b_->counters().tx_requests, 1U);
  EXPECT_EQ(ep_a_->counters().rx_requests, 1U);
  EXPECT_EQ(ep_a_->counters().tx_responses, 1U);
}

TEST_F(ManagerTest, MethodWithStringParameter) {
  AddLink("l1");
  Attach({"l1"});
  handler_ = [](std::uint16_t, const std::vector<std::uint8_t>& p) {
    const std::uint32_t len = static_cast<std::uint32_t>(p.size());
    std::vector<std::uint8_t> out(4);
    std::memcpy(out.data(), &len, 4);
    return ara::core::Result<std::vector<std::uint8_t>>{out};
  };
  const auto r = ep_b_->CallMethod(kName, std::vector<std::uint8_t>{'a', 'b', 'c'});
  ASSERT_TRUE(r.HasValue());
  std::uint32_t len = 0;
  std::memcpy(&len, r.Value().data(), 4);
  EXPECT_EQ(len, 3U);
  EXPECT_EQ(last_payload_, (std::vector<std::uint8_t>{'a', 'b', 'c'}));
}

TEST_F(ManagerTest, MethodHandlerErrorGivesStatusOneAndCommunicationError) {
  AddLink("l1");
  Attach({"l1"});
  handler_ = [](std::uint16_t, const std::vector<std::uint8_t>&) {
    return ara::core::Result<std::vector<std::uint8_t>>{
        ara::com::MakeErrorCode(ComErrc::kFieldValueIsNotValid, "x")};
  };
  const auto r = ep_b_->CallMethod(kSetMode, {5});
  ASSERT_FALSE(r.HasValue());
  EXPECT_EQ(r.Error().Value(), static_cast<int>(ComErrc::kCommunicationStackError));
}

TEST_F(ManagerTest, MethodHandlerExceptionDoesNotKillSkeleton) {
  AddLink("l1");
  Attach({"l1"});
  handler_ = [](std::uint16_t, const std::vector<std::uint8_t>&)
      -> ara::core::Result<std::vector<std::uint8_t>> { throw std::runtime_error("boom"); };
  EXPECT_FALSE(ep_b_->CallMethod(kSetMode, {5}).HasValue());
  handler_ = [](std::uint16_t, const std::vector<std::uint8_t>&) {
    return ara::core::Result<std::vector<std::uint8_t>>{std::vector<std::uint8_t>{9}};
  };
  const auto r = ep_b_->CallMethod(kSetMode, {5});
  ASSERT_TRUE(r.HasValue());
  EXPECT_EQ(r.Value()[0], 9);
}

TEST_F(ManagerTest, MethodTimeoutWhenHandlerIsTooSlowAndLateResponseIsIgnored) {
  AddLink("l1");
  Attach({"l1"});
  delay_ms_ = 700;  // timeout metody 300 ms
  const auto t0 = Clock::now();
  const auto r = ep_b_->CallMethod(kSetMode, {5});
  const auto ms = std::chrono::duration_cast<milliseconds>(Clock::now() - t0).count();
  ASSERT_FALSE(r.HasValue());
  EXPECT_EQ(r.Error().Value(), static_cast<int>(ComErrc::kPeerIsUnreachable));
  EXPECT_GE(ms, 280);
  EXPECT_LT(ms, 600);
  EXPECT_EQ(ep_b_->counters().call_timeouts, 1U);
  std::this_thread::sleep_for(milliseconds(600));  // spóźniona RESP nie psuje kolejnego wywołania
  delay_ms_ = 0;
  const auto r2 = ep_b_->CallMethod(kSetMode, {7});
  ASSERT_TRUE(r2.HasValue());
  EXPECT_EQ(r2.Value()[0], 8);
}

TEST_F(ManagerTest, FireAndForgetReturnsImmediatelyAndExecutes) {
  AddLink("l1");
  Attach({"l1"});
  delay_ms_ = 200;
  const auto t0 = Clock::now();
  const auto r = ep_b_->CallMethod(kReboot, {});
  const auto ms = std::chrono::duration_cast<milliseconds>(Clock::now() - t0).count();
  ASSERT_TRUE(r.HasValue());
  EXPECT_TRUE(r.Value().empty());
  EXPECT_LT(ms, 100);  // nie czeka na wykonanie
  const auto end = Clock::now() + milliseconds(1500);
  while (calls_.load() == 0 && Clock::now() < end) std::this_thread::sleep_for(milliseconds(5));
  EXPECT_EQ(calls_.load(), 1);
  EXPECT_EQ(last_method_, kReboot);
  EXPECT_EQ(ep_a_->counters().tx_responses, 0U);  // void: bez RESP
}

TEST_F(ManagerTest, MethodExcludedOnProxyFailsImmediatelyWithZeroBytes) {
  AddLink("l1");
  ModelMavlinkOptions o;
  o.exclude = {"SetMode"};
  Attach({"l1"}, {}, o);
  const auto bytes_before = net_.b["l1"]->bytes_written();
  const auto t0 = Clock::now();
  const auto r = ep_b_->CallMethod(kSetMode, {5});
  EXPECT_LT(std::chrono::duration_cast<milliseconds>(Clock::now() - t0).count(), 100);
  ASSERT_FALSE(r.HasValue());
  EXPECT_EQ(r.Error().Value(), static_cast<int>(ComErrc::kServiceNotAvailable));
  EXPECT_EQ(net_.b["l1"]->bytes_written(), bytes_before);
  EXPECT_EQ(ep_b_->counters().call_unavailable, 1U);
  EXPECT_EQ(calls_.load(), 0);
}

TEST_F(ManagerTest, DisabledVoidMethodWarnsAndCountsWithoutSending) {
  AddLink("l1");
  ModelMavlinkOptions o;
  o.exclude = {"Reboot"};
  Attach({"l1"}, {}, o);
  EXPECT_FALSE(ep_b_->CallMethod(kReboot, {}).HasValue());
  EXPECT_EQ(ep_b_->counters().call_unavailable, 1U);
  EXPECT_GE(LogCount(LogLevel::kWarn, "Reboot"), 1);
}

TEST_F(ManagerTest, RequestForMethodExcludedAtSkeletonGetsStatusTwoAndHandlerNotCalled) {
  AddLink("l1");
  ModelMavlinkOptions skel;
  skel.exclude = {"SetMode"};  // skeleton wyklucza, proxy o tym nie wie
  Attach({"l1"}, skel);
  const auto t0 = Clock::now();
  const auto r = ep_b_->CallMethod(kSetMode, {5});
  EXPECT_LT(std::chrono::duration_cast<milliseconds>(Clock::now() - t0).count(), 250);  // bez timeoutu
  ASSERT_FALSE(r.HasValue());
  EXPECT_EQ(r.Error().Value(), static_cast<int>(ComErrc::kServiceNotAvailable));
  EXPECT_EQ(calls_.load(), 0);
  EXPECT_EQ(ep_a_->counters().rx_rejected, 1U);
}

TEST_F(ManagerTest, ExcludedVoidRequestAtSkeletonIsRejectedSilently) {
  AddLink("l1");
  ModelMavlinkOptions skel;
  skel.exclude = {"Reboot"};
  Attach({"l1"}, skel);
  EXPECT_TRUE(ep_b_->CallMethod(kReboot, {}).HasValue());
  std::this_thread::sleep_for(milliseconds(200));
  EXPECT_EQ(calls_.load(), 0);
  EXPECT_EQ(ep_a_->counters().rx_rejected, 1U);
  EXPECT_EQ(ep_a_->counters().tx_responses, 0U);
}

TEST_F(ManagerTest, UnknownInstanceGetsStatusTwoResponse) {
  AddLink("l1");
  Attach({"l1"}, {}, {}, /*inst_a=*/3, /*inst_b=*/9);
  const auto t0 = Clock::now();
  const auto r = ep_b_->CallMethod(kSetMode, {5});
  EXPECT_LT(std::chrono::duration_cast<milliseconds>(Clock::now() - t0).count(), 250);
  ASSERT_FALSE(r.HasValue());
  EXPECT_EQ(r.Error().Value(), static_cast<int>(ComErrc::kServiceNotAvailable));
  EXPECT_EQ(calls_.load(), 0);
}

TEST_F(ManagerTest, EventFromOtherInstanceIsNotDelivered) {
  AddLink("l1");
  Attach({"l1"}, {}, {}, 3, 9);
  ASSERT_TRUE(ep_a_->PublishEvent(kPosition, PositionPayload(1, 2, 3)).HasValue());
  std::this_thread::sleep_for(milliseconds(200));
  EXPECT_EQ(EventCount(), 0U);
  EXPECT_GE(ep_b_->counters().rx_rejected, 1U);
}

TEST_F(ManagerTest, NoReadyLinkFailsMethodImmediately) {
  AddLink("l1");
  Attach({"l1"});
  net_.b["l1"]->FailNextOpens(1000);
  net_.b["l1"]->Close();  // proxy traci łącze
  const auto end = Clock::now() + milliseconds(1000);
  while (ep_b_->AnyLinkReady() && Clock::now() < end) std::this_thread::sleep_for(milliseconds(5));
  ASSERT_FALSE(ep_b_->AnyLinkReady());
  const auto t0 = Clock::now();
  const auto r = ep_b_->CallMethod(kSetMode, {1});
  EXPECT_LT(std::chrono::duration_cast<milliseconds>(Clock::now() - t0).count(), 100);
  ASSERT_FALSE(r.HasValue());
  EXPECT_EQ(r.Error().Value(), static_cast<int>(ComErrc::kCommunicationLinkError));
}

TEST_F(ManagerTest, TargetFilterIgnoresRequestForOtherSystem) {
  AddLink("l1");
  ep_a_ = mgr_a_->AttachEndpoint(Model(3, 1, 1, {"l1"}, {}), true);
  ep_b_ = mgr_b_->AttachEndpoint(Model(3, 2, 1, {"l1"}, {}, /*target_sys=*/9, 1), false);
  ep_a_->SetMethodHandler([this](std::uint16_t, const std::vector<std::uint8_t>&) {
    calls_++;
    return ara::core::Result<std::vector<std::uint8_t>>{std::vector<std::uint8_t>{1}};
  });
  WaitReady();
  const auto r = ep_b_->CallMethod(kSetMode, {5});
  EXPECT_FALSE(r.HasValue());  // adresowane do sysid=9, skeleton ma 1 -> ignorowane -> timeout
  EXPECT_EQ(calls_.load(), 0);
}

TEST_F(ManagerTest, BroadcastTargetZeroIsAccepted) {
  AddLink("l1");
  Attach({"l1"});  // proxy: target 1/1; sprawdzamy też target 0 poniżej
  ASSERT_TRUE(ep_b_->CallMethod(kSetMode, {5}).HasValue());
  mgr_b_->DetachEndpoint(ep_b_);
  ep_b_ = mgr_b_->AttachEndpoint(Model(3, 2, 1, {"l1"}, {}, 0, 0), false);
  WaitReady();
  EXPECT_TRUE(ep_b_->CallMethod(kSetMode, {6}).HasValue());
}

TEST_F(ManagerTest, SourceFilterOnProxyDropsOtherSenders) {
  AddLink("l1");
  ModelMavlinkOptions prox;
  prox.source_system_id = 7;  // skeleton ma sysid 1
  Attach({"l1"}, {}, prox);
  ASSERT_TRUE(ep_a_->PublishEvent(kPosition, PositionPayload(1, 2, 3)).HasValue());
  std::this_thread::sleep_for(milliseconds(200));
  EXPECT_EQ(EventCount(), 0U);
}

TEST_F(ManagerTest, SourceFilterMatchingSenderPasses) {
  AddLink("l1");
  ModelMavlinkOptions prox;
  prox.source_system_id = 1;
  prox.source_component_id = 1;
  Attach({"l1"}, {}, prox);
  ASSERT_TRUE(ep_a_->PublishEvent(kPosition, PositionPayload(1, 2, 3)).HasValue());
  EXPECT_TRUE(WaitEvents(1));
}

// ---------------------------------------------------------------- redundancja

TEST_F(ManagerTest, TwoLinksDeliverEventExactlyOnce) {
  AddLink("l1");
  AddLink("l2");
  Attach({"l1", "l2"});
  for (int i = 0; i < 10; ++i) {
    ASSERT_TRUE(ep_a_->PublishEvent(kPosition, PositionPayload(static_cast<float>(i), 0, 1)).HasValue());
    ASSERT_TRUE(WaitEvents(static_cast<std::size_t>(i + 1)));
  }
  std::this_thread::sleep_for(milliseconds(200));
  EXPECT_EQ(EventCount(), 10U);
  EXPECT_EQ(ep_b_->counters().rx_duplicate, 10U);  // druga kopia każdego eventu
}

TEST_F(ManagerTest, TwoLinksExecuteRequestOnceAndFirstResponseWins) {
  AddLink("l1");
  AddLink("l2");
  Attach({"l1", "l2"});
  for (int i = 0; i < 5; ++i) {
    const auto r = ep_b_->CallMethod(kSetMode, {static_cast<std::uint8_t>(i)});
    ASSERT_TRUE(r.HasValue());
    EXPECT_EQ(r.Value()[0], i + 1);
  }
  std::this_thread::sleep_for(milliseconds(200));
  EXPECT_EQ(calls_.load(), 5);  // 5 wykonań mimo 10 kopii REQ
  EXPECT_EQ(ep_a_->counters().rx_duplicate, 5U);
}

TEST_F(ManagerTest, OneLinkDelayedStillExecutesOnce) {
  AddLink("l1");
  AddLink("l2");
  net_.b["l2"]->SetDeliveryDelayMs(400);  // kopia REQ po drugim łączu spóźniona
  Attach({"l1", "l2"});
  ASSERT_TRUE(ep_b_->CallMethod(kSetMode, {1}).HasValue());
  std::this_thread::sleep_for(milliseconds(700));
  EXPECT_EQ(calls_.load(), 1);
}

TEST_F(ManagerTest, OneLinkDownTrafficContinuesOnTheOther) {
  AddLink("l1");
  AddLink("l2");
  Attach({"l1", "l2"});
  net_.a["l1"]->SetFailIo(true);
  net_.b["l1"]->SetFailIo(true);
  for (int i = 0; i < 5; ++i) {
    ASSERT_TRUE(ep_a_->PublishEvent(kPosition, PositionPayload(static_cast<float>(i), 0, 1)).HasValue());
    ASSERT_TRUE(WaitEvents(static_cast<std::size_t>(i + 1)));
  }
  EXPECT_TRUE(ep_b_->CallMethod(kSetMode, {3}).HasValue());
}

TEST_F(ManagerTest, ResponseAlsoRedundantButProxyTakesFirst) {
  AddLink("l1");
  AddLink("l2");
  Attach({"l1", "l2"});
  ASSERT_TRUE(ep_b_->CallMethod(kSetMode, {1}).HasValue());
  std::this_thread::sleep_for(milliseconds(200));
  EXPECT_EQ(ep_a_->counters().tx_responses, 1U);  // jedna RESP (seq REQ) na 2 łącza
  EXPECT_LE(ep_b_->counters().rx_responses, 1U);
}

TEST_F(ManagerTest, LinkDelayedThreeSecondsStillYieldsSingleExecutionAndDelivery) {
  AddLink("l1");
  AddLink("l2");
  net_.b["l2"]->SetDeliveryDelayMs(3000);  // R2: jedno łącze opóźnione o 3 s
  net_.a["l2"]->SetDeliveryDelayMs(3000);
  Attach({"l1", "l2"});
  ASSERT_TRUE(ep_a_->PublishEvent(kPosition, PositionPayload(1, 2, 3)).HasValue());
  ASSERT_TRUE(WaitEvents(1));
  ASSERT_TRUE(ep_b_->CallMethod(kSetMode, {1}).HasValue());
  std::this_thread::sleep_for(milliseconds(3600));  // spóźnione kopie dochodzą w oknie TTL=10 s
  EXPECT_EQ(EventCount(), 1U);
  EXPECT_EQ(calls_.load(), 1);
  EXPECT_GE(ep_b_->counters().rx_duplicate, 1U);
  EXPECT_GE(ep_a_->counters().rx_duplicate, 1U);
}

TEST_F(ManagerTest, SenderRestartInsideDedupWindowLosesNoFramesWithDifferentContent) {
  AddLink("l1");
  Attach({"l1"});
  ASSERT_TRUE(ep_a_->PublishEvent(kPosition, PositionPayload(1, 1, 1)).HasValue());
  ASSERT_TRUE(WaitEvents(1));
  // restart nadawcy po ~1 s: nowy endpoint = nowy losowy start seq, TTL 10 s jeszcze trwa
  std::this_thread::sleep_for(milliseconds(1000));
  mgr_a_->DetachEndpoint(ep_a_);
  ep_a_ = mgr_a_->AttachEndpoint(Model(3, 1, 1, {"l1"}, {}), true);
  const auto end = Clock::now() + milliseconds(2000);
  while (!ep_a_->AnyLinkReady() && Clock::now() < end) std::this_thread::sleep_for(milliseconds(5));
  ASSERT_TRUE(ep_a_->PublishEvent(kPosition, PositionPayload(2, 2, 2)).HasValue());
  ASSERT_TRUE(ep_a_->PublishEvent(kPosition, PositionPayload(3, 3, 3)).HasValue());
  EXPECT_TRUE(WaitEvents(2));  // treść inna => dochodzi mimo możliwej kolizji seq
  EXPECT_EQ(events_.back().second, PositionPayload(3, 3, 3));
}

TEST_F(ManagerTest, SenderRestartWithForcedSameSeqAndSamePayloadIsDuplicateDifferentPayloadIsNot) {
  // hak testowy test_initial_seq: po restarcie nadawcy ten sam seq (T2 #2)
  initial_seq_ = 100;
  mgr_a_->Shutdown();
  AddLink("l1");
  mgr_a_ = MakeManager(&net_.a);
  Attach({"l1"});
  ASSERT_TRUE(ep_a_->PublishEvent(kPosition, PositionPayload(1, 1, 1)).HasValue());
  ASSERT_TRUE(WaitEvents(1));
  mgr_a_->DetachEndpoint(ep_a_);
  mgr_a_->Shutdown();
  mgr_a_ = MakeManager(&net_.a);  // "restart": seq znowu 100
  ep_a_ = mgr_a_->AttachEndpoint(Model(3, 1, 1, {"l1"}, {}), true);
  const auto end = Clock::now() + milliseconds(2000);
  while (!ep_a_->AnyLinkReady() && Clock::now() < end) std::this_thread::sleep_for(milliseconds(5));
  ASSERT_TRUE(ep_a_->PublishEvent(kPosition, PositionPayload(1, 1, 1)).HasValue());  // seq+payload takie same
  std::this_thread::sleep_for(milliseconds(300));
  EXPECT_EQ(EventCount(), 1U);  // duplikat (R2: seq + CRC32 payloadu)
  std::this_thread::sleep_for(milliseconds(50));
  ASSERT_TRUE(ep_a_->PublishEvent(kPosition, PositionPayload(9, 9, 9)).HasValue());  // seq 101, inny payload
  EXPECT_TRUE(WaitEvents(2));
}

// ---------------------------------------------------------------- instancje / łącza

TEST_F(ManagerTest, TwoInstancesOfOneServiceOnDifferentLinksAreIndependent) {
  AddLink("lora");
  AddLink("wifi");
  ModelMavlinkOptions lora;
  lora.exclude = {"Position"};
  ModelMavlinkOptions wifi;
  wifi.exclude = {"SetMode"};
  auto a1 = mgr_a_->AttachEndpoint(Model(3, 1, 1, {"lora"}, lora), true);
  auto a2 = mgr_a_->AttachEndpoint(Model(4, 1, 1, {"wifi"}, wifi), true);
  auto b1 = mgr_b_->AttachEndpoint(Model(3, 2, 1, {"lora"}, {}, 1, 1), false);
  auto b2 = mgr_b_->AttachEndpoint(Model(4, 2, 1, {"wifi"}, {}, 1, 1), false);
  std::atomic<int> ev1{0}, ev2{0};
  b1->SetEventHandler([&](std::uint16_t, const std::vector<std::uint8_t>&) { ev1++; });
  b2->SetEventHandler([&](std::uint16_t, const std::vector<std::uint8_t>&) { ev2++; });
  std::atomic<int> m1{0}, m2{0};
  a1->SetMethodHandler([&](std::uint16_t, const std::vector<std::uint8_t>&) {
    m1++;
    return ara::core::Result<std::vector<std::uint8_t>>{std::vector<std::uint8_t>{1}};
  });
  a2->SetMethodHandler([&](std::uint16_t, const std::vector<std::uint8_t>&) {
    m2++;
    return ara::core::Result<std::vector<std::uint8_t>>{std::vector<std::uint8_t>{2}};
  });
  const auto end = Clock::now() + milliseconds(2000);
  while (Clock::now() < end && !(a1->AnyLinkReady() && a2->AnyLinkReady() && b1->AnyLinkReady() &&
                                 b2->AnyLinkReady())) {
    std::this_thread::sleep_for(milliseconds(5));
  }
  EXPECT_TRUE(a1->PublishEvent(kPosition, PositionPayload(1, 1, 1)).HasValue());  // lora: wyłączony
  EXPECT_TRUE(a2->PublishEvent(kPosition, PositionPayload(2, 2, 2)).HasValue());  // wifi: OK
  std::this_thread::sleep_for(milliseconds(250));
  EXPECT_EQ(ev1.load(), 0);
  EXPECT_EQ(ev2.load(), 1);
  EXPECT_TRUE(b1->CallMethod(kSetMode, {1}).HasValue());   // lora: SetMode dostępny
  EXPECT_FALSE(b2->CallMethod(kSetMode, {1}).HasValue());  // wifi: wyklucz.
  EXPECT_EQ(m1.load(), 1);
  EXPECT_EQ(m2.load(), 0);
  mgr_a_->DetachEndpoint(a1);
  mgr_a_->DetachEndpoint(a2);
  mgr_b_->DetachEndpoint(b1);
  mgr_b_->DetachEndpoint(b2);
}

TEST_F(ManagerTest, LinkIsSharedBetweenEndpointsAndStoppedWithLastOne) {
  AddLink("l1");
  auto e1 = mgr_a_->AttachEndpoint(Model(3, 1, 1, {"l1"}, {}), true);
  auto e2 = mgr_a_->AttachEndpoint(Model(4, 1, 1, {"l1"}, {}), true);
  ASSERT_NE(mgr_a_->FindLink("l1"), nullptr);
  EXPECT_EQ(e1->links()[0], e2->links()[0]);
  mgr_a_->DetachEndpoint(e1);
  EXPECT_NE(mgr_a_->FindLink("l1"), nullptr);
  mgr_a_->DetachEndpoint(e2);
  EXPECT_EQ(mgr_a_->FindLink("l1"), nullptr);
}

TEST_F(ManagerTest, SequenceNumbersAdvancePerElementAndInstance) {
  AddLink("l1");
  ModelMavlinkOptions o;
  Attach({"l1"}, o);
  // dwa kolejne eventy muszą mieć kolejne seq (mod 2^16) — widać po dedup: oba dochodzą
  ASSERT_TRUE(ep_a_->PublishEvent(kPosition, PositionPayload(1, 0, 0)).HasValue());
  ASSERT_TRUE(WaitEvents(1));
  ASSERT_TRUE(ep_a_->PublishEvent(kPosition, PositionPayload(1, 0, 0)).HasValue());  // ta sama treść
  EXPECT_TRUE(WaitEvents(2));  // inny seq => nie duplikat mimo identycznego payloadu
}

// ---------------------------------------------------------------- podpis

TEST_F(ManagerTest, SignedLinksWorkEndToEnd) {
  const std::string key = "/tmp/mavlink_mgr_key_ok";
  WriteKey(key, 3);
  signing_ = true;
  key_ = key;
  AddLink("l1");
  Attach({"l1"});
  ASSERT_TRUE(ep_a_->PublishEvent(kPosition, PositionPayload(1, 2, 3)).HasValue());
  ASSERT_TRUE(WaitEvents(1));
  EXPECT_TRUE(ep_b_->CallMethod(kSetMode, {1}).HasValue());
  std::remove(key.c_str());
}

TEST_F(ManagerTest, EventsFromTwoLinksNeverRunTheProxyHandlerConcurrently) {
  AddLink("l1");
  AddLink("l2");
  ep_a_ = mgr_a_->AttachEndpoint(Model(3, 1, 1, {"l1", "l2"}, {}), true);
  ep_b_ = mgr_b_->AttachEndpoint(Model(3, 2, 1, {"l1", "l2"}, {}, 1, 1), false);
  std::atomic<int> inside{0};
  std::atomic<bool> overlap{false};
  std::atomic<int> delivered{0};
  ep_b_->SetEventHandler([&](std::uint16_t, const std::vector<std::uint8_t>&) {
    if (inside++ != 0) overlap = true;
    std::this_thread::sleep_for(milliseconds(1));
    inside--;
    delivered++;
  });
  WaitReady();
  for (int i = 0; i < 60; ++i) {
    ASSERT_TRUE(ep_a_->PublishEvent(kPosition, PositionPayload(i, i + 1, i + 2)).HasValue());
    std::this_thread::sleep_for(milliseconds(4));  // koalescencja zjada eventy publikowane szybciej
  }
  std::this_thread::sleep_for(milliseconds(300));
  EXPECT_GE(delivered.load(), 30);
  EXPECT_FALSE(overlap.load());
}

TEST_F(ManagerTest, ProxyHandlerIsNeverCalledAfterDetachReturns) {
  AddLink("l1");
  AddLink("l2");
  ep_a_ = mgr_a_->AttachEndpoint(Model(3, 1, 1, {"l1", "l2"}, {}), true);
  ep_b_ = mgr_b_->AttachEndpoint(Model(3, 2, 1, {"l1", "l2"}, {}, 1, 1), false);
  std::atomic<bool> gone{false};
  std::atomic<bool> late{false};
  std::atomic<int> delivered{0};
  ep_b_->SetEventHandler([&](std::uint16_t, const std::vector<std::uint8_t>&) {
    if (gone.load()) late = true;
    std::this_thread::sleep_for(milliseconds(2));
    if (gone.load()) late = true;  // handler w toku: Detach musi na niego poczekac
    delivered++;
  });
  WaitReady();
  std::atomic<bool> stop{false};
  std::thread pub{[&] {
    for (int i = 0; !stop.load() && i < 100000; ++i) {
      std::ignore = ep_a_->PublishEvent(kPosition, PositionPayload(i, i, i));
      std::this_thread::sleep_for(std::chrono::microseconds(300));
    }
  }};
  const auto end = Clock::now() + milliseconds(2000);
  while (delivered.load() < 5 && Clock::now() < end) std::this_thread::sleep_for(milliseconds(2));
  mgr_b_->DetachEndpoint(ep_b_);
  gone = true;
  std::this_thread::sleep_for(milliseconds(150));  // ramki w locie dojda do RX
  stop = true;
  pub.join();
  EXPECT_FALSE(late.load());
  EXPECT_GE(delivered.load(), 5);
}

TEST_F(ManagerTest, DetachSkeletonWhileRequestsAndResponsesAreInFlight) {
  AddLink("l1");
  AddLink("l2");
  Attach({"l1", "l2"});
  std::atomic<bool> stop{false};
  std::thread caller{[&] {
    while (!stop.load()) {
      std::ignore = ep_b_->CallMethod(kSetMode, {1});
    }
  }};
  std::this_thread::sleep_for(milliseconds(150));
  mgr_a_->DetachEndpoint(ep_a_);  // worker + RX moga w tym czasie wywolywac SendResponse
  ep_a_.reset();
  std::this_thread::sleep_for(milliseconds(100));
  stop = true;
  caller.join();
  SUCCEED();  // TSAN/ASAN: brak wyscigu na links_ i use-after-free
}

TEST_F(ManagerTest, FrameFromLinkOutsideTheInstanceIsRejected) {
  // Instancja 3 = lora (podpisana), instancja 4 = wifi (bez podpisu). REQ z instance=3
  // przyslany przez wifi nie moze zostac wykonany przez instancje 3 (QA M2a-2 #1).
  const std::string key = "/tmp/mavlink_mgr_key_inst";
  WriteKey(key, 5);
  AddLink("lora");
  AddLink("wifi");
  auto opts = [] {
    ModelMavlinkOptions o;
    o.elements = Elements();
    return o;
  };
  auto a1 = mgr_a_->AttachEndpoint(
      ModelMavlink::CreatSkeletonItem("a1", 3, 1, 0, 1, 1, 0, 0, "t", {Link("lora", true, key)}, opts())
          .Value(),
      true);
  auto a2 = mgr_a_->AttachEndpoint(
      ModelMavlink::CreatSkeletonItem("a2", 4, 1, 0, 1, 1, 0, 0, "t", {Link("wifi", false, "")}, opts())
          .Value(),
      true);
  // proxy: instancja 3 widzi tylko wifi (bez podpisu) - symulacja napastnika/zlej konfiguracji
  auto b = mgr_b_->AttachEndpoint(
      ModelMavlink::CreatProxyItem("b", 3, 1, 0, 2, 1, 1, 1, "t", {Link("wifi", false, "")}, opts())
          .Value(),
      false);
  std::atomic<int> m3{0}, m4{0};
  a1->SetMethodHandler([&](std::uint16_t, const std::vector<std::uint8_t>&) {
    m3++;
    return ara::core::Result<std::vector<std::uint8_t>>{std::vector<std::uint8_t>{1}};
  });
  a2->SetMethodHandler([&](std::uint16_t, const std::vector<std::uint8_t>&) {
    m4++;
    return ara::core::Result<std::vector<std::uint8_t>>{std::vector<std::uint8_t>{1}};
  });
  const auto end = Clock::now() + milliseconds(2000);
  while (Clock::now() < end && !(a2->AnyLinkReady() && b->AnyLinkReady())) {
    std::this_thread::sleep_for(milliseconds(5));
  }
  EXPECT_FALSE(b->CallMethod(kSetMode, {1}).HasValue());
  std::this_thread::sleep_for(milliseconds(100));
  EXPECT_EQ(m3.load(), 0);
  EXPECT_EQ(m4.load(), 0);
  EXPECT_GE(a1->counters().rx_rejected, 1U);
  mgr_a_->DetachEndpoint(a1);
  mgr_a_->DetachEndpoint(a2);
  mgr_b_->DetachEndpoint(b);
  std::remove(key.c_str());
}

TEST_F(ManagerTest, MismatchedKeysDropEverything) {
  const std::string ka = "/tmp/mavlink_mgr_key_a";
  const std::string kb = "/tmp/mavlink_mgr_key_b";
  WriteKey(ka, 3);
  WriteKey(kb, 77);
  AddLink("l1");
  ModelMavlink ma = ModelMavlink::CreatSkeletonItem("ca", 3, 1, 0, 1, 1, 0, 0, "t",
                                                    {Link("l1", true, ka)}, [] {
                                                      ModelMavlinkOptions o;
                                                      o.elements = Elements();
                                                      return o;
                                                    }()).Value();
  ModelMavlink mb = ModelMavlink::CreatProxyItem("cb", 3, 1, 0, 2, 1, 1, 1, "t",
                                                 {Link("l1", true, kb)}, [] {
                                                   ModelMavlinkOptions o;
                                                   o.elements = Elements();
                                                   return o;
                                                 }()).Value();
  ep_a_ = mgr_a_->AttachEndpoint(ma, true);
  ep_b_ = mgr_b_->AttachEndpoint(mb, false);
  ep_b_->SetEventHandler([this](std::uint16_t id, const std::vector<std::uint8_t>& p) {
    std::lock_guard<std::mutex> lock{mu_};
    events_.emplace_back(id, p);
  });
  WaitReady();
  ASSERT_TRUE(ep_a_->PublishEvent(kPosition, PositionPayload(1, 2, 3)).HasValue());
  std::this_thread::sleep_for(milliseconds(250));
  EXPECT_EQ(EventCount(), 0U);
  EXPECT_GE(mgr_b_->FindLink("l1")->counters().rx_sig_err, 1U);
  std::remove(ka.c_str());
  std::remove(kb.c_str());
}

// ---------------------------------------------------------------- inne

TEST_F(ManagerTest, UnknownMessageIdsAreCounted) {
  AddLink("l1");
  Attach({"l1"});
  // nadawca (testowy) wysyła ramkę o msgid spoza tablicy - link ją porzuca (CRC nieweryfikowalne)
  const auto frame = BuildFrame(2, 1, 5, 5, 999, std::vector<std::uint8_t>{1}, 1);
  ASSERT_TRUE(net_.b["l1"]->Write(std::span<const std::uint8_t>{frame}, 100, {}).HasValue());
  std::this_thread::sleep_for(milliseconds(150));
  EXPECT_GE(mgr_a_->FindLink("l1")->counters().rx_dropped, 1U);
}

TEST_F(ManagerTest, HeartbeatsFlowWhenEnabledAndDoNotReachEndpoints) {
  AddLink("l1");
  // HEARTBEAT włączony na obu łączach
  auto la = Link("l1");
  la.heartbeat_hz = 20;
  ModelMavlinkOptions o;
  o.elements = Elements();
  ep_a_ = mgr_a_->AttachEndpoint(
      ModelMavlink::CreatSkeletonItem("ca", 3, 1, 0, 1, 1, 0, 0, "t", {la}, o).Value(), true);
  ep_b_ = mgr_b_->AttachEndpoint(
      ModelMavlink::CreatProxyItem("cb", 3, 1, 0, 2, 1, 1, 1, "t", {la}, o).Value(), false);
  WaitReady();
  std::this_thread::sleep_for(milliseconds(400));
  EXPECT_GE(mgr_a_->FindLink("l1")->counters().heartbeats_rx, 3U);
  EXPECT_GE(mgr_b_->FindLink("l1")->counters().heartbeats_rx, 3U);
}

TEST_F(ManagerTest, ShutdownStopsEverythingPromptly) {
  AddLink("l1");
  Attach({"l1"});
  const auto t0 = Clock::now();
  mgr_a_->DetachEndpoint(ep_a_);
  ep_a_.reset();
  mgr_b_->DetachEndpoint(ep_b_);
  ep_b_.reset();
  mgr_a_->Shutdown();
  mgr_b_->Shutdown();
  EXPECT_LT(std::chrono::duration_cast<milliseconds>(Clock::now() - t0).count(), 2000);
}

}  // namespace
}  // namespace mavlink
}  // namespace com
}  // namespace ara
