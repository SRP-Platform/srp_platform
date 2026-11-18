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
  r.links = {"lora", "radio_b"};
  ElementDesc ev;
  ev.id = 0x8001;
  ev.is_event = true;
  ev.links = {{"lora", 200}, {"radio_b", 0}};
  ev.req = Desc(50001, 12);
  ElementDesc m;
  m.id = 1;
  m.has_resp = true;
  m.links = {{"radio_b", 0}};
  m.req = Desc(50002, 20);
  m.resp = Desc(50003, 9);
  ElementDesc v;
  v.id = 2;
  v.links = {{"lora", 0}};
  v.req = Desc(50004, 6);
  r.elements = {ev, m, v};
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
  EXPECT_EQ(out.links, in.links);
  ASSERT_EQ(out.elements.size(), 3U);
  EXPECT_TRUE(out.elements[0].is_event);
  ASSERT_EQ(out.elements[0].links.size(), 2U);
  EXPECT_EQ(out.elements[0].links[0].link, "lora");
  EXPECT_EQ(out.elements[0].links[0].min_interval_ms, 200U);
  EXPECT_EQ(out.elements[0].links[1].link, "radio_b");
  EXPECT_EQ(out.elements[0].links[1].min_interval_ms, 0U);
  EXPECT_EQ(out.elements[0].req.msg_id, 50001U);
  EXPECT_EQ(out.elements[0].req.crc_extra, 77);
  EXPECT_EQ(out.elements[0].req.name, "MSG_50001");
  EXPECT_EQ(out.elements[0].req.instance_offset, 2);
  EXPECT_EQ(out.elements[0].req.target_system_offset, kNoOffset);
  EXPECT_TRUE(out.elements[1].has_resp);
  EXPECT_EQ(out.elements[1].resp.msg_id, 50003U);
  EXPECT_EQ(out.elements[1].resp.status_offset, 8);
  ASSERT_EQ(out.elements[1].links.size(), 1U);
  EXPECT_EQ(out.elements[1].links[0].link, "radio_b");
  ASSERT_EQ(out.elements[2].links.size(), 1U);
  EXPECT_EQ(out.elements[2].links[0].link, "lora");
}

TEST(DaemonProtocol, ElementLinksAreValidated) {
  auto r = MakeRegister();
  r.elements[0].links.clear();
  EXPECT_NE(Validate(r).find("no link"), std::string::npos) << Validate(r);
  r = MakeRegister();
  r.elements[0].links = {{"elsewhere", 0}};
  EXPECT_NE(Validate(r).find("not a link of the instance"), std::string::npos) << Validate(r);
  r = MakeRegister();
  r.elements[0].links = {{"lora", 0}, {"lora", 5}};
  EXPECT_NE(Validate(r).find("repeated"), std::string::npos) << Validate(r);
}

TEST(DaemonProtocol, ProtocolVersionIsThree) {
  EXPECT_EQ(kProtocolVersion, 3);
}

