/**
 * Copyright (c) 2024
 *
 * @file test_mavlink_daemon_core.cc
 * @brief MavlinkDaemonCore with IPC clients and MemoryRadio drivers: registration and conflicts,
 * event/method in both directions, redundancy and dedup, driver restart after a fatal error,
 * cleanup after an application disconnects, isolation of slow applications, the signing replay
 * window and start errors. Per-link behaviour (queues, kBusy, HEARTBEAT, signing of TX) is tested in
 * test_radio_link.cc.
 */
#include <gtest/gtest.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>  // NOLINT
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>  // NOLINT
#include <string>
#include <thread>  // NOLINT
#include <tuple>
#include <utility>
#include <vector>

#include "ara/com/mavlink/mavlink_frame.h"
#include "ara/com/mavlink/mavlink_signing.h"
#include "ara/com/mavlink/testing/memory_radio.h"
#include "platform/common/mavlink_daemon/code/core/mavlink_daemon_core.h"
#include "platform/common/mavlink_daemon/code/ut/daemon_test_support.h"

namespace srp {
namespace mavlink_daemon {

/// Friend of MavlinkDaemonCore: the introspection the tests need without widening its API.
class MavlinkDaemonCoreTestAccess {
 public:
  static void AddDriver(MavlinkDaemonCore* c, const std::string& id, ara::com::mavlink::RadioDriverFactory f) {
    std::ignore = c->registry_.AddBuiltin(id, std::move(f));
  }
  static std::size_t InstanceCount(MavlinkDaemonCore* c) {
    std::lock_guard<std::mutex> lock{c->mutex_};
    return c->instances_.size();
  }
  static std::size_t SessionCount(MavlinkDaemonCore* c) {
    std::lock_guard<std::mutex> lock{c->mutex_};
    return static_cast<std::size_t>(std::count_if(c->sessions_.begin(), c->sessions_.end(),
                                                  [](const std::shared_ptr<MavlinkDaemonCore::Session>& s) {
                                                    return !s->done;
                                                  }));
  }
  static bool IsMsgIdRegistered(MavlinkDaemonCore* c, std::uint32_t msg_id) {
    std::lock_guard<std::mutex> lock{c->mutex_};
    return c->messages_.count(msg_id) != 0;
  }
};

namespace {
using Access = MavlinkDaemonCoreTestAccess;
using ara::com::mavlink::BuildFrame;
using ara::com::mavlink::ExpandPayload;
using ara::com::mavlink::Frame;
using ara::com::mavlink::FrameParser;
using ara::com::mavlink::LogLevel;
using ara::com::mavlink::testing::MemoryRadio;
using testing::Desc;
using testing::EventPayload;
using testing::MakeRegistration;
using testing::ReqPayload;
using testing::RespPayload;
using testing::SeqOf;
using testing::TestClient;
namespace dp = ara::com::mavlink::daemon;
using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;
using Bytes = std::vector<std::uint8_t>;

constexpr std::uint8_t kSys = 10;   // daemon system_id
constexpr std::uint8_t kComp = 3;   // daemon component_id
constexpr std::uint8_t kPeerSys = 7;
constexpr std::uint8_t kPeerComp = 1;
constexpr std::uint32_t kBaseA = 50000;  // service A: msg 50001..50005
constexpr std::uint32_t kBaseB = 60000;  // service B

std::string TmpPath(const std::string& name) {
  const char* d = std::getenv("TEST_TMPDIR");
  return std::string(d != nullptr ? d : "/tmp") + "/" + name;
}

struct Logs {
  std::mutex mu;
  std::vector<std::pair<LogLevel, std::string>> items;
  void Add(LogLevel l, const std::string& s) {
    std::lock_guard<std::mutex> lock{mu};
    items.emplace_back(l, s);
  }
  std::string Dump() {
    std::lock_guard<std::mutex> lock{mu};
    std::string o;
    for (const auto& item : items) o += "\n  [" + std::to_string(static_cast<int>(item.first)) + "] " + item.second;
    return o;
  }
  int Count(LogLevel l, const std::string& needle = "") {
    std::lock_guard<std::mutex> lock{mu};
    int n = 0;
    for (const auto& [lv, s] : items) {
      if (lv == l && s.find(needle) != std::string::npos) ++n;
    }
    return n;
  }
};

class DaemonTest : public ::testing::Test {
 protected:
  void SetUp() override {
    static std::atomic<int> counter{0};
    path_ = "/tmp/srp_mavd_" + std::to_string(::getpid()) + "_" + std::to_string(counter++) + ".sock";
    for (const char* name : {"l1", "l2"}) {
      radios_[name] = std::make_shared<MemoryRadio>(name);
      LinkConfigEntry l;
      l.name = name;
      l.driver = "memory_radio";
      l.driver_config = name;
      l.heartbeat_hz = 0;
      cfg_.links.push_back(l);
    }
    cfg_.system_id = kSys;
    cfg_.component_id = kComp;
    cfg_.socket_path = path_;
    cfg_.restart_delay = milliseconds(150);
  }
  void TearDown() override {
    clients_.clear();
    if (core_) core_->Stop();
  }

  DaemonOptions Options() {
    DaemonOptions o;
    o.log = [this](LogLevel l, const std::string& s) { logs_.Add(l, s); };
    return o;
  }

  /// Creates the daemon with the in-memory radios registered as the driver `memory_radio`.
  void MakeDaemon() {
    core_ = std::make_unique<MavlinkDaemonCore>(cfg_, Options());
    Access::AddDriver(core_.get(), "memory_radio",
                      [this](const std::string& cfg) -> std::shared_ptr<ara::com::mavlink::IRadioDriver> {
                        const auto it = radios_.find(cfg);
                        return it == radios_.end() ? nullptr : it->second;
                      });
  }

  bool WaitLinkState(const std::string& link, RadioLinkState st, milliseconds timeout) {
    const auto end = Clock::now() + timeout;
    while (Clock::now() < end) {
      const auto l = core_->FindLink(link);
      if (l && l->state() == st) return true;
      std::this_thread::sleep_for(milliseconds(2));
    }
    const auto l = core_->FindLink(link);
    return l && l->state() == st;
  }

  void StartDaemon(bool wait_links = true) {
    MakeDaemon();
    const auto r = core_->Start();
    ASSERT_TRUE(r.HasValue()) << r.Error().SupportData() << logs_.Dump();
    if (wait_links) {
      for (const auto& l : cfg_.links) {
        ASSERT_TRUE(WaitLinkState(l.name, RadioLinkState::kRunning, milliseconds(3000))) << l.name;
      }
    }
  }

  TestClient* NewClient() {
    clients_.push_back(std::make_unique<TestClient>(path_));
    EXPECT_TRUE(clients_.back()->connected());
    return clients_.back().get();
  }

  /// Registers an instance and checks for kOk; returns the client.
  TestClient* RegisterOk(dp::Role role, const std::string& com_id, std::uint16_t instance,
                         const std::vector<std::string>& links, std::uint32_t handle, std::uint32_t base = kBaseA,
                         std::uint32_t interval_ms = 0, TestClient* client = nullptr) {
    if (client == nullptr) client = NewClient();
    const auto reply = client->Register(MakeRegistration(role, com_id, instance, links, handle, base, interval_ms));
    EXPECT_TRUE(reply.has_value());
    if (reply) EXPECT_EQ(reply->status, dp::RegisterStatus::kOk) << reply->text;
    return client;
  }

