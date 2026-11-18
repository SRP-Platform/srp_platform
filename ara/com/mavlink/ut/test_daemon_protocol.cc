/**
 * Copyright (c) 2024
 *
 * @file test_daemon_protocol.cc
 */
#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

#include "ara/com/mavlink/daemon_protocol.h"

namespace ara {
namespace com {
namespace mavlink {
namespace daemon {
namespace {

MessageDesc Desc(std::uint32_t id, std::uint8_t len, std::uint8_t seq_off = 0) {
  MessageDesc d;
  d.msg_id = id;
  d.crc_extra = 77;
  d.full_len = len;
  d.seq_offset = seq_off;
  d.instance_offset = static_cast<std::uint8_t>(seq_off + 2);
  d.status_offset = static_cast<std::uint8_t>(len - 1);
  d.name = "MSG_" + std::to_string(id);
  return d;
}

RegisterInstance MakeRegister() {
  RegisterInstance r;
  r.handle = 7;
  r.role = Role::kRequire;
  r.com_id = "srp.radio.Example";
  r.instance = 3;
  r.target_system = 1;
  r.target_component = 2;
  r.source_system = 4;
  r.source_component = 5;
  r.dedup_ttl_ms = 2500;
  r.links = {"lora", "radio_b"};
  ElementDesc ev;
  ev.id = 0x8001;
  ev.is_event = true;
  ev.min_interval_ms = 200;
  ev.req = Desc(50001, 12);
  ElementDesc m;
  m.id = 1;
  m.has_resp = true;
  m.timeout_ms = 750;
  m.req = Desc(50002, 20);
  m.resp = Desc(50003, 9);
  ElementDesc off;
  off.id = 2;
  off.available = false;
  off.req = Desc(50004, 6);
  r.elements = {ev, m, off};
  return r;
}

Message RoundTrip(const Message& m) {
  const auto enc = Encode(m);
  EXPECT_TRUE(enc.HasValue());
  const auto dec = Decode(enc.Value());
  EXPECT_TRUE(dec.HasValue());
  EXPECT_EQ(dec.Value().index(), m.index());
  return dec.Value();
}

TEST(DaemonProtocol, RegisterInstanceRoundTrip) {
  const auto in = MakeRegister();
  ASSERT_EQ(Validate(in), "");
  const auto out = std::get<RegisterInstance>(RoundTrip(in));
  EXPECT_EQ(out.protocol_version, kProtocolVersion);
  EXPECT_EQ(out.handle, 7U);
  EXPECT_EQ(out.role, Role::kRequire);
  EXPECT_EQ(out.com_id, in.com_id);
  EXPECT_EQ(out.instance, 3);
  EXPECT_EQ(out.target_system, 1);
  EXPECT_EQ(out.target_component, 2);
  EXPECT_EQ(out.source_system, 4);
  EXPECT_EQ(out.source_component, 5);
  EXPECT_EQ(out.dedup_ttl_ms, 2500U);
  EXPECT_EQ(out.links, in.links);
  ASSERT_EQ(out.elements.size(), 3U);
  EXPECT_TRUE(out.elements[0].is_event);
  EXPECT_EQ(out.elements[0].min_interval_ms, 200U);
  EXPECT_EQ(out.elements[0].req.msg_id, 50001U);
  EXPECT_EQ(out.elements[0].req.crc_extra, 77);
  EXPECT_EQ(out.elements[0].req.name, "MSG_50001");
  EXPECT_EQ(out.elements[0].req.instance_offset, 2);
  EXPECT_EQ(out.elements[0].req.target_system_offset, kNoOffset);
  EXPECT_EQ(out.elements[1].timeout_ms, 750U);
  EXPECT_TRUE(out.elements[1].has_resp);
  EXPECT_EQ(out.elements[1].resp.msg_id, 50003U);
  EXPECT_EQ(out.elements[1].resp.status_offset, 8);
  EXPECT_FALSE(out.elements[2].available);
}

TEST(DaemonProtocol, SimpleMessagesRoundTrip) {
  RegisterReply rr;
  rr.handle = 9;
  rr.status = RegisterStatus::kConflictMsgId;
  rr.system_id = 11;
  rr.component_id = 1;
  rr.text = "msg_id 50001 zajęty";
  const auto rr2 = std::get<RegisterReply>(RoundTrip(rr));
  EXPECT_EQ(rr2.handle, 9U);
  EXPECT_EQ(rr2.status, RegisterStatus::kConflictMsgId);
  EXPECT_EQ(rr2.system_id, 11);
  EXPECT_EQ(rr2.text, rr.text);

  EXPECT_EQ(std::get<Unregister>(RoundTrip(Unregister{42})).handle, 42U);

  Event ev;
  ev.handle = 1;
  ev.element_id = 0x8001;
  ev.payload = {1, 2, 3};
  const auto ev2 = std::get<Event>(RoundTrip(ev));
  EXPECT_EQ(ev2.element_id, 0x8001);
  EXPECT_EQ(ev2.payload, ev.payload);
  Request rq;
  rq.handle = 2;
  rq.element_id = 5;
  rq.payload = std::vector<std::uint8_t>(255, 0xAB);
  EXPECT_EQ(std::get<Request>(RoundTrip(rq)).payload.size(), 255U);
  Response rs;
  rs.handle = 3;
  rs.element_id = 6;
  EXPECT_TRUE(std::get<Response>(RoundTrip(rs)).payload.empty());

  SendError se;
  se.handle = 4;
  se.element_id = 5;
  se.seq = 600;
  se.code = SendErrc::kQueueFull;
  const auto se2 = std::get<SendError>(RoundTrip(se));
  EXPECT_EQ(se2.seq, 600);
  EXPECT_EQ(se2.code, SendErrc::kQueueFull);

  EventIn d;
  d.handle = 8;
  d.element_id = 0x8002;
  d.sender_system = 21;
  d.sender_component = 22;
  d.link = "lora";
  d.payload = {9, 8, 7, 6};
  const auto d2 = std::get<EventIn>(RoundTrip(d));
  EXPECT_EQ(d2.sender_system, 21);
  EXPECT_EQ(d2.sender_component, 22);
  EXPECT_EQ(d2.link, "lora");
  EXPECT_EQ(d2.payload, d.payload);
  RequestIn rqi;
  rqi.link = "radio_b";
  EXPECT_EQ(std::get<RequestIn>(RoundTrip(rqi)).link, "radio_b");
  ResponseIn rsi;
  rsi.element_id = 3;
  EXPECT_EQ(std::get<ResponseIn>(RoundTrip(rsi)).element_id, 3);

  LinkStatus ls_in;
  ls_in.handle = 5;
  ls_in.link = "lora";
  ls_in.ready = true;
  const auto ls = std::get<LinkStatus>(RoundTrip(ls_in));
  EXPECT_EQ(ls.handle, 5U);
  EXPECT_EQ(ls.link, "lora");
  EXPECT_TRUE(ls.ready);
}

TEST(DaemonProtocol, TypeByteMatchesMsgType) {
  EXPECT_EQ(TypeOf(Message{RegisterInstance{}}), MsgType::kRegisterInstance);
  EXPECT_EQ(TypeOf(Message{LinkStatus{}}), MsgType::kLinkStatus);
  EXPECT_EQ(TypeOf(Message{ResponseIn{}}), MsgType::kResponseIn);
  EXPECT_EQ(Encode(Message{Request{}}).Value()[0], static_cast<std::uint8_t>(MsgType::kRequest));
  EXPECT_EQ(Encode(Message{SendError{}}).Value()[0], static_cast<std::uint8_t>(MsgType::kSendError));
}

TEST(DaemonProtocol, DecodeRejectsMalformed) {
  EXPECT_FALSE(Decode({}).HasValue());
  const std::vector<std::uint8_t> unknown{0xEE, 1, 2};
  EXPECT_FALSE(Decode(unknown).HasValue());
  const std::vector<std::uint8_t> zero{0};
  EXPECT_FALSE(Decode(zero).HasValue());

  // każdy prefiks poprawnego komunikatu jest błędem (urwanie)
  const auto good = Encode(MakeRegister()).Value();
  for (std::size_t n = 1; n < good.size(); ++n) {
    EXPECT_FALSE(Decode(std::span<const std::uint8_t>(good.data(), n)).HasValue()) << "n=" << n;
  }
  // nadmiarowe bajty
  auto extra = good;
  extra.push_back(0);
  EXPECT_FALSE(Decode(extra).HasValue());
  // rola poza zakresem: [type][u16 version][u32 handle][role]
  auto bad_role = good;
  bad_role[7] = 2;
  EXPECT_FALSE(Decode(bad_role).HasValue());
  // nieznany status / kod
  auto reply = Encode(RegisterReply{}).Value();
  reply[5] = 99;
  EXPECT_FALSE(Decode(reply).HasValue());
  SendError se;
  se.code = SendErrc::kNoLink;
  auto err = Encode(se).Value();
  err.back() = 0;
  EXPECT_FALSE(Decode(err).HasValue());
}

TEST(DaemonProtocol, DecodeRejectsTooManyLinks) {
  const auto good = Encode(MakeRegister()).Value();
  // liczba łączy: po [type][ver 2][handle 4][role 1][u8 len + com_id][inst 2][4 x u8][ttl 4]
  const std::size_t links_at =
      1 + 2 + 4 + 1 + 1 + std::string{"srp.radio.Example"}.size() + 2 + 4 + 4;
  ASSERT_EQ(good[links_at], 2);
  auto many = good;
  many[links_at] = static_cast<std::uint8_t>(kMaxLinksPerInstance + 1);
  EXPECT_FALSE(Decode(many).HasValue());
}

TEST(DaemonProtocol, EncodeEnforcesLimits) {
  Event ev;
  ev.payload = std::vector<std::uint8_t>(kMaxPayload + 1, 0);
  EXPECT_FALSE(Encode(ev).HasValue());
  RegisterReply rr;
  rr.text = std::string(kMaxStringLen + 1, 'x');
  EXPECT_FALSE(Encode(rr).HasValue());
  auto r = MakeRegister();
  r.links.assign(kMaxLinksPerInstance + 1, "x");
  EXPECT_FALSE(Encode(r).HasValue());
  r = MakeRegister();
  r.elements.assign(kMaxElements + 1, r.elements[0]);
  EXPECT_FALSE(Encode(r).HasValue());
}

TEST(DaemonProtocol, ValidateCatchesBadRegistrations) {
  EXPECT_EQ(Validate(MakeRegister()), "");
  auto r = MakeRegister();
  r.com_id.clear();
  EXPECT_NE(Validate(r), "");
  r = MakeRegister();
  r.links.clear();
  EXPECT_NE(Validate(r), "");
  r = MakeRegister();
  r.links = {"a", "a"};
  EXPECT_NE(Validate(r), "");
  r = MakeRegister();
  r.elements.clear();
  EXPECT_NE(Validate(r), "");
  r = MakeRegister();
  r.elements[1].id = r.elements[0].id;
  EXPECT_NE(Validate(r), "");
  r = MakeRegister();
  r.elements[0].req.full_len = 0;
  EXPECT_NE(Validate(r), "");
  r = MakeRegister();
  r.elements[0].req.seq_offset = 11;  // u16 od 11 wychodzi poza 12 B
  EXPECT_NE(Validate(r), "");
  r = MakeRegister();
  r.elements[0].has_resp = true;  // event z RESP
  EXPECT_NE(Validate(r), "");
  r = MakeRegister();
  r.elements[1].resp.target_system_offset = 9;  // poza 9 B
  EXPECT_NE(Validate(r), "");
  r = MakeRegister();
  r.elements[0].req.msg_id = 0x1000000;
  EXPECT_NE(Validate(r), "");
}

}  // namespace
}  // namespace daemon
}  // namespace mavlink
}  // namespace com
}  // namespace ara