TEST(DaemonProtocol, SimpleMessagesRoundTrip) {
  RegisterReply rr;
  rr.handle = 9;
  rr.status = RegisterStatus::kConflictMsgId;
  rr.system_id = 11;
  rr.component_id = 1;
  rr.text = "msg_id 50001 is taken";
  const auto rr2 = std::get<RegisterReply>(RoundTrip(rr));
  EXPECT_EQ(rr2.handle, 9U);
  EXPECT_EQ(rr2.status, RegisterStatus::kConflictMsgId);
  EXPECT_EQ(rr2.system_id, 11);
  EXPECT_EQ(rr2.text, rr.text);
  EXPECT_TRUE(rr2.method_timeouts.empty());

  // The effective method timeouts travel in the reply (the daemon config decides them).
  RegisterReply ok;
  ok.handle = 3;
  ok.method_timeouts = {{0x0010, 4000}, {0x0011, 1000}};
  const auto ok2 = std::get<RegisterReply>(RoundTrip(ok));
  ASSERT_EQ(ok2.method_timeouts.size(), 2U);
  EXPECT_EQ(ok2.method_timeouts[0].element_id, 0x0010);
  EXPECT_EQ(ok2.method_timeouts[0].timeout_ms, 4000U);
  EXPECT_EQ(ok2.method_timeouts[1].timeout_ms, 1000U);

  EXPECT_EQ(std::get<Unregister>(RoundTrip(Unregister{42})).handle, 42U);

  for (const auto kind : {ElementKind::kEvent, ElementKind::kRequest, ElementKind::kResponse}) {
    ElementPayload el;
    el.kind = kind;
    el.handle = 1;
    el.element_id = 0x8001;
    el.payload = {1, 2, 3};
    const auto el2 = std::get<ElementPayload>(RoundTrip(el));
    EXPECT_EQ(el2.kind, kind);
    EXPECT_EQ(el2.element_id, 0x8001);
    EXPECT_EQ(el2.payload, el.payload);
  }
  ElementPayload big;
  big.kind = ElementKind::kRequest;
  big.payload = std::vector<std::uint8_t>(255, 0xAB);
  EXPECT_EQ(std::get<ElementPayload>(RoundTrip(big)).payload.size(), 255U);

  SendError se;
  se.handle = 4;
  se.element_id = 5;
  se.seq = 600;
  se.code = SendErrc::kQueueFull;
  const auto se2 = std::get<SendError>(RoundTrip(se));
  EXPECT_EQ(se2.seq, 600);
  EXPECT_EQ(se2.code, SendErrc::kQueueFull);

  Delivery d;
  d.kind = ElementKind::kResponse;
  d.handle = 8;
  d.element_id = 0x8002;
  d.sender_system = 21;
  d.sender_component = 22;
  d.link = "lora";
  d.payload = {9, 8, 7, 6};
  const auto d2 = std::get<Delivery>(RoundTrip(d));
  EXPECT_EQ(d2.kind, ElementKind::kResponse);
  EXPECT_EQ(d2.sender_system, 21);
  EXPECT_EQ(d2.sender_component, 22);
  EXPECT_EQ(d2.link, "lora");
  EXPECT_EQ(d2.payload, d.payload);

  LinkStatus ls_in;
  ls_in.handle = 5;
  ls_in.link = "lora";
  ls_in.ready = true;
  const auto ls = std::get<LinkStatus>(RoundTrip(ls_in));
  EXPECT_EQ(ls.handle, 5U);
  EXPECT_EQ(ls.link, "lora");
  EXPECT_TRUE(ls.ready);
}

TEST(DaemonProtocol, LinkStatsMessagesRoundTrip) {
  EXPECT_EQ(TypeOf(Message{SubscribeLinkStats{}}), MsgType::kSubscribeLinkStats);
  EXPECT_TRUE(std::holds_alternative<SubscribeLinkStats>(RoundTrip(SubscribeLinkStats{})));

  GetLinkStats g;
  g.request_id = 77;
  g.link = "radio_b";
  const auto g2 = std::get<GetLinkStats>(RoundTrip(g));
  EXPECT_EQ(g2.request_id, 77U);
  EXPECT_EQ(g2.link, "radio_b");

  LinkStatsMessage m;
  m.request_id = 3;
  m.ok = true;
  m.stats.link_index = 1;
  m.stats.link_name = "radio_b";
  m.stats.ready = true;
  m.stats.age_ms = 250;
  m.stats.valid = 3;
  m.stats.rssi_dbm = -71;
  m.stats.noise_floor_dbm = -110;
  m.stats.snr_db = 39;
  m.stats.rx_ok = 1;
  m.stats.rx_crc_err = 2;
  m.stats.rx_sig_err = 3;
  m.stats.tx_frames = 4;
  m.stats.tx_busy = 5;
  m.stats.tx_dropped = 6;
  m.stats.tx_expired = 7;
  m.stats.budget_pct = 8;
  const auto m2 = std::get<LinkStatsMessage>(RoundTrip(m));
  EXPECT_EQ(m2.request_id, 3U);
  EXPECT_TRUE(m2.ok);
  EXPECT_EQ(m2.stats.link_name, "radio_b");
  EXPECT_EQ(m2.stats.rssi_dbm, -71);
  EXPECT_EQ(m2.stats.noise_floor_dbm, -110);
  EXPECT_EQ(m2.stats.snr_db, 39);
  EXPECT_EQ(m2.stats.valid, 3);
  EXPECT_EQ(m2.stats.age_ms, 250U);
  EXPECT_EQ(m2.stats.tx_expired, 7U);
  EXPECT_EQ(m2.stats.budget_pct, 8);

  // a link name longer than the service allows (16 B) is not encodable
  m.stats.link_name = std::string(LinkStats::kMaxNameLen + 1, 'x');
  EXPECT_FALSE(Encode(m).HasValue());
  // truncation of every proper prefix
  m.stats.link_name = "a";
  const auto good = Encode(m).Value();
  for (std::size_t n = 1; n < good.size(); ++n) {
    EXPECT_FALSE(Decode(std::span<const std::uint8_t>(good.data(), n)).HasValue()) << n;
  }
}

