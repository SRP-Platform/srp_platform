/**
 * Copyright (c) 2024
 *
 * @file test_mavlink_bindings.cc
 * @brief Bindingi MAVLink za IBindings oraz ścieżka ServiceSkeleton/ServiceHandler
 * (StartOffer/StartFind) na modelu ModelMavlink.
 */
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>  // NOLINT
#include <cstring>
#include <map>
#include <memory>
#include <mutex>  // NOLINT
#include <thread>  // NOLINT
#include <vector>

#include "ara/com/com_error_domain.h"
#include "ara/com/mavlink/link_manager.h"
#include "ara/com/mavlink/link_transport_registry.h"
#include "ara/com/mavlink/testing/memory_transport.h"
#include "ara/com/someip/bindings/binding_factory.h"
#include "ara/com/someip/bindings/mavlink/mavlink_bindings.h"
#include "ara/com/someip/service_skeleton.h"
#include "ara/com/someip/someip_handler.h"
#include "ara/core/model_db.h"
#include "ara/log/logging_menager.h"

namespace ara {
namespace com {
namespace someip {
namespace bindings {
namespace {
using ara::com::mavlink::LinkManager;
using ara::core::model::ModelMavlink;
using ara::core::model::ModelMavlinkElement;
using ara::core::model::ModelMavlinkField;
using ara::core::model::ModelMavlinkLink;
using ara::core::model::ModelMavlinkMessage;
using ara::core::model::ModelMavlinkOptions;
using ara::com::mavlink::testing::MemoryTransport;
using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;
using Type = ModelMavlinkField::Type;
using Role = ModelMavlinkField::Role;

ModelMavlinkField F(const std::string& n, Type t, Role r = Role::kPayload) {
  ModelMavlinkField f;
  f.name = n;
  f.type = t;
  f.role = r;
  return f;
}

ModelMavlinkMessage Msg(std::uint32_t id, const std::string& name, std::vector<ModelMavlinkField> f) {
  ModelMavlinkMessage m;
  m.msg_id = id;
  m.name = name;
  m.fields = std::move(f);
  m.crc_extra = ara::com::mavlink::ComputeCrcExtra(m);
  return m;
}

std::vector<ModelMavlinkElement> Elements() {
  std::vector<ModelMavlinkElement> out;
  ModelMavlinkElement ev;
  ev.id = 0x8001;
  ev.is_event = true;
  ev.enabled = true;
  ev.name = "Temp";
  ev.req = Msg(300, "B_TEMP", {F("seq", Type::kU16, Role::kSeq), F("instance", Type::kU16, Role::kInstance),
                               F("value", Type::kI16)});
  out.push_back(ev);
  ModelMavlinkElement off;
  off.id = 0x8002;
  off.is_event = true;
  off.enabled = false;
  off.name = "Debug";
  out.push_back(off);
  ModelMavlinkElement m;
  m.id = 1;
  m.enabled = true;
  m.name = "Add";
  m.req = Msg(310, "B_ADD_REQ", {F("seq", Type::kU16, Role::kSeq), F("instance", Type::kU16, Role::kInstance),
                                 F("target_system", Type::kU8, Role::kTargetSystem),
                                 F("target_component", Type::kU8, Role::kTargetComponent),
                                 F("value", Type::kU8)});
  m.has_resp = true;
  m.resp = Msg(311, "B_ADD_RESP", {F("seq", Type::kU16, Role::kSeq), F("instance", Type::kU16, Role::kInstance),
                                   F("target_system", Type::kU8, Role::kTargetSystem),
                                   F("target_component", Type::kU8, Role::kTargetComponent),
                                   F("status", Type::kU8, Role::kStatus), F("value", Type::kU8)});
  m.timeout_ms = 300;
  out.push_back(m);
  return out;
}

ModelMavlinkLink Link(const std::string& name) {
  ModelMavlinkLink l;
  l.name = name;
  l.heartbeat_hz = 0;
  return l;
}

ModelMavlink Model(bool skeleton, std::uint16_t instance, const std::string& link,
                   ModelMavlinkOptions opts = {}) {
  if (opts.elements.empty()) opts.elements = Elements();
  const auto sys = skeleton ? 1 : 2;
  const auto fn = skeleton ? &ModelMavlink::CreatSkeletonItem : &ModelMavlink::CreatProxyItem;
  return fn("c" + link, instance, 1, 0, sys, 1, skeleton ? 0 : 1, skeleton ? 0 : 1, "bnd", {Link(link)}, opts)
      .Value();
}

LinkManager::Options FastOptions(std::map<std::string, std::shared_ptr<MemoryTransport>>* m) {
  LinkManager::Options o;
  o.transport_factory = [m](const ModelMavlinkLink& l) {
    std::shared_ptr<MemoryTransport> t = m->at(l.name);
    return ara::com::mavlink::MavlinkLink::TransportProvider{
        [t](milliseconds, std::stop_token) { return std::shared_ptr<ara::com::mavlink::IByteTransport>{t}; }};
  };
  o.retry_interval = milliseconds(40);
  o.read_timeout_ms = 20;
  o.write_timeout_ms = 100;
  o.now = [] { return ara::com::mavlink::kClockSanityThreshold + 1; };
  return o;
}

bool WaitFor(const std::function<bool()>& pred, int ms = 2000) {
  const auto end = Clock::now() + milliseconds(ms);
  while (Clock::now() < end) {
    if (pred()) return true;
    std::this_thread::sleep_for(milliseconds(5));
  }
  return pred();
}

// ---------------------------------------------------------------- widok modelu / fabryka

TEST(MavlinkModelView, MapsFieldsAndDirection) {
  const auto skel = Model(true, 7, "v1");
  const auto prox = Model(false, 8, "v2");
  const auto a = MavlinkModelView(skel);
  const auto b = MavlinkModelView(prox);
  EXPECT_EQ(a.instance_, 7);
  EXPECT_EQ(a.dir_, ara::core::model::ModelSomeIp::kOut);
  EXPECT_EQ(a.port_, 0U);
  EXPECT_EQ(b.instance_, 8);
  EXPECT_EQ(b.dir_, ara::core::model::ModelSomeIp::kIn);
  EXPECT_EQ(a.major_version_, 1);
}

TEST(MavlinkBindingsFactory, ChoosesByDirectionNotByPort) {
  const auto skel = BindingsFactory::GetBindings(Model(true, 1, "f1"));
  const auto prox = BindingsFactory::GetBindings(Model(false, 1, "f2"));
  ASSERT_NE(skel, nullptr);
  ASSERT_NE(prox, nullptr);
  EXPECT_NE(dynamic_cast<MavlinkSkeletonBindings*>(skel.get()), nullptr);
  EXPECT_NE(dynamic_cast<MavlinkProxyBindings*>(prox.get()), nullptr);
}

// ---------------------------------------------------------------- bindingi przez IBindings

class BindingsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto pair = MemoryTransport::MakePair("sk", "px", 64 * 1024);
    maps_a_["lk"] = pair.first;
    maps_b_["lk"] = pair.second;
    mgr_a_ = std::make_unique<LinkManager>(FastOptions(&maps_a_));
    mgr_b_ = std::make_unique<LinkManager>(FastOptions(&maps_b_));
  }
  void TearDown() override {
    if (prox_) prox_->Stop();
    if (skel_) skel_->Stop();
    mgr_a_->Shutdown();
    mgr_b_->Shutdown();
  }

