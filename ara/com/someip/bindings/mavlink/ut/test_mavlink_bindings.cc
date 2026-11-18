/**
 * Copyright (c) 2024
 *
 * @file test_mavlink_bindings.cc
 * @brief MAVLink bindings behind IBindings (the MavlinkDaemon IPC client, a fake daemon) and the
 * ServiceSkeleton/ServiceHandler path (StartOffer/StartFind) on a ModelMavlink model.
 */
#include <gtest/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>  // NOLINT
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <mutex>  // NOLINT
#include <optional>
#include <string>
#include <thread>  // NOLINT
#include <vector>

#include "ara/com/com_error_domain.h"
#include "ara/com/mavlink/client/daemon_client.h"
#include "ara/com/mavlink/client/testing/fake_daemon.h"
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
using ara::com::mavlink::DaemonClient;
using ara::com::mavlink::MessageCodec;
using ara::com::mavlink::MessageHeader;
using ara::com::mavlink::testing::FakeDaemon;
using ara::core::model::ModelMavlink;
using ara::core::model::ModelMavlinkElement;
using ara::core::model::ModelMavlinkField;
using ara::core::model::ModelMavlinkMessage;
using ara::core::model::ModelMavlinkOptions;
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

ModelMavlinkMessage Msg(std::uint32_t id, const std::string& name,
                        std::vector<ModelMavlinkField> f) {
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
  ev.req = Msg(300, "B_TEMP",
               {F("seq", Type::kU16, Role::kSeq), F("instance", Type::kU16, Role::kInstance),
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
  m.req = Msg(310, "B_ADD_REQ",
              {F("seq", Type::kU16, Role::kSeq), F("instance", Type::kU16, Role::kInstance),
               F("target_system", Type::kU8, Role::kTargetSystem),
               F("target_component", Type::kU8, Role::kTargetComponent), F("value", Type::kU8)});
  m.has_resp = true;
  m.resp = Msg(311, "B_ADD_RESP",
               {F("seq", Type::kU16, Role::kSeq), F("instance", Type::kU16, Role::kInstance),
                F("target_system", Type::kU8, Role::kTargetSystem),
                F("target_component", Type::kU8, Role::kTargetComponent),
                F("status", Type::kU8, Role::kStatus), F("value", Type::kU8)});
  out.push_back(m);
  return out;
}

/// Options with the element `name` outside the whitelist (on no radio).
ModelMavlinkOptions Without(const std::string& name) {
  ModelMavlinkOptions o;
  o.elements = Elements();
  for (auto& e : o.elements) {
    if (e.name == name) e.enabled = false;
  }
  return o;
}

ModelMavlink Model(bool skeleton, std::uint16_t instance, const std::string& link,
                   ModelMavlinkOptions opts = {}) {
  if (opts.elements.empty()) opts.elements = Elements();
  for (auto& e : opts.elements) {  // an enabled element goes over the link of the instance
    if (e.enabled && e.links.empty()) e.links = {{link, 0.0}};
  }
  const auto fn = skeleton ? &ModelMavlink::CreatSkeletonItem : &ModelMavlink::CreatProxyItem;
  return fn("c" + link, instance, skeleton ? 0 : 1, skeleton ? 0 : 1, {link}, opts).Value();
}

bool WaitFor(const std::function<bool()>& pred, int ms = 3000) {
  const auto end = Clock::now() + milliseconds(ms);
  while (Clock::now() < end) {
    if (pred()) return true;
    std::this_thread::sleep_for(milliseconds(5));
  }
  return pred();
}

std::string SocketPath(const std::string& tag) {
  return "/tmp/srp_mav_bnd_" + tag + "_" + std::to_string(::getpid()) + ".sock";
}

// DaemonClient::Instance() reads the socket path from the environment on first use (also in the
// factory test, which creates bindings with the default client) — set it before all tests.
class SocketEnv : public ::testing::Environment {
 public:
  void SetUp() override { ::setenv("SRP_MAVLINK_DAEMON_SOCKET", SocketPath("svc").c_str(), 1); }
};
const ::testing::Environment* const kSocketEnv =
    ::testing::AddGlobalTestEnvironment(new SocketEnv);

// ---------------------------------------------------------------- factory

TEST(MavlinkBindingsFactory, UnknownInstanceIsNotAMavlinkInstance) {
  EXPECT_EQ(MakeMavlinkBindings(ara::core::InstanceSpecifier{"no/such/instance"}, 1), nullptr);
}

TEST(MavlinkBindingsFactory, ChoosesByDirectionNotByPort) {
  const auto skel = BindingsFactory::GetBindings(Model(true, 1, "f1"));
  const auto prox = BindingsFactory::GetBindings(Model(false, 1, "f2"));
  ASSERT_NE(skel, nullptr);
  ASSERT_NE(prox, nullptr);
  EXPECT_NE(dynamic_cast<MavlinkSkeletonBindings*>(skel.get()), nullptr);
  EXPECT_NE(dynamic_cast<MavlinkProxyBindings*>(prox.get()), nullptr);
}

// ---------------------------------------------------------------- bindings through IBindings

class BindingsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    path_ = SocketPath("b");
    daemon_ = std::make_unique<FakeDaemon>(path_);
    ASSERT_TRUE(daemon_->Start());
    DaemonClient::Options o;
    o.socket_path = path_;
    o.reconnect_interval = milliseconds(50);
    o.poll_ms = 20;
    client_ = std::make_unique<DaemonClient>(o);
  }
  void TearDown() override {
    if (prox_) prox_->Stop();
    if (skel_) skel_->Stop();
    client_->Shutdown();
    daemon_->Stop();
  }

  void StartSkeleton(ModelMavlinkOptions opts = {}) {
    skel_ = std::make_shared<MavlinkSkeletonBindings>(Model(true, 5, "lk", opts), client_.get());
    skel_->SetSkeletonCallback([this](const uint16_t& id, const std::vector<uint8_t>& p) {
      calls_++;
      last_method_ = id;
      return ara::core::Result<std::vector<uint8_t>>{
          std::vector<uint8_t>{static_cast<uint8_t>(p.empty() ? 0 : p[0] + 10)}};
    });
    skel_->Start({});
    ASSERT_TRUE(WaitFor([&] { return skel_->endpoint()->AnyLinkReady(); }));
  }
  void StartProxy(ModelMavlinkOptions opts = {}) {
    prox_ = std::make_shared<MavlinkProxyBindings>(Model(false, 5, "lk", opts), client_.get());
    prox_->SetProxyCallback(
        [this](const uint16_t id, const std::vector<uint8_t>& p) {
          std::lock_guard<std::mutex> lock{mu_};
          events_.emplace_back(id, p);
        },
        [this](const uint16_t id, const uint8_t status) {
          std::lock_guard<std::mutex> lock{mu_};
          statuses_[id] = status;
        });
    prox_->Start({});
    ASSERT_TRUE(WaitFor([&] { return prox_->endpoint()->AnyLinkReady(); }));
  }
  std::size_t EventCount() {
    std::lock_guard<std::mutex> lock{mu_};
    return events_.size();
  }
  // handle of the instance in the fake daemon (the last registration)
  std::uint32_t LastHandle() { return daemon_->registrations().back().handle; }

  std::string path_;
  std::unique_ptr<FakeDaemon> daemon_;
  std::unique_ptr<DaemonClient> client_;
  std::shared_ptr<MavlinkSkeletonBindings> skel_;
  std::shared_ptr<MavlinkProxyBindings> prox_;
  std::mutex mu_;
  std::vector<std::pair<uint16_t, std::vector<uint8_t>>> events_;
  std::map<uint16_t, uint8_t> statuses_;
  std::atomic<int> calls_{0};
  std::atomic<uint16_t> last_method_{0};
};