TEST(DaemonProtocol, DecodeRejectsMalformed) {
  EXPECT_FALSE(Decode({}).HasValue());
  const std::vector<std::uint8_t> unknown{0xEE, 1, 2};
  EXPECT_FALSE(Decode(unknown).HasValue());
  const std::vector<std::uint8_t> zero{0};
  EXPECT_FALSE(Decode(zero).HasValue());

  // every proper prefix of a valid message is an error (truncation)
  const auto good = Encode(MakeRegister()).Value();
  for (std::size_t n = 1; n < good.size(); ++n) {
    EXPECT_FALSE(Decode(std::span<const std::uint8_t>(good.data(), n)).HasValue()) << "n=" << n;
  }
  // excess bytes
  auto extra = good;
  extra.push_back(0);
  EXPECT_FALSE(Decode(extra).HasValue());
  // role out of range: [type][u16 version][u32 handle][role]
  auto bad_role = good;
  bad_role[7] = 2;
  EXPECT_FALSE(Decode(bad_role).HasValue());
  // unknown status / code / kind
  auto reply = Encode(RegisterReply{}).Value();
  reply[5] = 99;
  EXPECT_FALSE(Decode(reply).HasValue());
  SendError se;
  se.code = SendErrc::kNoLink;
  auto err = Encode(se).Value();
  err.back() = 0;
  EXPECT_FALSE(Decode(err).HasValue());
  auto bad_kind = Encode(ElementPayload{}).Value();
  bad_kind[1] = 3;  // [type][kind]
  EXPECT_FALSE(Decode(bad_kind).HasValue());
}

TEST(DaemonProtocol, DecodeRejectsTooManyLinks) {
  const auto good = Encode(MakeRegister()).Value();
  // link count: after [type][ver 2][handle 4][role 1][u8 len + com_id][inst 2][4 x u8]
  const std::size_t links_at =
      1 + 2 + 4 + 1 + 1 + std::string{"srp.radio.Example"}.size() + 2 + 4;
  ASSERT_EQ(good[links_at], 2);
  auto many = good;
  many[links_at] = static_cast<std::uint8_t>(kMaxLinksPerInstance + 1);
  EXPECT_FALSE(Decode(many).HasValue());
}

TEST(DaemonProtocol, EncodeEnforcesLimits) {
  ElementPayload ev;
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
  r.elements[0].req.seq_offset = 11;  // a u16 at 11 exceeds 12 B
  EXPECT_NE(Validate(r), "");
  r = MakeRegister();
  r.elements[0].has_resp = true;  // an event with a RESP
  EXPECT_NE(Validate(r), "");
  r = MakeRegister();
  r.elements[1].resp.target_system_offset = 9;  // beyond 9 B
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