  void StartBoth(ModelMavlinkOptions skel_opts = {}, ModelMavlinkOptions prox_opts = {}) {
    skel_ = std::make_shared<MavlinkSkeletonBindings>(Model(true, 5, "lk", skel_opts), mgr_a_.get());
    prox_ = std::make_shared<MavlinkProxyBindings>(Model(false, 5, "lk", prox_opts), mgr_b_.get());
    skel_->SetSkeletonCallback([this](const uint16_t& id, const std::vector<uint8_t>& p) {
      calls_++;
      last_method_ = id;
      return ara::core::Result<std::vector<uint8_t>>{
          std::vector<uint8_t>{static_cast<uint8_t>(p.empty() ? 0 : p[0] + 10)}};
    });
    prox_->SetProxyCallback(
        [this](const uint16_t id, const std::vector<uint8_t>& p) {
          std::lock_guard<std::mutex> lock{mu_};
          events_.emplace_back(id, p);
        },
        [this](const uint16_t id, const uint8_t status) {
          std::lock_guard<std::mutex> lock{mu_};
          statuses_[id] = status;
        });
    skel_->Start({});
    prox_->Start({});
    ASSERT_TRUE(WaitFor([&] {
      return skel_->endpoint()->AnyLinkReady() && prox_->endpoint()->AnyLinkReady();
    }));
  }
  std::size_t EventCount() {
    std::lock_guard<std::mutex> lock{mu_};
    return events_.size();
  }