TEST_F(BindingsTest, SkeletonEventGoesToDaemonAsFullPayload) {
  StartSkeleton();
  skel_->HandleEvent(0x8001, std::vector<uint8_t>{0x34, 0x12});
  const auto msg = daemon_->WaitMessage(milliseconds(1000));
  ASSERT_TRUE(msg.has_value());
  const auto& ev = std::get<ara::com::mavlink::daemon::ElementPayload>(*msg);
  EXPECT_EQ(ev.element_id, 0x8001);
  EXPECT_EQ(ev.handle, LastHandle());
  ASSERT_EQ(ev.payload.size(), 6U);
  EXPECT_EQ(ev.payload[4], 0x34);  // value: SOME/IP little (the default field endianness) = wire little
  EXPECT_EQ(ev.payload[5], 0x12);
}

TEST_F(BindingsTest, EventFromDaemonReachesProxyCallback) {
  StartProxy();
  const MessageCodec codec{Elements()[0].req};
  ara::com::mavlink::daemon::Delivery in;
  in.kind = ara::com::mavlink::daemon::ElementKind::kEvent;
  in.handle = LastHandle();
  in.element_id = 0x8001;
  in.sender_system = 1;
  in.sender_component = 1;
  in.link = "lk";
  in.payload = codec.Encode(MessageHeader{1, 5, 0, 0, 0}, {0x34, 0x12}).Value();
  ASSERT_TRUE(daemon_->SendToApp(in));
  ASSERT_TRUE(WaitFor([&] { return EventCount() == 1; }));
  EXPECT_EQ(events_[0].first, 0x8001);
  EXPECT_EQ(events_[0].second, (std::vector<uint8_t>{0x34, 0x12}));
}