  /// An "over the air" (v2) frame from the peer.
  Bytes PeerFrame(std::uint32_t msgid, const Bytes& payload, std::uint8_t sys = kPeerSys,
                  std::uint8_t comp = kPeerComp) {
    return BuildFrame(2, peer_seq_++, sys, comp, msgid, payload, testing::kCrc);
  }

  std::vector<Frame> Sent(const std::string& link) {
    std::vector<Frame> out;
    FrameParser parser{[](std::uint32_t id) -> std::optional<std::uint8_t> {
      if (id == 0) return 50;
      if ((id >= 50000 && id < 50100) || (id >= 60000 && id < 60100) || (id >= 70000 && id < 70100)) {
        return testing::kCrc;
      }
      return std::nullopt;
    }};
    for (const auto& bytes : radios_[link]->sent()) parser.Feed(bytes, &out);
    return out;
  }
  std::vector<Frame> SentWithId(const std::string& link, std::uint32_t msgid) {
    std::vector<Frame> out;
    for (auto& f : Sent(link)) {
      if (f.msgid == msgid) out.push_back(std::move(f));
    }
    return out;
  }
  bool WaitSentWithId(const std::string& link, std::uint32_t msgid, std::size_t n, std::uint32_t ms = 2000) {
    const auto end = Clock::now() + milliseconds(ms);
    while (Clock::now() < end) {
      if (SentWithId(link, msgid).size() >= n) return true;
      std::this_thread::sleep_for(milliseconds(5));
    }
    return SentWithId(link, msgid).size() >= n;
  }
  template <typename Pred>
  bool WaitFor(Pred pred, std::uint32_t ms = 2000) {
    const auto end = Clock::now() + milliseconds(ms);
    while (Clock::now() < end) {
      if (pred()) return true;
      std::this_thread::sleep_for(milliseconds(5));
    }
    return pred();
  }
  void WriteKey(const std::string& path, std::uint8_t seed) {
    std::ofstream out{path, std::ios::trunc};
    static const char* hex = "0123456789abcdef";
    for (int i = 0; i < 32; ++i) {
      const std::uint8_t b = static_cast<std::uint8_t>(seed + i);
      out << hex[b >> 4] << hex[b & 15];
    }
    out << "\n";
  }