  std::map<std::string, std::shared_ptr<MemoryTransport>> maps_a_;
  std::map<std::string, std::shared_ptr<MemoryTransport>> maps_b_;
  std::unique_ptr<LinkManager> mgr_a_;
  std::unique_ptr<LinkManager> mgr_b_;
  std::shared_ptr<MavlinkSkeletonBindings> skel_;
  std::shared_ptr<MavlinkProxyBindings> prox_;
  std::mutex mu_;
  std::vector<std::pair<uint16_t, std::vector<uint8_t>>> events_;
  std::map<uint16_t, uint8_t> statuses_;
  std::atomic<int> calls_{0};
  std::atomic<uint16_t> last_method_{0};
};

TEST_F(BindingsTest, EventFlowsFromSkeletonToProxyCallback) {
  StartBoth();
  skel_->HandleEvent(0x8001, std::vector<uint8_t>{0x34, 0x12});
  ASSERT_TRUE(WaitFor([&] { return EventCount() == 1; }));
  EXPECT_EQ(events_[0].first, 0x8001);
  EXPECT_EQ(events_[0].second, (std::vector<uint8_t>{0x34, 0x12}));
}

TEST_F(BindingsTest, MethodGoesThroughProxyToSkeletonCallback) {
  StartBoth();
  const auto r = prox_->HandleMethod(1, std::vector<uint8_t>{5});
  ASSERT_TRUE(r.HasValue());
  EXPECT_EQ(r.Value(), (std::vector<uint8_t>{15}));
  EXPECT_EQ(calls_.load(), 1);
  EXPECT_EQ(last_method_.load(), 1);
}

TEST_F(BindingsTest, SkeletonHandleMethodIsRejected) {
  StartBoth();
  const auto r = skel_->HandleMethod(1, {});
  EXPECT_FALSE(r.HasValue());
}

TEST_F(BindingsTest, ProxyHandleEventIsNoOp) {
  StartBoth();
  prox_->HandleEvent(0x8001, std::vector<uint8_t>{1, 2});
  std::this_thread::sleep_for(milliseconds(100));
  EXPECT_EQ(EventCount(), 0U);
}

TEST_F(BindingsTest, SubscribeIsLocalAndReportsAvailability) {
  ModelMavlinkOptions prox;
  prox.exclude = {};
  StartBoth({}, prox);
  prox_->SubscribeToEvent(0x8001);   // dostępny
  prox_->SubscribeToEvent(0x8002);   // wyłączony w service.json
  prox_->SubscribeToEvent(0x8999);   // nieznany
  std::lock_guard<std::mutex> lock{mu_};
  EXPECT_EQ(statuses_[0x8001], 2);  // kSubscribe
  EXPECT_EQ(statuses_[0x8002], 0);  // kNotSubscribe
  EXPECT_EQ(statuses_[0x8999], 0);
}

TEST_F(BindingsTest, SubscribeToInstanceExcludedEventIsNotSubscribed) {
  ModelMavlinkOptions prox;
  prox.exclude = {"Temp"};
  StartBoth({}, prox);
  prox_->SubscribeToEvent(0x8001);
  std::lock_guard<std::mutex> lock{mu_};
  EXPECT_EQ(statuses_[0x8001], 0);
}

TEST_F(BindingsTest, SubscribeBeforeCallbacksAreSetIsHarmless) {
  MavlinkProxyBindings p{Model(false, 5, "lk"), mgr_b_.get()};
  p.SubscribeToEvent(0x8001);  // brak callbacku i endpointu: bez crasha
}

TEST_F(BindingsTest, StopDetachesAndMethodsFail) {
  StartBoth();
  prox_->Stop();
  const auto r = prox_->HandleMethod(1, std::vector<uint8_t>{1});
  EXPECT_FALSE(r.HasValue());
  skel_->Stop();
  skel_->HandleEvent(0x8001, std::vector<uint8_t>{1, 2});  // po Stop: no-op
}

TEST_F(BindingsTest, MethodWithoutSkeletonCallbackGivesErrorNotCrash) {
  StartBoth();
  skel_->SetSkeletonCallback(nullptr);
  const auto r = prox_->HandleMethod(1, std::vector<uint8_t>{1});
  EXPECT_FALSE(r.HasValue());
}

TEST_F(BindingsTest, ExcludedMethodOnProxyFailsWithoutTraffic) {
  ModelMavlinkOptions prox;
  prox.exclude = {"Add"};
  StartBoth({}, prox);
  EXPECT_FALSE(prox_->HandleMethod(1, std::vector<uint8_t>{1}).HasValue());
  EXPECT_EQ(calls_.load(), 0);
}

TEST_F(BindingsTest, ExcludedEventOnSkeletonIsDroppedSilently) {
  ModelMavlinkOptions skel;
  skel.exclude = {"Temp"};
  StartBoth(skel);
  skel_->HandleEvent(0x8001, std::vector<uint8_t>{1, 2});
  std::this_thread::sleep_for(milliseconds(150));
  EXPECT_EQ(EventCount(), 0U);
  EXPECT_EQ(skel_->endpoint()->counters().tx_excluded, 1U);
}

// ---------------------------------------------------------------- ServiceSkeleton / ServiceHandler

class TestSkeleton : public ara::com::someip::ServiceSkeleton {
 public:
  explicit TestSkeleton(const ara::core::InstanceSpecifier& i) : ServiceSkeleton{i, 0x77} {}
  void Publish(uint16_t id, const std::vector<uint8_t>& d) { HandleEvent(id, d); }
  std::atomic<int> ready{0};
  std::atomic<int> calls{0};