TEST_F(BindingsTest, MethodGoesThroughProxyToDaemonAndBack) {
  StartProxy();
  const auto handle = LastHandle();
  const auto elements = Elements();
  std::jthread responder([&](std::stop_token) {
    const auto msg = daemon_->WaitMessage(milliseconds(2000));
    ASSERT_TRUE(msg.has_value());
    const auto& req = std::get<ara::com::mavlink::daemon::ElementPayload>(*msg);
    const auto dec = MessageCodec{elements[2].req}.Decode(req.payload).Value();
    EXPECT_EQ(dec.someip, (std::vector<uint8_t>{5}));
    ara::com::mavlink::daemon::Delivery in;
    in.kind = ara::com::mavlink::daemon::ElementKind::kResponse;
    in.handle = handle;
    in.element_id = 1;
    in.link = "lk";
    in.payload = MessageCodec{elements[2].resp}
                     .Encode(MessageHeader{dec.header.seq, 5, 0, 0, 0}, {15})
                     .Value();
    EXPECT_TRUE(daemon_->SendToApp(in));
  });
  const auto r = prox_->HandleMethod(1, std::vector<uint8_t>{5});
  ASSERT_TRUE(r.HasValue());
  EXPECT_EQ(r.Value(), (std::vector<uint8_t>{15}));
}

TEST_F(BindingsTest, SkeletonCallbackExecutesRequestFromDaemon) {
  StartSkeleton();
  const auto elements = Elements();
  ara::com::mavlink::daemon::Delivery in;
  in.kind = ara::com::mavlink::daemon::ElementKind::kRequest;
  in.handle = LastHandle();
  in.element_id = 1;
  in.sender_system = 1;
  in.sender_component = 1;
  in.link = "lk";
  in.payload = MessageCodec{elements[2].req}.Encode(MessageHeader{9, 5, 0, 0, 0}, {5}).Value();
  ASSERT_TRUE(daemon_->SendToApp(in));
  const auto msg = daemon_->WaitMessage(milliseconds(1000));
  ASSERT_TRUE(msg.has_value());
  const auto& resp = std::get<ara::com::mavlink::daemon::ElementPayload>(*msg);
  const auto dec = MessageCodec{elements[2].resp}.Decode(resp.payload).Value();
  EXPECT_EQ(dec.header.seq, 9);
  EXPECT_EQ(dec.header.status, 0);
  EXPECT_EQ(dec.someip, (std::vector<uint8_t>{15}));
  EXPECT_EQ(calls_.load(), 1);
  EXPECT_EQ(last_method_.load(), 1);
}