  std::string path_;
  DaemonConfig cfg_;
  Logs logs_;
  std::map<std::string, std::shared_ptr<MemoryRadio>> radios_;
  std::unique_ptr<MavlinkDaemonCore> core_;
  std::vector<std::unique_ptr<TestClient>> clients_;
  std::uint8_t peer_seq_{0};
};

// ============================================================== registration and conflicts

TEST_F(DaemonTest, RegisterSendsReplyWithDaemonIdsAndInitialLinkStatus) {
  StartDaemon();
  auto* c = NewClient();
  const auto reply = c->Register(MakeRegistration(dp::Role::kProvide, "svc.A", 1, {"l1", "l2"}, 5));
  ASSERT_TRUE(reply.has_value());
  EXPECT_EQ(reply->status, dp::RegisterStatus::kOk);
  EXPECT_EQ(reply->handle, 5U);
  EXPECT_EQ(reply->system_id, kSys);
  EXPECT_EQ(reply->component_id, kComp);
  EXPECT_TRUE(c->WaitLinkStatus("l1", true));
  EXPECT_TRUE(c->WaitLinkStatus("l2", true));
  EXPECT_EQ(Access::InstanceCount(core_.get()), 1U);
  EXPECT_TRUE(Access::IsMsgIdRegistered(core_.get(), kBaseA + 1));
}

TEST_F(DaemonTest, DuplicateInstanceOfTheSameServiceAndRoleIsRejected) {
  StartDaemon();
  RegisterOk(dp::Role::kProvide, "svc.A", 1, {"l1"}, 1);
  auto* c2 = NewClient();
  const auto reply = c2->Register(MakeRegistration(dp::Role::kProvide, "svc.A", 1, {"l1"}, 1));
  ASSERT_TRUE(reply.has_value());
  EXPECT_EQ(reply->status, dp::RegisterStatus::kConflictInstance);
  EXPECT_NE(reply->text.find("svc.A"), std::string::npos);
  EXPECT_NE(reply->text.find("app#"), std::string::npos);
  EXPECT_EQ(Access::InstanceCount(core_.get()), 1U);
  EXPECT_EQ(core_->counters().register_rejected, 1U);
  EXPECT_GE(logs_.Count(LogLevel::kWarn, "rejected"), 1);
}

TEST_F(DaemonTest, SameServiceAndInstanceInOtherRoleOrOtherInstanceIsAccepted) {
  StartDaemon();
  RegisterOk(dp::Role::kProvide, "svc.A", 1, {"l1"}, 1);
  RegisterOk(dp::Role::kRequire, "svc.A", 1, {"l1"}, 1);  // a proxy of the same service (another application)
  RegisterOk(dp::Role::kProvide, "svc.A", 2, {"l1"}, 1);  // another instance, the same messages
  EXPECT_EQ(Access::InstanceCount(core_.get()), 3U);
}

TEST_F(DaemonTest, MsgIdUsedByAnotherServiceIsRejected) {
  StartDaemon();
  RegisterOk(dp::Role::kProvide, "svc.A", 1, {"l1"}, 1, kBaseA);
  auto* c2 = NewClient();
  const auto reply = c2->Register(MakeRegistration(dp::Role::kProvide, "svc.B", 1, {"l1"}, 1, kBaseA));
  ASSERT_TRUE(reply.has_value());
  EXPECT_EQ(reply->status, dp::RegisterStatus::kConflictMsgId);
  EXPECT_NE(reply->text.find("svc.A"), std::string::npos) << reply->text;
  EXPECT_EQ(Access::InstanceCount(core_.get()), 1U);
}

TEST_F(DaemonTest, MsgIdWithDifferentDefinitionIsRejected) {
  StartDaemon();
  RegisterOk(dp::Role::kProvide, "svc.A", 1, {"l1"}, 1, kBaseA);
  auto reg = MakeRegistration(dp::Role::kRequire, "svc.A", 1, {"l1"}, 1, kBaseA);
  reg.elements[0].req.crc_extra = 43;  // the same service, a different crc_extra
  const auto reply = NewClient()->Register(reg);
  ASSERT_TRUE(reply.has_value());
  EXPECT_EQ(reply->status, dp::RegisterStatus::kConflictMsgId);
}

TEST_F(DaemonTest, HeartbeatMsgIdAndDuplicatedIdInsideRegistrationAreRejected) {
  StartDaemon();
  auto hb = MakeRegistration(dp::Role::kProvide, "svc.A", 1, {"l1"}, 1, kBaseA);
  hb.elements[0].req.msg_id = 0;
  auto reply = NewClient()->Register(hb);
  ASSERT_TRUE(reply.has_value());
  EXPECT_EQ(reply->status, dp::RegisterStatus::kConflictMsgId);
  EXPECT_NE(reply->text.find("HEARTBEAT"), std::string::npos);
  auto dup = MakeRegistration(dp::Role::kProvide, "svc.A", 1, {"l1"}, 2, kBaseA);
  dup.elements[1].req.msg_id = dup.elements[0].req.msg_id;
  reply = NewClient()->Register(dup);
  ASSERT_TRUE(reply.has_value());
  EXPECT_EQ(reply->status, dp::RegisterStatus::kConflictMsgId);
  EXPECT_EQ(Access::InstanceCount(core_.get()), 0U);
}

TEST_F(DaemonTest, UnknownLinkBadVersionInvalidAndDuplicateHandleAreRejectedWithReason) {
  StartDaemon();
  auto* c = NewClient();
  auto reply = c->Register(MakeRegistration(dp::Role::kProvide, "svc.A", 1, {"l1", "nie_ma"}, 1));
  ASSERT_TRUE(reply.has_value());
  EXPECT_EQ(reply->status, dp::RegisterStatus::kUnknownLink);
  EXPECT_NE(reply->text.find("nie_ma"), std::string::npos);
  EXPECT_NE(reply->text.find("l1"), std::string::npos);  // the list of links from the config

  auto bad_version = MakeRegistration(dp::Role::kProvide, "svc.A", 1, {"l1"}, 2);
  bad_version.protocol_version = 99;
  reply = c->Register(bad_version);
  ASSERT_TRUE(reply.has_value());
  EXPECT_EQ(reply->status, dp::RegisterStatus::kBadVersion);

  auto invalid = MakeRegistration(dp::Role::kProvide, "svc.A", 1, {"l1"}, 3);
  invalid.elements.clear();
  reply = c->Register(invalid);
  ASSERT_TRUE(reply.has_value());
  EXPECT_EQ(reply->status, dp::RegisterStatus::kInvalid);

  RegisterOk(dp::Role::kProvide, "svc.A", 1, {"l1"}, 4, kBaseA, 0, c);
  reply = c->Register(MakeRegistration(dp::Role::kProvide, "svc.B", 1, {"l1"}, 4, kBaseB));
  ASSERT_TRUE(reply.has_value());
  EXPECT_EQ(reply->status, dp::RegisterStatus::kDuplicateHandle);
  EXPECT_EQ(Access::InstanceCount(core_.get()), 1U);
}

TEST_F(DaemonTest, ClientDisconnectCleansUpRegistrationsAndFreesIdentifiers) {
  StartDaemon();
  // another application keeps the radio busy (held), so the event of the vanishing application
  // waits in the queue
  auto* keeper = RegisterOk(dp::Role::kProvide, "svc.K", 1, {"l1"}, 1, 70000);
  radios_["l1"]->SetHold(true);
  keeper->Send(testing::Event(1, testing::kEv1, EventPayload(1, 1, 1)));
  ASSERT_TRUE(WaitFor([&] { return radios_["l1"]->busy_count() > 0; }));
  {
    auto client = std::make_unique<TestClient>(path_);
    ASSERT_TRUE(client->Register(MakeRegistration(dp::Role::kProvide, "svc.A", 1, {"l1"}, 1, kBaseA)).has_value());
    ASSERT_TRUE(client->Register(MakeRegistration(dp::Role::kRequire, "svc.B", 1, {"l1"}, 2, kBaseB)).has_value());
    ASSERT_EQ(Access::InstanceCount(core_.get()), 3U);
    client->Send(testing::Event(1, testing::kEv1, EventPayload(1, 1, 5)));
    // give the daemon time to accept the event into its queue
    std::this_thread::sleep_for(milliseconds(100));
    client->Close();  // disconnect (like an application crash)
  }
  ASSERT_TRUE(WaitFor([&] { return Access::InstanceCount(core_.get()) == 1; }));
  EXPECT_FALSE(Access::IsMsgIdRegistered(core_.get(), kBaseA + 1));
  EXPECT_FALSE(Access::IsMsgIdRegistered(core_.get(), kBaseB + 2));
  EXPECT_TRUE(Access::IsMsgIdRegistered(core_.get(), 70001));  // the keeper application stays
  EXPECT_TRUE(WaitFor([&] { return Access::SessionCount(core_.get()) == 1; }));
  // the identifiers returned to the pool: another application can register the same instance and msg_id
  RegisterOk(dp::Role::kProvide, "svc.A", 1, {"l1"}, 1, kBaseA);
  RegisterOk(dp::Role::kProvide, "svc.X", 1, {"l1"}, 1, kBaseB);  // msg_id taken earlier by svc.B
  radios_["l1"]->SetHold(false);
  ASSERT_TRUE(WaitSentWithId("l1", 70001, 1));
  std::this_thread::sleep_for(milliseconds(200));
  EXPECT_EQ(SentWithId("l1", kBaseA + 1).size(), 0U) << "the event of an unregistered instance must not go out";
}

TEST_F(DaemonTest, UnregisterRemovesOnlyThatInstance) {
  StartDaemon();
  auto* c = RegisterOk(dp::Role::kProvide, "svc.A", 1, {"l1"}, 1, kBaseA);
  RegisterOk(dp::Role::kProvide, "svc.B", 1, {"l1"}, 2, kBaseB, 0, c);
  ASSERT_EQ(Access::InstanceCount(core_.get()), 2U);
  c->Send(dp::Unregister{1});
  ASSERT_TRUE(WaitFor([&] { return Access::InstanceCount(core_.get()) == 1; }));
  EXPECT_FALSE(Access::IsMsgIdRegistered(core_.get(), kBaseA + 1));
  EXPECT_TRUE(Access::IsMsgIdRegistered(core_.get(), kBaseB + 1));
  c->Send(testing::Event(1, testing::kEv1, EventPayload(1, 1, 5)));  // unregistered: rejected
  c->Send(testing::Event(2, testing::kEv1, EventPayload(1, 1, 6)));
  ASSERT_TRUE(WaitSentWithId("l1", kBaseB + 1, 1));
  EXPECT_EQ(SentWithId("l1", kBaseA + 1).size(), 0U);
  EXPECT_GE(core_->counters().send_rejected, 1U);
}

TEST_F(DaemonTest, GarbageFromClientClosesItsSessionAndCleansUp) {
  StartDaemon();
  const int fd = ::socket(AF_UNIX, SOCK_SEQPACKET, 0);
  ASSERT_GE(fd, 0);
  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path_.c_str());
  ASSERT_EQ(::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
  ASSERT_TRUE(WaitFor([&] { return Access::SessionCount(core_.get()) == 1; }));
  const unsigned char junk[] = {0xEE, 1, 2, 3};
  ASSERT_EQ(::send(fd, junk, sizeof(junk), 0), 4);
  EXPECT_TRUE(WaitFor([&] { return Access::SessionCount(core_.get()) == 0; }));
  ::close(fd);
}

TEST_F(DaemonTest, MessageOfDaemonToAppTypeFromClientClosesSession) {
  StartDaemon();
  auto* c = NewClient();
  ASSERT_TRUE(WaitFor([&] { return Access::SessionCount(core_.get()) == 1; }));
  c->Send(dp::LinkStatus{1, "l1", true});
  EXPECT_TRUE(WaitFor([&] { return Access::SessionCount(core_.get()) == 0; }));
}

// ============================================================== events

TEST_F(DaemonTest, EventFromAppIsSentOnAllLinksOfTheInstanceWithTheSamePayload) {
  StartDaemon();
  auto* c = RegisterOk(dp::Role::kProvide, "svc.A", 1, {"l1", "l2"}, 1);
  const auto payload = EventPayload(77, 1, 0xCAFEBABE);
  c->Send(testing::Event(1, testing::kEv1, payload));
  for (const char* link : {"l1", "l2"}) {
    ASSERT_TRUE(WaitSentWithId(link, kBaseA + 1, 1)) << link;
    const auto f = SentWithId(link, kBaseA + 1).at(0);
    EXPECT_EQ(ExpandPayload(f.payload, 8), payload);
    EXPECT_EQ(f.sysid, kSys);
    EXPECT_EQ(f.compid, kComp);
  }
}

TEST_F(DaemonTest, TwoApplicationsShareOneLink) {
  StartDaemon();
  auto* a = RegisterOk(dp::Role::kProvide, "svc.A", 1, {"l1"}, 1, kBaseA);
  auto* b = RegisterOk(dp::Role::kProvide, "svc.B", 1, {"l1"}, 1, kBaseB);
  a->Send(testing::Event(1, testing::kEv1, EventPayload(1, 1, 111)));
  b->Send(testing::Event(1, testing::kEv1, EventPayload(1, 1, 222)));
  ASSERT_TRUE(WaitSentWithId("l1", kBaseA + 1, 1));
  ASSERT_TRUE(WaitSentWithId("l1", kBaseB + 1, 1));
  EXPECT_EQ(ExpandPayload(SentWithId("l1", kBaseA + 1).at(0).payload, 8), EventPayload(1, 1, 111));
  EXPECT_EQ(ExpandPayload(SentWithId("l1", kBaseB + 1).at(0).payload, 8), EventPayload(1, 1, 222));
}

TEST_F(DaemonTest, ExcludedEventIsANoOpWithZeroBytesOnTheWire) {
  StartDaemon();
  auto reg = MakeRegistration(dp::Role::kProvide, "svc.A", 1, {"l1"}, 1);
  reg.elements[0].available = false;
  auto* c = NewClient();
  ASSERT_TRUE(c->Register(reg).has_value());
  c->Send(testing::Event(1, testing::kEv1, EventPayload(1, 1, 5)));
  c->Send(testing::Event(1, testing::kEv2, EventPayload(1, 1, 6)));
  ASSERT_TRUE(WaitSentWithId("l1", kBaseA + 4, 1));
  EXPECT_EQ(SentWithId("l1", kBaseA + 1).size(), 0U);
}

TEST_F(DaemonTest, MalformedEventsAreRejectedAndCounted) {
  StartDaemon();
  auto* c = RegisterOk(dp::Role::kProvide, "svc.A", 1, {"l1"}, 1);
  c->Send(testing::Event(1, testing::kEv1, Bytes{1, 2, 3}));                 // wrong length
  c->Send(testing::Event(1, 0x8099, EventPayload(1, 1, 1)));                 // unknown element
  c->Send(testing::Event(9, testing::kEv1, EventPayload(1, 1, 1)));          // unknown handle
  c->Send(testing::Event(1, testing::kMethod, ReqPayload(1, 1, 0, 0, 0)));   // a method sent as an Event
  ASSERT_TRUE(WaitFor([&] { return core_->counters().send_rejected >= 4; }));
  std::this_thread::sleep_for(milliseconds(100));
  EXPECT_EQ(radios_["l1"]->sent_count(), 0U);
}

TEST_F(DaemonTest, EventFromTheRadioIsDeliveredToTheProxyApplication) {
  StartDaemon();
  auto* c = RegisterOk(dp::Role::kRequire, "svc.A", 3, {"l1"}, 4);
  const auto payload = EventPayload(5, 3, 0x11223344);
  radios_["l1"]->Inject(PeerFrame(kBaseA + 1, payload));
  const auto in = c->NextDelivery(dp::ElementKind::kEvent);
  ASSERT_TRUE(in.has_value());
  EXPECT_EQ(in->handle, 4U);
  EXPECT_EQ(in->element_id, testing::kEv1);
  EXPECT_EQ(in->sender_system, kPeerSys);
  EXPECT_EQ(in->sender_component, kPeerComp);
  EXPECT_EQ(in->link, "l1");
  EXPECT_EQ(in->payload, payload);
}

TEST_F(DaemonTest, DeliveryRoutesByServiceAndInstance) {
  StartDaemon();
  auto* a3 = RegisterOk(dp::Role::kRequire, "svc.A", 3, {"l1"}, 1, kBaseA);
  auto* a4 = RegisterOk(dp::Role::kRequire, "svc.A", 4, {"l1"}, 1, kBaseA);
  auto* b = RegisterOk(dp::Role::kRequire, "svc.B", 3, {"l1"}, 1, kBaseB);
  radios_["l1"]->Inject(PeerFrame(kBaseA + 1, EventPayload(1, 4, 44)));
  ASSERT_TRUE(a4->NextDelivery(dp::ElementKind::kEvent).has_value());
  EXPECT_TRUE(a3->NoDelivery(dp::ElementKind::kEvent));  // another instance of the same service
  EXPECT_TRUE(b->NoDelivery(dp::ElementKind::kEvent));   // another service
}

TEST_F(DaemonTest, ProvideInstanceDoesNotReceiveEventsOnlyProxyDoes) {
  StartDaemon();
  auto* provider = RegisterOk(dp::Role::kProvide, "svc.A", 3, {"l1"}, 1, kBaseA);
  auto* proxy = RegisterOk(dp::Role::kRequire, "svc.A", 3, {"l1"}, 1, kBaseA);
  radios_["l1"]->Inject(PeerFrame(kBaseA + 1, EventPayload(1, 3, 5)));
  EXPECT_TRUE(proxy->NextDelivery(dp::ElementKind::kEvent).has_value());
  EXPECT_TRUE(provider->NoDelivery(dp::ElementKind::kEvent));
}

TEST_F(DaemonTest, FrameFromALinkOutsideTheInstanceIsRejected) {
  StartDaemon();
  auto* c = RegisterOk(dp::Role::kRequire, "svc.A", 3, {"l1"}, 1);
  radios_["l2"]->Inject(PeerFrame(kBaseA + 1, EventPayload(1, 3, 5)));  // link l2 does not belong to the instance
  EXPECT_TRUE(c->NoDelivery(dp::ElementKind::kEvent, 300));
  radios_["l1"]->Inject(PeerFrame(kBaseA + 1, EventPayload(2, 3, 6)));
  EXPECT_TRUE(c->NextDelivery(dp::ElementKind::kEvent).has_value());
}

TEST_F(DaemonTest, SourceFilterOnTheProxyDropsOtherSenders) {
  StartDaemon();
  auto reg = MakeRegistration(dp::Role::kRequire, "svc.A", 3, {"l1"}, 1);
  reg.source_system = kPeerSys;
  reg.source_component = kPeerComp;
  auto* c = NewClient();
  ASSERT_TRUE(c->Register(reg).has_value());
  radios_["l1"]->Inject(PeerFrame(kBaseA + 1, EventPayload(1, 3, 1), 8, 1));  // different system
  radios_["l1"]->Inject(PeerFrame(kBaseA + 1, EventPayload(2, 3, 2), kPeerSys, 2));  // different component
  EXPECT_TRUE(c->NoDelivery(dp::ElementKind::kEvent, 300));
  radios_["l1"]->Inject(PeerFrame(kBaseA + 1, EventPayload(3, 3, 3)));
  EXPECT_TRUE(c->NextDelivery(dp::ElementKind::kEvent).has_value());
}

TEST_F(DaemonTest, ExcludedEventFromTheRadioIsRejected) {
  StartDaemon();
  auto reg = MakeRegistration(dp::Role::kRequire, "svc.A", 3, {"l1"}, 1);
  reg.elements[0].available = false;
  auto* c = NewClient();
  ASSERT_TRUE(c->Register(reg).has_value());
  radios_["l1"]->Inject(PeerFrame(kBaseA + 1, EventPayload(1, 3, 1)));
  EXPECT_TRUE(c->NoDelivery(dp::ElementKind::kEvent, 300));
}

TEST_F(DaemonTest, UnknownMessagesAreCounted) {
  StartDaemon();
  RegisterOk(dp::Role::kRequire, "svc.A", 3, {"l1"}, 1);
  radios_["l1"]->Inject(BuildFrame(2, 1, 5, 6, 777, Bytes{1}, testing::kCrc));
  ASSERT_TRUE(WaitFor([&] { return core_->FindLink("l1")->counters().rx_dropped >= 1; }));
}

// ============================================================== methods

TEST_F(DaemonTest, MethodRoundTripFromTheProxySide) {
  StartDaemon();
  auto* c = RegisterOk(dp::Role::kRequire, "svc.A", 3, {"l1"}, 1);
  ASSERT_TRUE(c->WaitLinkStatus("l1", true));
  const auto req = ReqPayload(21, 3, kPeerSys, kPeerComp, 9);
  c->Send(testing::Request(1, testing::kMethod, req));
  ASSERT_TRUE(WaitSentWithId("l1", kBaseA + 2, 1));
  const auto f = SentWithId("l1", kBaseA + 2).at(0);
  EXPECT_EQ(ExpandPayload(f.payload, 7), req);
  // a reply from the peer: target = the daemon
  radios_["l1"]->Inject(PeerFrame(kBaseA + 3, RespPayload(21, 3, kSys, kComp, 0, 77)));
  const auto in = c->NextDelivery(dp::ElementKind::kResponse);
  ASSERT_TRUE(in.has_value());
  EXPECT_EQ(in->element_id, testing::kMethod);
  EXPECT_EQ(SeqOf(in->payload), 21);
  EXPECT_EQ(in->payload[6], 0);   // status
  EXPECT_EQ(in->payload[7], 77);  // result
}

TEST_F(DaemonTest, ResponseForSomeoneElseIsNotDelivered) {
  StartDaemon();
  auto* c = RegisterOk(dp::Role::kRequire, "svc.A", 3, {"l1"}, 1);
  radios_["l1"]->Inject(PeerFrame(kBaseA + 3, RespPayload(21, 3, kSys + 1, kComp, 0, 1)));
  radios_["l1"]->Inject(PeerFrame(kBaseA + 3, RespPayload(22, 3, kSys, kComp + 1, 0, 1)));
  EXPECT_TRUE(c->NoDelivery(dp::ElementKind::kResponse, 300));
}

TEST_F(DaemonTest, RequestFromTheRadioIsExecutedByTheProviderAndAnswered) {
  StartDaemon();
  auto* c = RegisterOk(dp::Role::kProvide, "svc.A", 3, {"l1"}, 2);
  radios_["l1"]->Inject(PeerFrame(kBaseA + 2, ReqPayload(31, 3, kSys, kComp, 5)));
  const auto in = c->NextDelivery(dp::ElementKind::kRequest);
  ASSERT_TRUE(in.has_value());
  EXPECT_EQ(in->element_id, testing::kMethod);
  EXPECT_EQ(in->sender_system, kPeerSys);
  EXPECT_EQ(in->sender_component, kPeerComp);
  EXPECT_EQ(SeqOf(in->payload), 31);
  c->Send(testing::Response(2, testing::kMethod, RespPayload(31, 3, kPeerSys, kPeerComp, 0, 88)));
  ASSERT_TRUE(WaitSentWithId("l1", kBaseA + 3, 1));
  const auto f = SentWithId("l1", kBaseA + 3).at(0);
  EXPECT_EQ(ExpandPayload(f.payload, 8), RespPayload(31, 3, kPeerSys, kPeerComp, 0, 88));
}

TEST_F(DaemonTest, RequestAddressedToAnotherSystemIsIgnoredAndBroadcastIsAccepted) {
  StartDaemon();
  auto* c = RegisterOk(dp::Role::kProvide, "svc.A", 3, {"l1"}, 2);
  radios_["l1"]->Inject(PeerFrame(kBaseA + 2, ReqPayload(1, 3, kSys + 1, kComp, 1)));
  radios_["l1"]->Inject(PeerFrame(kBaseA + 2, ReqPayload(2, 3, kSys, kComp + 1, 1)));
  EXPECT_TRUE(c->NoDelivery(dp::ElementKind::kRequest, 300));
  radios_["l1"]->Inject(PeerFrame(kBaseA + 2, ReqPayload(3, 3, 0, 0, 1)));  // 0/0 = broadcast
  EXPECT_TRUE(c->NextDelivery(dp::ElementKind::kRequest).has_value());
}

TEST_F(DaemonTest, RequestForAnUnknownInstanceGetsStatusTwoOnTheArrivalLink) {
  StartDaemon();
  RegisterOk(dp::Role::kProvide, "svc.A", 3, {"l1", "l2"}, 2);
  radios_["l2"]->Inject(PeerFrame(kBaseA + 2, ReqPayload(41, 99, kSys, kComp, 1)));  // instance 99 does not exist
  ASSERT_TRUE(WaitSentWithId("l2", kBaseA + 3, 1));
  const auto f = SentWithId("l2", kBaseA + 3).at(0);
  const auto p = ExpandPayload(f.payload, 8);
  EXPECT_EQ(SeqOf(p), 41);
  EXPECT_EQ(p[4], kPeerSys);
  EXPECT_EQ(p[5], kPeerComp);
  EXPECT_EQ(p[6], 2);  // status: unknown instance / method
  EXPECT_EQ(SentWithId("l1", kBaseA + 3).size(), 0U);  // only on the link it arrived on
}

TEST_F(DaemonTest, RequestForAnExcludedMethodGetsStatusTwoAndTheAppIsNotCalled) {
  StartDaemon();
  auto reg = MakeRegistration(dp::Role::kProvide, "svc.A", 3, {"l1"}, 2);
  reg.elements[2].available = false;  // method 1 excluded
  auto* c = NewClient();
  ASSERT_TRUE(c->Register(reg).has_value());
  radios_["l1"]->Inject(PeerFrame(kBaseA + 2, ReqPayload(51, 3, kSys, kComp, 1)));
  ASSERT_TRUE(WaitSentWithId("l1", kBaseA + 3, 1));
  EXPECT_EQ(ExpandPayload(SentWithId("l1", kBaseA + 3).at(0).payload, 8)[6], 2);
  EXPECT_TRUE(c->NoDelivery(dp::ElementKind::kRequest, 200));
  // The first rejection is logged (once per instance, not once per frame).
  radios_["l1"]->Inject(PeerFrame(kBaseA + 2, ReqPayload(52, 3, kSys, kComp, 1)));
  ASSERT_TRUE(WaitSentWithId("l1", kBaseA + 3, 2));
  EXPECT_EQ(logs_.Count(LogLevel::kWarn, "excluded element"), 1) << logs_.Dump();
}

TEST_F(DaemonTest, ExcludedVoidRequestIsRejectedSilently) {
  StartDaemon();
  auto reg = MakeRegistration(dp::Role::kProvide, "svc.A", 3, {"l1"}, 2);
  reg.elements[3].available = false;  // void method excluded
  auto* c = NewClient();
  ASSERT_TRUE(c->Register(reg).has_value());
  radios_["l1"]->Inject(PeerFrame(kBaseA + 5, ReqPayload(52, 3, kSys, kComp, 1)));
  EXPECT_TRUE(c->NoDelivery(dp::ElementKind::kRequest, 200));
  EXPECT_EQ(radios_["l1"]->sent_count(), 0U);
}

TEST_F(DaemonTest, RequestOfAnUnavailableMethodFailsImmediatelyOnTheProxy) {
  StartDaemon();
  auto reg = MakeRegistration(dp::Role::kRequire, "svc.A", 3, {"l1"}, 1);
  reg.elements[2].available = false;
  auto* c = NewClient();
  ASSERT_TRUE(c->Register(reg).has_value());
  c->Send(testing::Request(1, testing::kMethod, ReqPayload(1, 3, kPeerSys, kPeerComp, 1)));
  const auto err = c->Next<dp::SendError>();
  ASSERT_TRUE(err.has_value());
  EXPECT_EQ(err->code, dp::SendErrc::kUnavailable);
  EXPECT_EQ(err->seq, 1);
  EXPECT_EQ(err->element_id, testing::kMethod);
  EXPECT_EQ(radios_["l1"]->sent_count(), 0U);
}

TEST_F(DaemonTest, RequestWithoutAnyReadyLinkFailsImmediately) {
  radios_["l1"]->FailStarts(100000, true);
  radios_["l2"]->FailStarts(100000, true);
  StartDaemon(/*wait_links=*/false);
  auto* c = RegisterOk(dp::Role::kRequire, "svc.A", 3, {"l1", "l2"}, 1);
  ASSERT_TRUE(c->WaitLinkStatus("l1", false));
  const auto t0 = Clock::now();
  c->Send(testing::Request(1, testing::kMethod, ReqPayload(8, 3, kPeerSys, kPeerComp, 1)));
  const auto err = c->Next<dp::SendError>(500);
  ASSERT_TRUE(err.has_value());
  EXPECT_EQ(err->code, dp::SendErrc::kNoLink);
  EXPECT_EQ(err->seq, 8);
  EXPECT_LT(std::chrono::duration_cast<milliseconds>(Clock::now() - t0).count(), 500);
}

TEST_F(DaemonTest, RequestGoesOnlyToReadyLinks) {
  radios_["l2"]->FailStarts(100000, true);
  StartDaemon(/*wait_links=*/false);
  ASSERT_TRUE(WaitLinkState("l1", RadioLinkState::kRunning, milliseconds(3000)));
  auto* c = RegisterOk(dp::Role::kRequire, "svc.A", 3, {"l1", "l2"}, 1);
  c->Send(testing::Request(1, testing::kMethod, ReqPayload(8, 3, kPeerSys, kPeerComp, 1)));
  ASSERT_TRUE(WaitSentWithId("l1", kBaseA + 2, 1));
  EXPECT_EQ(SentWithId("l2", kBaseA + 2).size(), 0U);
  EXPECT_TRUE(c->NothingArrives<dp::SendError>(100));
}

TEST_F(DaemonTest, VoidRequestIsSentAndFireAndForget) {
  StartDaemon();
  auto* c = RegisterOk(dp::Role::kRequire, "svc.A", 3, {"l1"}, 1);
  c->Send(testing::Request(1, testing::kVoidMethod, ReqPayload(5, 3, kPeerSys, kPeerComp, 4)));
  ASSERT_TRUE(WaitSentWithId("l1", kBaseA + 5, 1));
  EXPECT_TRUE(c->NothingArrives<dp::SendError>(100));
}

TEST_F(DaemonTest, MalformedRequestsAreRejectedWithErrors) {
  StartDaemon();
  auto* c = RegisterOk(dp::Role::kRequire, "svc.A", 3, {"l1"}, 1);
  c->Send(testing::Request(1, testing::kMethod, Bytes{1, 2}));                       // wrong length
  c->Send(testing::Request(1, 0x8001, EventPayload(1, 3, 1)));                       // an event sent as a Request
  c->Send(testing::Request(7, testing::kMethod, ReqPayload(1, 3, 0, 0, 0)));         // unknown handle
  for (int i = 0; i < 3; ++i) {
    const auto e = c->Next<dp::SendError>();
    ASSERT_TRUE(e.has_value());
    EXPECT_EQ(e->code, dp::SendErrc::kUnknownElement);
  }
}

TEST_F(DaemonTest, ProviderCannotSendRequestsAndProxyCannotSendEventsOrResponses) {
  StartDaemon();
  auto* provider = RegisterOk(dp::Role::kProvide, "svc.A", 3, {"l1"}, 1);
  auto* proxy = RegisterOk(dp::Role::kRequire, "svc.B", 3, {"l1"}, 1, kBaseB);
  provider->Send(testing::Request(1, testing::kMethod, ReqPayload(1, 3, 0, 0, 0)));
  EXPECT_EQ(provider->Next<dp::SendError>()->code, dp::SendErrc::kUnknownElement);
  proxy->Send(testing::Event(1, testing::kEv1, EventPayload(1, 3, 1)));
  proxy->Send(testing::Response(1, testing::kMethod, RespPayload(1, 3, 0, 0, 0, 0)));
  std::this_thread::sleep_for(milliseconds(150));
  EXPECT_EQ(radios_["l1"]->sent_count(), 0U);
  EXPECT_GE(core_->counters().send_rejected, 3U);
}

// ============================================================== redundancy and dedup

TEST_F(DaemonTest, TwoLinksDeliverTheSameEventExactlyOnce) {
  StartDaemon();
  auto* c = RegisterOk(dp::Role::kRequire, "svc.A", 3, {"l1", "l2"}, 1);
  const auto frame = PeerFrame(kBaseA + 1, EventPayload(5, 3, 55));
  radios_["l1"]->Inject(frame);
  radios_["l2"]->Inject(frame);
  ASSERT_TRUE(c->NextDelivery(dp::ElementKind::kEvent).has_value());
  EXPECT_TRUE(c->NoDelivery(dp::ElementKind::kEvent, 300));
  EXPECT_EQ(core_->counters().duplicates, 1U);
}

TEST_F(DaemonTest, OneLinkDelayedStillYieldsASingleDelivery) {
  StartDaemon();
  auto* c = RegisterOk(dp::Role::kRequire, "svc.A", 3, {"l1", "l2"}, 1);
  const auto frame = PeerFrame(kBaseA + 1, EventPayload(5, 3, 55));
  radios_["l1"]->Inject(frame);
  ASSERT_TRUE(c->NextDelivery(dp::ElementKind::kEvent).has_value());
  std::this_thread::sleep_for(milliseconds(1200));  // a copy from the second link after a long delay
  radios_["l2"]->Inject(frame);
  EXPECT_TRUE(c->NoDelivery(dp::ElementKind::kEvent, 300));
}

TEST_F(DaemonTest, SameSeqWithDifferentPayloadIsNotADuplicate) {
  StartDaemon();
  auto* c = RegisterOk(dp::Role::kRequire, "svc.A", 3, {"l1", "l2"}, 1);
  radios_["l1"]->Inject(PeerFrame(kBaseA + 1, EventPayload(5, 3, 1)));
  ASSERT_TRUE(c->NextDelivery(dp::ElementKind::kEvent).has_value());
  // sender restart: the same seq, different content
  radios_["l2"]->Inject(PeerFrame(kBaseA + 1, EventPayload(5, 3, 2)));
  EXPECT_TRUE(c->NextDelivery(dp::ElementKind::kEvent).has_value());
}

TEST_F(DaemonTest, DedupWindowFollowsTheRegistrationTtl) {
  StartDaemon();
  auto reg = MakeRegistration(dp::Role::kRequire, "svc.A", 3, {"l1", "l2"}, 1);
  reg.dedup_ttl_ms = 200;
  auto* c = NewClient();
  ASSERT_TRUE(c->Register(reg).has_value());
  const auto frame = PeerFrame(kBaseA + 1, EventPayload(5, 3, 55));
  radios_["l1"]->Inject(frame);
  ASSERT_TRUE(c->NextDelivery(dp::ElementKind::kEvent).has_value());
  std::this_thread::sleep_for(milliseconds(400));
  radios_["l2"]->Inject(frame);  // after the TTL it is a new message
  EXPECT_TRUE(c->NextDelivery(dp::ElementKind::kEvent).has_value());
}

TEST_F(DaemonTest, TwoLinksExecuteARequestOnceAndTheFirstResponseWins) {
  StartDaemon();
  auto* provider = RegisterOk(dp::Role::kProvide, "svc.A", 3, {"l1", "l2"}, 2);
  const auto req = PeerFrame(kBaseA + 2, ReqPayload(61, 3, kSys, kComp, 1));
  radios_["l1"]->Inject(req);
  radios_["l2"]->Inject(req);
  ASSERT_TRUE(provider->NextDelivery(dp::ElementKind::kRequest).has_value());
  EXPECT_TRUE(provider->NoDelivery(dp::ElementKind::kRequest, 300));
  provider->Send(testing::Response(2, testing::kMethod, RespPayload(61, 3, kPeerSys, kPeerComp, 0, 1)));
  // the RESP goes out on all links of the instance
  EXPECT_TRUE(WaitSentWithId("l1", kBaseA + 3, 1));
  EXPECT_TRUE(WaitSentWithId("l2", kBaseA + 3, 1));

  auto* proxy = RegisterOk(dp::Role::kRequire, "svc.A", 3, {"l1", "l2"}, 3);
  const auto resp = PeerFrame(kBaseA + 3, RespPayload(70, 3, kSys, kComp, 0, 9));
  radios_["l1"]->Inject(resp);
  radios_["l2"]->Inject(resp);
  ASSERT_TRUE(proxy->NextDelivery(dp::ElementKind::kResponse).has_value());
  EXPECT_TRUE(proxy->NoDelivery(dp::ElementKind::kResponse, 300));
}

TEST_F(DaemonTest, RedundantRequestGoesToEveryReadyLinkAndOneLinkDownKeepsTrafficFlowing) {
  StartDaemon();
  auto* c = RegisterOk(dp::Role::kRequire, "svc.A", 3, {"l1", "l2"}, 1);
  c->Send(testing::Request(1, testing::kMethod, ReqPayload(1, 3, kPeerSys, kPeerComp, 1)));
  ASSERT_TRUE(WaitSentWithId("l1", kBaseA + 2, 1));
  ASSERT_TRUE(WaitSentWithId("l2", kBaseA + 2, 1));
  radios_["l1"]->Fatal("l1 died");
  ASSERT_TRUE(c->WaitLinkStatus("l1", false));
  c->Send(testing::Request(1, testing::kMethod, ReqPayload(2, 3, kPeerSys, kPeerComp, 1)));
  ASSERT_TRUE(WaitSentWithId("l2", kBaseA + 2, 2));
  EXPECT_EQ(SentWithId("l1", kBaseA + 2).size(), 1U);
  EXPECT_TRUE(c->NothingArrives<dp::SendError>(100));
}

// ============================================================== kBusy / restart / LinkStatus

TEST_F(DaemonTest, FatalDriverErrorRestartsTheLinkAfterTheDelayAndAppsAreNotified) {
  StartDaemon();
  auto* c = RegisterOk(dp::Role::kProvide, "svc.A", 3, {"l1"}, 1);
  ASSERT_TRUE(c->WaitLinkStatus("l1", true));
  const auto t0 = Clock::now();
  radios_["l1"]->Fatal("radio failure");
  ASSERT_TRUE(c->WaitLinkStatus("l1", false));
  ASSERT_TRUE(c->WaitLinkStatus("l1", true));
  EXPECT_GE(std::chrono::duration_cast<milliseconds>(Clock::now() - t0).count(), 140);
  EXPECT_EQ(radios_["l1"]->start_count(), 2);
  EXPECT_EQ(radios_["l1"]->stop_count(), 1);
  // after the restart traffic flows normally
  c->Send(testing::Event(1, testing::kEv1, EventPayload(1, 3, 5)));
  EXPECT_TRUE(WaitSentWithId("l1", kBaseA + 1, 1));
  radios_["l1"]->Inject(PeerFrame(kBaseA + 2, ReqPayload(1, 3, kSys, kComp, 1)));
  EXPECT_TRUE(c->NextDelivery(dp::ElementKind::kRequest).has_value());
}

TEST_F(DaemonTest, LinkStatusReflectsStartFailuresOfTheDriver) {
  radios_["l2"]->FailStarts(2, true);
  StartDaemon(/*wait_links=*/false);
  auto* c = RegisterOk(dp::Role::kProvide, "svc.A", 3, {"l1", "l2"}, 1);
  EXPECT_TRUE(c->WaitLinkStatus("l1", true));
  EXPECT_TRUE(c->WaitLinkStatus("l2", true, 5000));  // after two failed Start() calls
  EXPECT_GE(core_->FindLink("l2")->counters().start_errors, 2U);
}

// ============================================================== HEARTBEAT and signature

TEST_F(DaemonTest, SigningInitErrorKeepsTheLinkSilentAndNotReady) {
  cfg_.links[0].signing_enabled = true;
  cfg_.links[0].key_file = TmpPath("daemon_missing_key");
  StartDaemon(/*wait_links=*/false);
  EXPECT_EQ(core_->FindLink("l1")->state(), RadioLinkState::kInitError);
  auto* c = RegisterOk(dp::Role::kRequire, "svc.A", 3, {"l1"}, 1);
  c->Send(testing::Request(1, testing::kMethod, ReqPayload(1, 3, kPeerSys, kPeerComp, 1)));
  EXPECT_EQ(c->Next<dp::SendError>()->code, dp::SendErrc::kNoLink);
  EXPECT_EQ(radios_["l1"]->sent_count(), 0U);
}

// ============================================================== start / stop

TEST_F(DaemonTest, UnusableSocketPathIsAStartError) {
  cfg_.socket_path = "/nonexistent_dir_srp/sock";
  MakeDaemon();
  const auto r = core_->Start();
  ASSERT_FALSE(r.HasValue());
  EXPECT_NE(r.Error().SupportData().find("IPC socket"), std::string::npos);
}

TEST_F(DaemonTest, StartAfterStopOnTheSameObjectRunsTheDaemonAgain) {
  StartDaemon();
  core_->Stop();
  ASSERT_TRUE(core_->Start().HasValue());
  for (const auto& l : cfg_.links) {
    ASSERT_TRUE(WaitLinkState(l.name, RadioLinkState::kRunning, milliseconds(3000))) << l.name;
  }
  auto* c = RegisterOk(dp::Role::kProvide, "svc.A", 3, {"l1"}, 1);
  c->Send(testing::Event(1, testing::kEv1, EventPayload(1, 3, 5)));
  EXPECT_TRUE(WaitSentWithId("l1", kBaseA + 1, 1));
}

TEST_F(DaemonTest, StopIsIdempotentAndDisconnectsApplications) {
  StartDaemon();
  auto* c = RegisterOk(dp::Role::kProvide, "svc.A", 3, {"l1"}, 1);
  core_->Stop();
  core_->Stop();
  EXPECT_TRUE(WaitFor([&] { return c->closed_by_peer(); }));
  EXPECT_EQ(Access::InstanceCount(core_.get()), 0U);
  EXPECT_EQ(radios_["l1"]->stop_count(), 1);
}

TEST_F(DaemonTest, RestartedDaemonAcceptsApplicationsThatReconnectAndRegisterAgain) {
  StartDaemon();
  {
    auto* c = RegisterOk(dp::Role::kProvide, "svc.A", 3, {"l1"}, 1);
    core_->Stop();
    ASSERT_TRUE(WaitFor([&] { return c->closed_by_peer(); }));
  }
  clients_.clear();
  core_.reset();
  StartDaemon();  // the same drivers and socket
  auto* c = RegisterOk(dp::Role::kProvide, "svc.A", 3, {"l1"}, 1);
  c->Send(testing::Event(1, testing::kEv1, EventPayload(1, 3, 5)));
  EXPECT_TRUE(WaitSentWithId("l1", kBaseA + 1, 1));
}

TEST_F(DaemonTest, SlowApplicationThatStopsReadingIsDisconnectedNotBlockingOthers) {
  StartDaemon();
  auto* healthy = RegisterOk(dp::Role::kRequire, "svc.A", 3, {"l1"}, 1, kBaseA);
  // a raw client that registers and stops reading
  auto stuck = dp::MessageSocket::Connect(path_);
  ASSERT_TRUE(stuck.HasValue());
  auto sock = stuck.Value();
  ASSERT_TRUE(sock.Send(MakeRegistration(dp::Role::kRequire, "svc.B", 3, {"l1"}, 1, kBaseB)).HasValue());
  ASSERT_TRUE(WaitFor([&] { return Access::InstanceCount(core_.get()) == 2; }));
  for (std::uint16_t i = 0; i < 3000 && Access::SessionCount(core_.get()) == 2; ++i) {
    radios_["l1"]->Inject(PeerFrame(kBaseB + 1, EventPayload(i, 3, i)));
  }
  ASSERT_TRUE(WaitFor([&] { return Access::SessionCount(core_.get()) == 1; }, 10000))
      << "an application that does not read gets disconnected";
  radios_["l1"]->Inject(PeerFrame(kBaseA + 1, EventPayload(1, 3, 1)));
  EXPECT_TRUE(healthy->NextDelivery(dp::ElementKind::kEvent, 3000).has_value());
  EXPECT_GE(core_->counters().deliver_failed, 1U);
}

// ============================================================== isolation of slow applications

TEST_F(DaemonTest, SlowApplicationDoesNotDelayDeliveryToAnotherApplicationOnTheSameLink) {
  StartDaemon();
  auto* healthy = RegisterOk(dp::Role::kRequire, "svc.A", 3, {"l1"}, 1, kBaseA);
  // A raw client that registers on the same link and never reads its socket.
  auto stuck = dp::MessageSocket::Connect(path_);
  ASSERT_TRUE(stuck.HasValue());
  auto sock = stuck.Value();
  ASSERT_TRUE(sock.Send(MakeRegistration(dp::Role::kRequire, "svc.B", 3, {"l1"}, 1, kBaseB)).HasValue());
  ASSERT_TRUE(WaitFor([&] { return Access::InstanceCount(core_.get()) == 2; }));
  // Enough frames to fill the socket buffer of the stuck application, then one for the healthy one.
  for (std::uint16_t i = 0; i < 400; ++i) {
    radios_["l1"]->Inject(PeerFrame(kBaseB + 1, EventPayload(i, 3, i)));
  }
  const auto t0 = Clock::now();
  radios_["l1"]->Inject(PeerFrame(kBaseA + 1, EventPayload(1, 3, 1)));
  ASSERT_TRUE(healthy->NextDelivery(dp::ElementKind::kEvent, 3000).has_value());
  const auto ms = std::chrono::duration_cast<milliseconds>(Clock::now() - t0).count();
  // A blocking delivery to the stuck application would hold the link RX thread for its 500 ms
  // send timeout before the frame of the healthy application is even looked at.
  EXPECT_LT(ms, 300);
}

TEST_F(DaemonTest, LinkRestartIsNotDelayedByASlowApplicationThatUsesTheLink) {
  StartDaemon();
  auto stuck = dp::MessageSocket::Connect(path_);
  ASSERT_TRUE(stuck.HasValue());
  auto sock = stuck.Value();
  ASSERT_TRUE(sock.Send(MakeRegistration(dp::Role::kRequire, "svc.B", 3, {"l1"}, 1, kBaseB)).HasValue());
  ASSERT_TRUE(WaitFor([&] { return Access::InstanceCount(core_.get()) == 1; }));
  for (std::uint16_t i = 0; i < 400; ++i) {
    radios_["l1"]->Inject(PeerFrame(kBaseB + 1, EventPayload(i, 3, i)));
  }
  // A link state change is reported to every application that uses the link: the stuck one must
  // not hold up the supervisor thread that restarts the driver (restart delay 150 ms).
  const auto t0 = Clock::now();
  radios_["l1"]->Fatal("link failure");
  ASSERT_TRUE(WaitFor([&] { return radios_["l1"]->start_count() == 2; }, 3000));
  EXPECT_LT(std::chrono::duration_cast<milliseconds>(Clock::now() - t0).count(), 400);
}

// ============================================================== signing replay window

TEST_F(DaemonTest, FrameForAForeignInstanceDoesNotMoveTheReplayWindowOfTheLink) {
  const auto key = TmpPath("daemon_key_window_" + std::to_string(::getpid()));
  WriteKey(key, 10);
  cfg_.links[0].signing_enabled = true;
  cfg_.links[0].key_file = key;
  cfg_.links[0].signing_link_id = 6;
  StartDaemon();
  auto* proxy = RegisterOk(dp::Role::kRequire, "svc.A", 3, {"l1"}, 1);
  std::array<std::uint8_t, 32> k{};
  for (int i = 0; i < 32; ++i) k[i] = static_cast<std::uint8_t>(10 + i);
  const auto now = ara::com::mavlink::SystemTimestamp10us();
  const ara::com::mavlink::SigningParams far_future{k, 5, now + 600 * ara::com::mavlink::kTimestampUnitsPerSecond};
  const ara::com::mavlink::SigningParams current{k, 5, now};
  // An event for instance 9, which nobody registered: validly signed, but not accepted by anyone.
  radios_["l1"]->Inject(BuildFrame(2, 1, kPeerSys, kPeerComp, kBaseA + 1, EventPayload(1, 9, 1), testing::kCrc,
                                   &far_future));
  EXPECT_TRUE(proxy->NoDelivery(dp::ElementKind::kEvent, 250));
  // A genuine frame for the registered instance with a much smaller timestamp is still new: the
  // frame above must not have advanced the window of the stream.
  radios_["l1"]->Inject(BuildFrame(2, 2, kPeerSys, kPeerComp, kBaseA + 1, EventPayload(2, 3, 2), testing::kCrc,
                                   &current));
  EXPECT_TRUE(proxy->NextDelivery(dp::ElementKind::kEvent).has_value());
  // Unsigned frames are still refused, and a repeated timestamp is a replay.
  radios_["l1"]->Inject(PeerFrame(kBaseA + 1, EventPayload(3, 3, 3)));
  radios_["l1"]->Inject(BuildFrame(2, 3, kPeerSys, kPeerComp, kBaseA + 1, EventPayload(4, 3, 4), testing::kCrc,
                                   &current));
  EXPECT_TRUE(proxy->NoDelivery(dp::ElementKind::kEvent, 250));
  std::remove(key.c_str());
}

}  // namespace
}  // namespace mavlink_daemon
}  // namespace srp