 protected:
  ara::core::Result<std::vector<uint8_t>> HandleMethod(const uint16_t method_id,
                                                       const std::vector<uint8_t>& data) override {
    calls++;
    return std::vector<uint8_t>{static_cast<uint8_t>(method_id + data[0])};
  }
  void BinderReady() override { ready++; }
};

class TestHandler : public ara::com::someip::ServiceHandler {
 public:
  explicit TestHandler(const ara::core::InstanceSpecifier& i) : ServiceHandler{i, 0x77} {}
  ara::core::Result<std::vector<uint8_t>> Call(uint16_t id, const std::vector<uint8_t>& d) {
    return HandleMethod(id, d);
  }
  std::mutex mu;
  std::vector<std::pair<uint16_t, std::vector<uint8_t>>> events;
  std::map<uint16_t, uint8_t> status;

 protected:
  void HandleEvent(const uint16_t id, const std::vector<uint8_t>& d) override {
    std::lock_guard<std::mutex> lock{mu};
    events.emplace_back(id, d);
  }
  void SubscribeNewStatus(const uint16_t id, const uint8_t s) override {
    std::lock_guard<std::mutex> lock{mu};
    status[id] = s;
  }
};

TEST(MavlinkServicePath, StartOfferAndStartFindResolveModelMavlinkAndTalkThroughLinks) {
  // ServiceSkeleton/ServiceHandler logują przez ara::log, który musi być zainicjowany.
  ara::log::LoggingMenager::Create("BND-", ara::log::LogMode::kConsole, ara::log::LogLevel::kWarn);
  auto pair = MemoryTransport::MakePair("svc-sk", "svc-px", 64 * 1024);
  auto& db = ara::core::ModelDataBase::GetInstance();
  ASSERT_TRUE(db.AddNewItem("svc/skeleton", {Model(true, 9, "svc_sk")}).HasValue());
  ASSERT_TRUE(db.AddNewItem("svc/proxy", {Model(false, 9, "svc_px")}).HasValue());
  ASSERT_TRUE(ara::com::mavlink::RegisterLinkTransport("svc_sk", pair.first).HasValue());
  ASSERT_TRUE(ara::com::mavlink::RegisterLinkTransport("svc_px", pair.second).HasValue());

  TestSkeleton skel{ara::core::InstanceSpecifier{"svc/skeleton"}};
  ASSERT_TRUE(skel.StartOffer().HasValue());
  EXPECT_EQ(skel.ready.load(), 1);  // BinderReady wywołane (generowany kod podpina eventy)
  TestHandler handler{ara::core::InstanceSpecifier{"svc/proxy"}};
  ASSERT_TRUE(handler.StartFind({}).HasValue());

  auto& mgr = LinkManager::Instance();
  ASSERT_TRUE(WaitFor([&] {
    const auto a = mgr.FindLink("svc_sk");
    const auto b = mgr.FindLink("svc_px");
    return a && b && a->IsReady() && b->IsReady();
  }, 4000));

  skel.Publish(0x8001, std::vector<uint8_t>{0x01, 0x02});
  ASSERT_TRUE(WaitFor([&] {
    std::lock_guard<std::mutex> lock{handler.mu};
    return handler.events.size() == 1;
  }));
  const auto r = handler.Call(1, std::vector<uint8_t>{5});
  ASSERT_TRUE(r.HasValue());
  EXPECT_EQ(r.Value(), (std::vector<uint8_t>{6}));  // method_id + data[0]
  EXPECT_EQ(skel.calls.load(), 1);

  EXPECT_TRUE(skel.StopOffer().HasValue());
  mgr.Shutdown();
  ara::com::mavlink::ClearLinkTransports();
}

}  // namespace
}  // namespace bindings
}  // namespace someip
}  // namespace com
}  // namespace ara