TEST_F(BindingsTest, SkeletonHandleMethodIsRejected) {
  StartSkeleton();
  EXPECT_FALSE(skel_->HandleMethod(1, {}).HasValue());
}

TEST_F(BindingsTest, ProxyHandleEventIsNoOp) {
  StartProxy();
  prox_->HandleEvent(0x8001, std::vector<uint8_t>{1, 2});
  EXPECT_FALSE(daemon_->WaitMessage(milliseconds(100)).has_value());
  EXPECT_EQ(EventCount(), 0U);
}

TEST_F(BindingsTest, SubscribeIsLocalAndReportsAvailability) {
  StartProxy();
  prox_->SubscribeToEvent(0x8001);  // available
  prox_->SubscribeToEvent(0x8002);  // disabled in service.json
  prox_->SubscribeToEvent(0x8999);  // unknown
  std::lock_guard<std::mutex> lock{mu_};
  EXPECT_EQ(statuses_[0x8001], 2);  // kSubscribe
  EXPECT_EQ(statuses_[0x8002], 0);  // kNotSubscribe
  EXPECT_EQ(statuses_[0x8999], 0);
}

TEST_F(BindingsTest, SubscribeToAnEventOutsideTheWhitelistIsNotSubscribed) {
  StartProxy(Without("Temp"));
  prox_->SubscribeToEvent(0x8001);
  std::lock_guard<std::mutex> lock{mu_};
  EXPECT_EQ(statuses_[0x8001], 0);
}

TEST_F(BindingsTest, SubscribeBeforeCallbacksAreSetIsHarmless) {
  MavlinkProxyBindings p{Model(false, 5, "lk"), client_.get()};
  p.SubscribeToEvent(0x8001);  // no callback and no endpoint: no crash
}

TEST_F(BindingsTest, StopDetachesAndMethodsFail) {
  StartProxy();
  StartSkeleton();
  prox_->Stop();
  EXPECT_FALSE(prox_->HandleMethod(1, std::vector<uint8_t>{1}).HasValue());
  skel_->Stop();
  skel_->HandleEvent(0x8001, std::vector<uint8_t>{1, 2});  // after Stop: a no-op
  EXPECT_TRUE(WaitFor([&] { return daemon_->unregistered() == 2U; }));
}

TEST_F(BindingsTest, MethodWithoutSkeletonCallbackReportsStatusNotCrash) {
  StartSkeleton();
  skel_->SetSkeletonCallback(nullptr);
  const auto elements = Elements();
  ara::com::mavlink::daemon::Delivery in;
  in.kind = ara::com::mavlink::daemon::ElementKind::kRequest;
  in.handle = LastHandle();
  in.element_id = 1;
  in.link = "lk";
  in.payload = MessageCodec{elements[2].req}.Encode(MessageHeader{1, 5, 0, 0, 0}, {5}).Value();
  ASSERT_TRUE(daemon_->SendToApp(in));
  const auto msg = daemon_->WaitMessage(milliseconds(1000));
  ASSERT_TRUE(msg.has_value());
  const auto& resp = std::get<ara::com::mavlink::daemon::ElementPayload>(*msg);
  EXPECT_EQ(MessageCodec{elements[2].resp}.Decode(resp.payload).Value().header.status, 1);
}

TEST_F(BindingsTest, MethodOutsideTheWhitelistOnProxyFailsWithoutTraffic) {
  StartProxy(Without("Add"));
  EXPECT_FALSE(prox_->HandleMethod(1, std::vector<uint8_t>{1}).HasValue());
  EXPECT_FALSE(daemon_->WaitMessage(milliseconds(100)).has_value());
}

TEST_F(BindingsTest, EventOutsideTheWhitelistOnSkeletonIsDroppedSilently) {
  StartSkeleton(Without("Temp"));
  skel_->HandleEvent(0x8001, std::vector<uint8_t>{1, 2});
  EXPECT_FALSE(daemon_->WaitMessage(milliseconds(150)).has_value());
  EXPECT_EQ(skel_->endpoint()->counters().tx_excluded, 1U);
}

