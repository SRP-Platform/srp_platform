/**
 * Copyright (c) 2024
 *
 * @file test_daemon_client.cc
 * @brief MavlinkDaemon client (DaemonClient + MavlinkEndpoint) against a fake daemon.
 */
#include <gtest/gtest.h>
#include <unistd.h>

#include <atomic>
#include <iostream>
#include <set>
#include <stdexcept>
#include <chrono>  // NOLINT
#include <condition_variable>  // NOLINT
#include <functional>
#include <memory>
#include <mutex>  // NOLINT
#include <string>
#include <thread>  // NOLINT
#include <vector>

#include "ara/com/mavlink/client/daemon_client.h"
#include "ara/com/mavlink/client/testing/fake_daemon.h"
#include "ara/com/mavlink/mavlink_codec.h"

namespace ara {
namespace com {
namespace mavlink {
namespace {
using std::chrono::milliseconds;
namespace m = ara::core::model;
using Field = m::ModelMavlinkField;
using T = Field::Type;
using R = Field::Role;

constexpr std::uint16_t kEvent = 0x8001;
constexpr std::uint16_t kMethod = 1;
constexpr std::uint16_t kNoRespMethod = 2;

m::ModelMavlinkMessage Msg(std::uint32_t id, const std::string& name, std::vector<Field> f) {
  m::ModelMavlinkMessage msg;
  msg.msg_id = id;
  msg.crc_extra = static_cast<std::uint8_t>(id & 0xFF);
  msg.name = name;
  msg.fields = std::move(f);
  return msg;
}

m::ModelMavlinkOptions Options(const std::vector<std::string>& exclude = {}, double rate = 0) {
  m::ModelMavlinkOptions o;
  o.exclude = exclude;
  m::ModelMavlinkElement ev;
  ev.id = kEvent;
  ev.is_event = true;
  ev.enabled = true;
  ev.name = "Telemetry";
  ev.req = Msg(50001, "TELEMETRY",
               {{"seq", T::kU16, 0, R::kSeq, false},
                {"instance", T::kU16, 0, R::kInstance, false},
                {"value", T::kU16, 0, R::kPayload, true}});
  ev.max_rate_hz = rate;
  o.elements.push_back(ev);
  m::ModelMavlinkElement me;
  me.id = kMethod;
  me.enabled = true;
  me.name = "SetMode";
  me.timeout_ms = 300;
  me.req = Msg(50002, "SET_MODE_REQ",
               {{"seq", T::kU16, 0, R::kSeq, false},
                {"instance", T::kU16, 0, R::kInstance, false},
                {"target_system", T::kU8, 0, R::kTargetSystem, false},
                {"target_component", T::kU8, 0, R::kTargetComponent, false},
                {"arg", T::kU8, 0, R::kPayload, false}});
  me.has_resp = true;
  me.resp = Msg(50003, "SET_MODE_RESP",
                {{"seq", T::kU16, 0, R::kSeq, false},
                 {"instance", T::kU16, 0, R::kInstance, false},
                 {"target_system", T::kU8, 0, R::kTargetSystem, false},
                 {"target_component", T::kU8, 0, R::kTargetComponent, false},
                 {"status", T::kU8, 0, R::kStatus, false},
                 {"result", T::kU8, 0, R::kPayload, false}});
  o.elements.push_back(me);
  m::ModelMavlinkElement nr;
  nr.id = kNoRespMethod;
  nr.enabled = true;
  nr.name = "Reset";
  nr.req = Msg(50004, "RESET_REQ",
               {{"seq", T::kU16, 0, R::kSeq, false},
                {"instance", T::kU16, 0, R::kInstance, false},
                {"target_system", T::kU8, 0, R::kTargetSystem, false},
                {"target_component", T::kU8, 0, R::kTargetComponent, false}});
  o.elements.push_back(nr);
  return o;
}

m::ModelMavlink Skeleton(const m::ModelMavlinkOptions& o, std::uint16_t inst = 2) {
  return m::ModelMavlink::CreatSkeletonItem("svc", inst, 0, 0, {"lora", "radio_b"}, o).Value();
}
m::ModelMavlink Proxy(const m::ModelMavlinkOptions& o, std::uint16_t inst = 2) {
  return m::ModelMavlink::CreatProxyItem("svc", inst, 7, 9, {"lora"}, o).Value();
}

class DaemonClientTest : public ::testing::Test {
 protected:
  void SetUp() override {
    path_ = "/tmp/srp_mav_cli_" + std::to_string(::getpid()) + ".sock";
    daemon_ = std::make_unique<testing::FakeDaemon>(path_);
    ASSERT_TRUE(daemon_->Start());
    DaemonClient::Options o;
    o.socket_path = path_;
    o.reconnect_interval = milliseconds(50);
    o.poll_ms = 20;
    o.register_retry = milliseconds(100);
    client_ = std::make_unique<DaemonClient>(o);
  }
  void TearDown() override {
    client_->Shutdown();
    daemon_->Stop();
  }
  static bool WaitFor(const std::function<bool()>& pred, milliseconds t = milliseconds(3000)) {
    const auto end = std::chrono::steady_clock::now() + t;
    while (std::chrono::steady_clock::now() < end) {
      if (pred()) return true;
      std::this_thread::sleep_for(milliseconds(5));
    }
    return pred();
  }
  std::string path_;
  std::unique_ptr<testing::FakeDaemon> daemon_;
  std::unique_ptr<DaemonClient> client_;
};

TEST_F(DaemonClientTest, RegistersInstanceWithElementTable) {
  const auto ep = client_->AttachEndpoint(Skeleton(Options({"Reset"}, 20.0)), true);
  ASSERT_TRUE(daemon_->WaitRegistrations(1, milliseconds(2000)));
  const auto regs = daemon_->registrations();
  ASSERT_EQ(regs.size(), 1U);
  const auto& r = regs[0];
  EXPECT_EQ(r.role, daemon::Role::kProvide);
  EXPECT_EQ(r.com_id, "svc");
  EXPECT_EQ(r.instance, 2);
  EXPECT_EQ((r.links), (std::vector<std::string>{"lora", "radio_b"}));
  ASSERT_EQ(r.elements.size(), 3U);
  const auto& ev = r.elements[0];
  EXPECT_EQ(ev.id, kEvent);
  EXPECT_TRUE(ev.is_event);
  EXPECT_TRUE(ev.available);
  EXPECT_EQ(ev.min_interval_ms, 50U);  // 20 Hz
  EXPECT_EQ(ev.req.msg_id, 50001U);
  EXPECT_EQ(ev.req.crc_extra, 50001U & 0xFF);
  EXPECT_EQ(ev.req.full_len, 6);
  EXPECT_EQ(ev.req.seq_offset, 0);
  EXPECT_EQ(ev.req.instance_offset, 2);
  EXPECT_EQ(ev.req.target_system_offset, daemon::kNoOffset);
  const auto& me = r.elements[1];
  EXPECT_TRUE(me.has_resp);
  EXPECT_EQ(me.timeout_ms, 300U);
  EXPECT_EQ(me.resp.msg_id, 50003U);
  EXPECT_EQ(me.resp.status_offset, 6);
  EXPECT_FALSE(r.elements[2].available);  // exclude
  ASSERT_TRUE(WaitFor([&] { return ep->IsRegistered() && ep->AnyLinkReady(); }));
  EXPECT_TRUE(ep->IsEventAvailable(kEvent));
  EXPECT_FALSE(ep->IsEventAvailable(kMethod));
}

TEST_F(DaemonClientTest, RegistrationUsesServiceNameSharedByAllInstances) {
  auto o = Options();
  o.service_name = "srp.example.Svc";
  const auto a = client_->AttachEndpoint(Skeleton(o, 2), true);
  const auto b = client_->AttachEndpoint(Proxy(o, 3), false);
  ASSERT_TRUE(daemon_->WaitRegistrations(2, milliseconds(2000)));
  const auto regs = daemon_->registrations();
  EXPECT_EQ(regs[0].com_id, "srp.example.Svc");
  EXPECT_EQ(regs[1].com_id, "srp.example.Svc");  // instances and roles of one service share one identity
  EXPECT_NE(regs[0].instance, regs[1].instance);
}

TEST_F(DaemonClientTest, PublishEventSendsFullPayloadAndExcludedIsNoOp) {
  auto o = Options({"Telemetry"});
  const auto excluded = client_->AttachEndpoint(Skeleton(o, 3), true);
  ASSERT_TRUE(WaitFor([&] { return excluded->IsRegistered(); }));
  ASSERT_TRUE(excluded->PublishEvent(kEvent, {0x12, 0x34}).HasValue());
  EXPECT_EQ(excluded->counters().tx_excluded, 1U);
  EXPECT_FALSE(daemon_->WaitMessage(milliseconds(100)).has_value());

  const auto ep = client_->AttachEndpoint(Skeleton(Options(), 2), true);
  ASSERT_TRUE(WaitFor([&] { return ep->IsRegistered(); }));
  ASSERT_TRUE(ep->PublishEvent(kEvent, {0x12, 0x34}).HasValue());  // SOME/IP BE
  const auto msg = daemon_->WaitMessage(milliseconds(1000));
  ASSERT_TRUE(msg.has_value());
  const auto& ev = std::get<daemon::ElementPayload>(*msg);
  EXPECT_EQ(ev.kind, daemon::ElementKind::kEvent);
  EXPECT_EQ(ev.element_id, kEvent);
  ASSERT_EQ(ev.payload.size(), 6U);
  EXPECT_EQ(ev.payload[2], 2);  // instance LE
  EXPECT_EQ(ev.payload[4], 0x34);  // value LE
  EXPECT_EQ(ev.payload[5], 0x12);
}

TEST_F(DaemonClientTest, MethodTimesOutAndRemoteStatusMapsToError) {
  const auto ep = client_->AttachEndpoint(Proxy(Options()), false);
  ASSERT_TRUE(WaitFor([&] { return ep->AnyLinkReady(); }));
  const auto start = std::chrono::steady_clock::now();
  const auto r = ep->CallMethod(kMethod, {1});
  EXPECT_FALSE(r.HasValue());
  EXPECT_GE(std::chrono::steady_clock::now() - start, milliseconds(280));
  EXPECT_EQ(ep->counters().call_timeouts, 1U);
}

TEST_F(DaemonClientTest, DaemonSendErrorFailsCallImmediately) {
  const auto ep = client_->AttachEndpoint(Proxy(Options()), false);
  ASSERT_TRUE(WaitFor([&] { return ep->AnyLinkReady(); }));
  const auto handle = daemon_->registrations()[0].handle;
  std::jthread responder([&](std::stop_token) {
    const auto msg = daemon_->WaitMessage(milliseconds(2000));
    ASSERT_TRUE(msg.has_value());
    const auto& req = std::get<daemon::ElementPayload>(*msg);
    daemon::SendError e;
    e.handle = handle;
    e.element_id = req.element_id;
    e.seq = static_cast<std::uint16_t>(req.payload[0] | (req.payload[1] << 8));
    e.code = daemon::SendErrc::kNoLink;
    EXPECT_TRUE(daemon_->SendToApp(e));
  });
  const auto start = std::chrono::steady_clock::now();
  const auto r = ep->CallMethod(kMethod, {1});
  EXPECT_FALSE(r.HasValue());
  EXPECT_LT(std::chrono::steady_clock::now() - start, milliseconds(250));
  EXPECT_EQ(ep->counters().call_rejected, 1U);
}

TEST_F(DaemonClientTest, NoDaemonMethodFailsImmediately) {
  daemon_->Stop();
  const auto ep = client_->AttachEndpoint(Proxy(Options()), false);
  const auto start = std::chrono::steady_clock::now();
  const auto r = ep->CallMethod(kMethod, {1});
  EXPECT_FALSE(r.HasValue());
  EXPECT_LT(std::chrono::steady_clock::now() - start, milliseconds(50));
  EXPECT_EQ(ep->counters().call_unavailable, 1U);
  // an event without a daemon: no error, counted in tx_dropped
  const auto sk = client_->AttachEndpoint(Skeleton(Options(), 5), true);
  EXPECT_TRUE(sk->PublishEvent(kEvent, {0, 1}).HasValue());
  EXPECT_EQ(sk->counters().tx_dropped, 1U);
}

TEST_F(DaemonClientTest, NoReadyLinkMethodFailsImmediately) {
  daemon_->SetLinksReady(false);
  const auto ep = client_->AttachEndpoint(Proxy(Options()), false);
  ASSERT_TRUE(WaitFor([&] { return ep->IsRegistered(); }));
  const auto start = std::chrono::steady_clock::now();
  EXPECT_FALSE(ep->CallMethod(kMethod, {1}).HasValue());
  EXPECT_LT(std::chrono::steady_clock::now() - start, milliseconds(50));
  // the link becomes ready after a LinkStatus
  daemon::LinkStatus ls;
  ls.handle = daemon_->registrations()[0].handle;
  ls.link = "lora";
  ls.ready = true;
  ASSERT_TRUE(daemon_->SendToApp(ls));
  EXPECT_TRUE(WaitFor([&] { return ep->AnyLinkReady(); }));
  ls.ready = false;
  ASSERT_TRUE(daemon_->SendToApp(ls));
  EXPECT_TRUE(WaitFor([&] { return !ep->AnyLinkReady(); }));
}

TEST_F(DaemonClientTest, ReRegistersAfterDaemonRestartAndTrafficResumes) {
  const auto sk = client_->AttachEndpoint(Skeleton(Options(), 2), true);
  const auto px = client_->AttachEndpoint(Proxy(Options(), 4), false);
  ASSERT_TRUE(WaitFor([&] { return sk->AnyLinkReady() && px->AnyLinkReady(); }));
  ASSERT_EQ(daemon_->registrations().size(), 2U);

  std::jthread pending_call([&](std::stop_token) {
    // a call in progress at the restart fails with an error instead of hanging until the timeout
    const auto start = std::chrono::steady_clock::now();
    EXPECT_FALSE(px->CallMethod(kMethod, {1}).HasValue());
    EXPECT_LT(std::chrono::steady_clock::now() - start, milliseconds(280));
  });
  ASSERT_TRUE(daemon_->WaitMessage(milliseconds(1000)).has_value());  // the REQ reached the daemon
  daemon_->Stop();                                                    // "kill the daemon"
  ASSERT_TRUE(WaitFor([&] { return !sk->IsRegistered() && !px->IsRegistered(); }));
  EXPECT_FALSE(px->CallMethod(kMethod, {1}).HasValue());  // without a daemon: an immediate error
  pending_call.join();

  const auto before = std::chrono::steady_clock::now();
  ASSERT_TRUE(daemon_->Start());
  ASSERT_TRUE(daemon_->WaitRegistrations(4, milliseconds(5000)));  // both instances again
  ASSERT_TRUE(WaitFor([&] { return sk->AnyLinkReady() && px->AnyLinkReady(); }));
  EXPECT_LT(std::chrono::steady_clock::now() - before, milliseconds(5000));
  ASSERT_TRUE(sk->PublishEvent(kEvent, {0, 5}).HasValue());
  EXPECT_TRUE(daemon_->WaitMessage(milliseconds(1000)).has_value());
  EXPECT_GE(daemon_->connections(), 1U);  // the client reconnected
}

TEST_F(DaemonClientTest, RejectedRegistrationLeavesInstanceUnavailable) {
  daemon_->SetRegisterStatus(daemon::RegisterStatus::kConflictMsgId);
  const auto ep = client_->AttachEndpoint(Proxy(Options()), false);
  ASSERT_TRUE(daemon_->WaitRegistrations(1, milliseconds(2000)));
  std::this_thread::sleep_for(milliseconds(50));
  EXPECT_FALSE(ep->IsRegistered());
  EXPECT_FALSE(ep->CallMethod(kMethod, {1}).HasValue());
  // a msg_id conflict is permanent: no retries (until a restart / reconnect)
  std::this_thread::sleep_for(milliseconds(300));
  EXPECT_EQ(daemon_->registrations().size(), 1U);
}

TEST_F(DaemonClientTest, ConflictingInstanceIsRetriedUntilAccepted) {
  daemon_->SetRegisterStatus(daemon::RegisterStatus::kConflictInstance);
  const auto ep = client_->AttachEndpoint(Skeleton(Options()), true);
  ASSERT_TRUE(daemon_->WaitRegistrations(1, milliseconds(2000)));
  EXPECT_FALSE(ep->IsRegistered());
  daemon_->SetRegisterStatus(daemon::RegisterStatus::kOk);  // the old registration has been cleaned up
  EXPECT_TRUE(WaitFor([&] { return ep->IsRegistered(); }));
}

TEST_F(DaemonClientTest, SkeletonExecutesRequestAndSendsResponseToSender) {
  MavlinkEndpoint::Handlers handlers;
  handlers.method = [](std::uint16_t id, const std::vector<std::uint8_t>& p)
      -> ara::core::Result<std::vector<std::uint8_t>> {
    EXPECT_EQ(id, kMethod);
    return std::vector<std::uint8_t>{static_cast<std::uint8_t>(p[0] + 1)};
  };
  const auto ep = client_->AttachEndpoint(Skeleton(Options()), true, std::move(handlers));
  ASSERT_TRUE(WaitFor([&] { return ep->IsRegistered(); }));
  const auto handle = daemon_->registrations()[0].handle;
  const auto opts = Options();
  const MessageCodec req_codec{opts.elements[1].req};
  daemon::Delivery in;
  in.kind = daemon::ElementKind::kRequest;
  in.handle = handle;
  in.element_id = kMethod;
  in.sender_system = 7;
  in.sender_component = 9;
  in.link = "lora";
  in.payload = req_codec.Encode(MessageHeader{321, 2, 0, 0, 0}, {10}).Value();
  ASSERT_TRUE(daemon_->SendToApp(in));
  const auto msg = daemon_->WaitMessage(milliseconds(1000));
  ASSERT_TRUE(msg.has_value());
  const auto& resp = std::get<daemon::ElementPayload>(*msg);
  EXPECT_EQ(resp.kind, daemon::ElementKind::kResponse);
  EXPECT_EQ(resp.element_id, kMethod);
  const MessageCodec resp_codec{opts.elements[1].resp};
  const auto dec = resp_codec.Decode(resp.payload).Value();
  EXPECT_EQ(dec.header.seq, 321);
  EXPECT_EQ(dec.header.instance, 2);
  EXPECT_EQ(dec.header.target_system, 7);
  EXPECT_EQ(dec.header.target_component, 9);
  EXPECT_EQ(dec.header.status, 0);
  EXPECT_EQ(dec.someip, (std::vector<std::uint8_t>{11}));
}

TEST_F(DaemonClientTest, DuplicateHandleReplyForRegisteredInstanceIsIgnored) {
  // Double registration after a disconnect inside the RegisterEntry window: the daemon answers kOk, then
  // kDuplicateHandle.
  const auto ep = client_->AttachEndpoint(Skeleton(Options()), true);
  ASSERT_TRUE(WaitFor([&] { return ep->IsRegistered(); }));
  const auto regs = daemon_->registrations();
  ASSERT_EQ(regs.size(), 1U);
  daemon::RegisterReply dup;
  dup.handle = regs[0].handle;
  dup.status = daemon::RegisterStatus::kDuplicateHandle;
  ASSERT_TRUE(daemon_->SendToApp(dup));
  // The reply is processed by the IO thread before the following ones (socket order): after the LinkStatus
  // the state is settled.
  daemon::LinkStatus ls;
  ls.handle = regs[0].handle;
  ls.link = "lora";
  ls.ready = true;
  ASSERT_TRUE(daemon_->SendToApp(ls));
  std::this_thread::sleep_for(milliseconds(100));
  EXPECT_TRUE(ep->IsRegistered());
  // the instance is still "registered": Detach must unregister it in the daemon
  client_->DetachEndpoint(ep);
  EXPECT_TRUE(WaitFor([&] { return daemon_->unregistered() == 1U; }));
  EXPECT_EQ(daemon_->registrations().size(), 1U);  // no new registration
}

TEST_F(DaemonClientTest, DetachUnregistersAndShutdownIsIdempotent) {
  const auto ep = client_->AttachEndpoint(Skeleton(Options()), true);
  ASSERT_TRUE(WaitFor([&] { return ep->IsRegistered(); }));
  client_->DetachEndpoint(ep);
  EXPECT_TRUE(WaitFor([&] { return daemon_->unregistered() == 1U; }));
  EXPECT_FALSE(ep->IsRegistered());
  client_->Shutdown();
  client_->Shutdown();
  // after Shutdown the client starts again on the next Attach (Initialize/Deinitialize cycle)
  const auto again = client_->AttachEndpoint(Skeleton(Options()), true);
  EXPECT_TRUE(WaitFor([&] { return again->IsRegistered(); }));
}

TEST_F(DaemonClientTest, FullDaemonBufferDoesNotDropConnectionOrRegistrations) {
  DaemonClient::Options o;
  o.socket_path = path_;
  o.reconnect_interval = milliseconds(50);
  o.poll_ms = 20;
  o.send_timeout_ms = 50;
  DaemonClient client{o};
  const auto ep = client.AttachEndpoint(Skeleton(Options()), true);
  ASSERT_TRUE(WaitFor([&] { return ep->IsRegistered(); }));
  daemon_->SetReading(false);
  for (int i = 0; i < 20000 && ep->counters().tx_dropped == 0; ++i) {
    ASSERT_TRUE(ep->PublishEvent(kEvent, {0, 1}).HasValue());
  }
  ASSERT_GT(ep->counters().tx_dropped, 0U) << "the daemon buffer never filled up";
  // A send timeout is not a broken connection: nothing is lost.
  EXPECT_TRUE(client.connected());
  EXPECT_TRUE(ep->IsRegistered());
  EXPECT_EQ(daemon_->connections(), 1U);
  EXPECT_EQ(daemon_->registrations().size(), 1U);
  daemon_->SetReading(true);
  const auto sent = ep->counters().tx_events;
  EXPECT_TRUE(WaitFor([&] {
    std::ignore = ep->PublishEvent(kEvent, {0, 1});
    return ep->counters().tx_events > sent;
  }));
  EXPECT_EQ(daemon_->registrations().size(), 1U);
  client.Shutdown();
}

TEST_F(DaemonClientTest, SequenceStartsAtARandomValueAfterRestart) {
  std::set<std::uint16_t> first_seqs;
  for (int i = 0; i < 20; ++i) {
    const auto ep = client_->AttachEndpoint(Skeleton(Options()), true);
    ASSERT_TRUE(WaitFor([&] { return ep->IsRegistered(); }));
    ASSERT_TRUE(ep->PublishEvent(kEvent, {0, 1}).HasValue());
    const auto msg = daemon_->WaitMessage(milliseconds(1000));
    ASSERT_TRUE(msg.has_value());
    const auto& ev = std::get<daemon::ElementPayload>(*msg);
    first_seqs.insert(daemon::ReadHeader(daemon_->registrations().back().elements[0].req, ev.payload).seq);
    client_->DetachEndpoint(ep);
  }
  // A fixed start would make a restarted sender repeat the seq (and payload) of its previous life.
  EXPECT_GT(first_seqs.size(), 1U);
}

TEST(DaemonClientDeath, ExceptionFromMethodHandlerIsLoggedAndNotTurnedIntoStatus) {
  ::testing::FLAGS_gtest_death_test_style = "threadsafe";
  EXPECT_DEATH_IF_SUPPORTED(
      {
        const std::string path = "/tmp/srp_mav_death_" + std::to_string(::getpid()) + ".sock";
        testing::FakeDaemon daemon{path};
        daemon.Start();
        DaemonClient::Options o;
        o.socket_path = path;
        o.reconnect_interval = milliseconds(50);
        o.poll_ms = 20;
        o.log = [](LogLevel l, const std::string& msg) {
          if (l == LogLevel::kError) std::cerr << msg << std::endl;
        };
        DaemonClient client{o};
        MavlinkEndpoint::Handlers handlers;
        handlers.method = [](std::uint16_t, const std::vector<std::uint8_t>&)
            -> ara::core::Result<std::vector<std::uint8_t>> { throw std::runtime_error("boom-handler"); };
        const auto ep = client.AttachEndpoint(Skeleton(Options()), true, std::move(handlers));
        for (int i = 0; i < 300 && !ep->IsRegistered(); ++i) std::this_thread::sleep_for(milliseconds(10));
        const auto opts = Options();
        daemon::Delivery in;
        in.kind = daemon::ElementKind::kRequest;
        in.handle = daemon.registrations()[0].handle;
        in.element_id = kMethod;
        in.link = "lora";
        in.payload = MessageCodec{opts.elements[1].req}.Encode(MessageHeader{1, 2, 0, 0, 0}, {10}).Value();
        daemon.SendToApp(in);
        std::this_thread::sleep_for(std::chrono::seconds(5));  // the worker thread kills the process first
      },
      "SetMode.*threw: boom-handler");
}

}  // namespace
}  // namespace mavlink
}  // namespace com
}  // namespace ara