TEST_F(BindingsTest, MethodWithoutDaemonFailsImmediately) {
  StartProxy();
  daemon_->Stop();
  ASSERT_TRUE(WaitFor([&] { return !prox_->endpoint()->IsRegistered(); }));
  const auto start = Clock::now();
  EXPECT_FALSE(prox_->HandleMethod(1, std::vector<uint8_t>{1}).HasValue());
  EXPECT_LT(Clock::now() - start, milliseconds(50));
}

// ---------------------------------------------------------------- ServiceSkeleton / ServiceHandler

class TestSkeleton : public ara::com::someip::ServiceSkeleton {
 public:
  explicit TestSkeleton(const ara::core::InstanceSpecifier& i) : ServiceSkeleton{i, 0x77} {}
  void Publish(uint16_t id, const std::vector<uint8_t>& d) { HandleEvent(id, d); }
  std::atomic<int> ready{0};

 protected:
  ara::core::Result<std::vector<uint8_t>> HandleMethod(const uint16_t method_id,
                                                       const std::vector<uint8_t>& data) override {
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

 protected:
  void HandleEvent(const uint16_t, const std::vector<uint8_t>&) override {}
  void SubscribeNewStatus(const uint16_t, const uint8_t) override {}
};

TEST(MavlinkServicePath, StartOfferAndStartFindResolveModelMavlinkAndRegisterInDaemon) {
  FakeDaemon daemon{SocketPath("svc")};
  ASSERT_TRUE(daemon.Start());
  // ServiceSkeleton/ServiceHandler log through ara::log, which must be initialised.
  ara::log::LoggingMenager::Create("BND-", ara::log::LogMode::kConsole, ara::log::LogLevel::kWarn);
  auto& db = ara::core::ModelDataBase::GetInstance();
  ASSERT_TRUE(db.AddNewItem("svc/skeleton", {Model(true, 9, "svc_sk")}).HasValue());
  ASSERT_TRUE(db.AddNewItem("svc/proxy", {Model(false, 9, "svc_px")}).HasValue());

  TestSkeleton skel{ara::core::InstanceSpecifier{"svc/skeleton"}};
  ASSERT_TRUE(skel.StartOffer().HasValue());
  EXPECT_EQ(skel.ready.load(), 1);  // BinderReady was called (generated code attaches the events)
  TestHandler handler{ara::core::InstanceSpecifier{"svc/proxy"}};
  ASSERT_TRUE(handler.StartFind({}).HasValue());

  ASSERT_TRUE(daemon.WaitRegistrations(2, milliseconds(4000)));
  const auto regs = daemon.registrations();
  ASSERT_EQ(regs.size(), 2U);
  // The skeleton and the proxy register on independent connections, so the registration order is unspecified.
  std::vector<std::vector<std::string>> links;
  for (const auto& reg : regs) {
    links.push_back(reg.links);
  }
  std::sort(links.begin(), links.end());
  EXPECT_EQ(links, (std::vector<std::vector<std::string>>{{"svc_px"}, {"svc_sk"}}));

  // The registration is confirmed asynchronously: an event sent before the RegisterReply is dropped
  // (counted in tx_dropped), so publish until it gets through.
  std::optional<ara::com::mavlink::daemon::Message> msg;
  for (int i = 0; i < 40 && !msg.has_value(); ++i) {
    skel.Publish(0x8001, std::vector<uint8_t>{0x01, 0x02});
    msg = daemon.WaitMessage(milliseconds(100));
  }
  ASSERT_TRUE(msg.has_value());
  EXPECT_EQ(std::get<ara::com::mavlink::daemon::ElementPayload>(*msg).element_id, 0x8001);

  EXPECT_TRUE(skel.StopOffer().HasValue());
  DaemonClient::Instance().Shutdown();
}

}  // namespace
}  // namespace bindings
}  // namespace someip
}  // namespace com
}  // namespace ara
