/**
 * Copyright (c) 2024
 *
 * @file daemon_protocol.cc
 */
#include "ara/com/mavlink/daemon_protocol.h"

#include <algorithm>
#include <set>
#include <string>
#include <type_traits>
#include <utility>

#include "ara/com/com_error_domain.h"

namespace ara {
namespace com {
namespace mavlink {
namespace daemon {
namespace {

template <typename T>
constexpr bool kIsElementPayload =
    std::is_same_v<T, Event> || std::is_same_v<T, Request> || std::is_same_v<T, Response>;
template <typename T>
constexpr bool kIsDelivery =
    std::is_same_v<T, EventIn> || std::is_same_v<T, RequestIn> || std::is_same_v<T, ResponseIn>;

ara::core::ErrorCode Invalid(const std::string& what) {
  return ara::com::MakeErrorCode(ara::com::ComErrc::kFieldValueIsNotValid, what);
}

class Writer {
 public:
  void U8(std::uint8_t v) { out_.push_back(v); }
  void U16(std::uint16_t v) { Le(v, 2); }
  void U32(std::uint32_t v) { Le(v, 4); }
  void Bool(bool v) { U8(v ? 1 : 0); }
  void Str(const std::string& s) {
    if (s.size() > kMaxStringLen) {
      Fail("tekst dłuższy niż " + std::to_string(kMaxStringLen) + " B");
      return;
    }
    U8(static_cast<std::uint8_t>(s.size()));
    out_.insert(out_.end(), s.begin(), s.end());
  }
  void Bytes(const std::vector<std::uint8_t>& b) {
    if (b.size() > kMaxPayload) {
      Fail("payload dłuższy niż " + std::to_string(kMaxPayload) + " B");
      return;
    }
    U8(static_cast<std::uint8_t>(b.size()));
    out_.insert(out_.end(), b.begin(), b.end());
  }
  void Fail(const std::string& why) {
    if (error_.empty()) {
      error_ = why;
    }
  }
  const std::string& error() const { return error_; }
  std::vector<std::uint8_t>& out() { return out_; }

 private:
  void Le(std::uint32_t v, int n) {
    for (int i = 0; i < n; ++i) {
      out_.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }
  }
  std::vector<std::uint8_t> out_;
  std::string error_;
};

class Reader {
 public:
  explicit Reader(std::span<const std::uint8_t> d) : d_{d} {}
  std::uint8_t U8() {
    if (!Need(1)) return 0;
    return d_[pos_++];
  }
  std::uint16_t U16() { return static_cast<std::uint16_t>(Le(2)); }
  std::uint32_t U32() { return static_cast<std::uint32_t>(Le(4)); }
  bool Bool() { return U8() != 0; }
  std::string Str() {
    const std::size_t n = U8();
    if (!Need(n)) return {};
    std::string s(reinterpret_cast<const char*>(d_.data() + pos_), n);
    pos_ += n;
    return s;
  }
  std::vector<std::uint8_t> Bytes() {
    const std::size_t n = U8();
    if (!Need(n)) return {};
    std::vector<std::uint8_t> v(d_.begin() + pos_, d_.begin() + pos_ + n);
    pos_ += n;
    return v;
  }
  bool ok() const { return ok_; }
  bool done() const { return pos_ == d_.size(); }

 private:
  bool Need(std::size_t n) {
    if (!ok_ || d_.size() - pos_ < n) {
      ok_ = false;
      return false;
    }
    return true;
  }
  std::uint32_t Le(int n) {
    if (!Need(static_cast<std::size_t>(n))) return 0;
    std::uint32_t v = 0;
    for (int i = 0; i < n; ++i) {
      v |= static_cast<std::uint32_t>(d_[pos_++]) << (8 * i);
    }
    return v;
  }
  std::span<const std::uint8_t> d_;
  std::size_t pos_{0};
  bool ok_{true};
};

void PutDesc(Writer* w, const MessageDesc& m) {
  w->U32(m.msg_id);
  w->U8(m.crc_extra);
  w->U8(m.full_len);
  w->U8(m.seq_offset);
  w->U8(m.instance_offset);
  w->U8(m.target_system_offset);
  w->U8(m.target_component_offset);
  w->U8(m.status_offset);
  w->Str(m.name);
}

MessageDesc GetDesc(Reader* r) {
  MessageDesc m;
  m.msg_id = r->U32();
  m.crc_extra = r->U8();
  m.full_len = r->U8();
  m.seq_offset = r->U8();
  m.instance_offset = r->U8();
  m.target_system_offset = r->U8();
  m.target_component_offset = r->U8();
  m.status_offset = r->U8();
  m.name = r->Str();
  return m;
}

void PutElementPayload(Writer* w, const ElementPayload& e) {
  w->U32(e.handle);
  w->U16(e.element_id);
  w->Bytes(e.payload);
}

void GetElementPayload(Reader* r, ElementPayload* e) {
  e->handle = r->U32();
  e->element_id = r->U16();
  e->payload = r->Bytes();
}

void PutDelivery(Writer* w, const Delivery& d) {
  w->U32(d.handle);
  w->U16(d.element_id);
  w->U8(d.sender_system);
  w->U8(d.sender_component);
  w->Str(d.link);
  w->Bytes(d.payload);
}

void GetDelivery(Reader* r, Delivery* d) {
  d->handle = r->U32();
  d->element_id = r->U16();
  d->sender_system = r->U8();
  d->sender_component = r->U8();
  d->link = r->Str();
  d->payload = r->Bytes();
}

void PutRegister(Writer* w, const RegisterInstance& m) {
  w->U16(m.protocol_version);
  w->U32(m.handle);
  w->U8(static_cast<std::uint8_t>(m.role));
  w->Str(m.com_id);
  w->U16(m.instance);
  w->U8(m.target_system);
  w->U8(m.target_component);
  w->U8(m.source_system);
  w->U8(m.source_component);
  w->U32(m.dedup_ttl_ms);
  if (m.links.size() > kMaxLinksPerInstance) {
    w->Fail("za dużo łączy w instancji");
    return;
  }
  w->U8(static_cast<std::uint8_t>(m.links.size()));
  for (const auto& l : m.links) {
    w->Str(l);
  }
  if (m.elements.size() > kMaxElements) {
    w->Fail("za dużo elementów w instancji");
    return;
  }
  w->U8(static_cast<std::uint8_t>(m.elements.size()));
  for (const auto& e : m.elements) {
    w->U16(e.id);
    w->Bool(e.is_event);
    w->Bool(e.available);
    w->Bool(e.has_resp);
    w->U32(e.timeout_ms);
    w->U32(e.min_interval_ms);
    PutDesc(w, e.req);
    PutDesc(w, e.resp);
  }
}

/// Zwraca niepusty `*err` dla niepoprawnej roli albo przekroczonych limitów.
RegisterInstance GetRegister(Reader* r, std::string* err) {
  RegisterInstance m;
  m.protocol_version = r->U16();
  m.handle = r->U32();
  const auto role = r->U8();
  if (role > 1) {
    *err = "RegisterInstance: nieznana rola";
    return m;
  }
  m.role = role == 1 ? Role::kRequire : Role::kProvide;
  m.com_id = r->Str();
  m.instance = r->U16();
  m.target_system = r->U8();
  m.target_component = r->U8();
  m.source_system = r->U8();
  m.source_component = r->U8();
  m.dedup_ttl_ms = r->U32();
  const std::size_t nl = r->U8();
  if (nl > kMaxLinksPerInstance) {
    *err = "RegisterInstance: za dużo łączy";
    return m;
  }
  for (std::size_t i = 0; i < nl && r->ok(); ++i) {
    m.links.push_back(r->Str());
  }
  const std::size_t ne = r->U8();
  if (ne > kMaxElements) {
    *err = "RegisterInstance: za dużo elementów";
    return m;
  }
  for (std::size_t i = 0; i < ne && r->ok(); ++i) {
    ElementDesc e;
    e.id = r->U16();
    e.is_event = r->Bool();
    e.available = r->Bool();
    e.has_resp = r->Bool();
    e.timeout_ms = r->U32();
    e.min_interval_ms = r->U32();
    e.req = GetDesc(r);
    e.resp = GetDesc(r);
    m.elements.push_back(std::move(e));
  }
  return m;
}

std::string CheckDesc(const MessageDesc& m, const std::string& what) {
  if (m.msg_id > 0xFFFFFF) {
    return what + ": msg_id > 24 bity";
  }
  if (m.full_len == 0) {
    return what + ": pusty payload";
  }
  const auto fits = [&m](std::uint8_t off, std::size_t width) {
    return off == kNoOffset || static_cast<std::size_t>(off) + width <= m.full_len;
  };
  if (!fits(m.seq_offset, 2) || !fits(m.instance_offset, 2) || !fits(m.target_system_offset, 1) ||
      !fits(m.target_component_offset, 1) || !fits(m.status_offset, 1)) {
    return what + ": offset pola nagłówka poza payloadem";
  }
  return {};
}

}  // namespace

MsgType TypeOf(const Message& m) noexcept {
  return static_cast<MsgType>(m.index() + 1);
}

std::string Validate(const RegisterInstance& r) {
  if (r.com_id.empty() || r.com_id.size() > kMaxStringLen) {
    return "com_id pusty albo za długi";
  }
  if (r.links.empty() || r.links.size() > kMaxLinksPerInstance) {
    return "liczba łączy poza zakresem 1.." + std::to_string(kMaxLinksPerInstance);
  }
  std::set<std::string> seen;
  for (const auto& l : r.links) {
    if (l.empty() || l.size() > kMaxStringLen) {
      return "nazwa łącza pusta albo za długa";
    }
    if (!seen.insert(l).second) {
      return "łącze '" + l + "' powtórzone";
    }
  }
  if (r.elements.empty() || r.elements.size() > kMaxElements) {
    return "liczba elementów poza zakresem 1.." + std::to_string(kMaxElements);
  }
  std::set<std::uint16_t> ids;
  for (const auto& e : r.elements) {
    const std::string what = "element " + std::to_string(e.id);
    if (!ids.insert(e.id).second) {
      return what + " powtórzony";
    }
    if (auto err = CheckDesc(e.req, what + " (req)"); !err.empty()) {
      return err;
    }
    if (e.is_event && e.has_resp) {
      return what + ": event nie ma RESP";
    }
    if (e.has_resp) {
      if (auto err = CheckDesc(e.resp, what + " (resp)"); !err.empty()) {
        return err;
      }
    }
  }
  return {};
}

ara::core::Result<std::vector<std::uint8_t>> Encode(const Message& m) {
  Writer w;
  w.U8(static_cast<std::uint8_t>(TypeOf(m)));
  std::visit(
      [&w](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, RegisterInstance>) {
          PutRegister(&w, v);
        } else if constexpr (std::is_same_v<T, RegisterReply>) {
          w.U32(v.handle);
          w.U8(static_cast<std::uint8_t>(v.status));
          w.U8(v.system_id);
          w.U8(v.component_id);
          w.Str(v.text);
        } else if constexpr (std::is_same_v<T, Unregister>) {
          w.U32(v.handle);
        } else if constexpr (kIsElementPayload<T>) {
          PutElementPayload(&w, v);
        } else if constexpr (std::is_same_v<T, SendError>) {
          w.U32(v.handle);
          w.U16(v.element_id);
          w.U16(v.seq);
          w.U8(static_cast<std::uint8_t>(v.code));
        } else if constexpr (kIsDelivery<T>) {
          PutDelivery(&w, v);
        } else if constexpr (std::is_same_v<T, LinkStatus>) {
          w.U32(v.handle);
          w.Str(v.link);
          w.Bool(v.ready);
        }
      },
      m);
  if (!w.error().empty()) {
    return Invalid(w.error());
  }
  if (w.out().size() > kMaxMessageSize) {
    return Invalid("komunikat większy niż " + std::to_string(kMaxMessageSize) + " B");
  }
  return std::move(w.out());
}

ara::core::Result<Message> Decode(std::span<const std::uint8_t> packet) {
  if (packet.empty() || packet.size() > kMaxMessageSize) {
    return Invalid("pusty albo za duży pakiet");
  }
  Reader r{packet.subspan(1)};
  Message out;
  switch (static_cast<MsgType>(packet[0])) {
    case MsgType::kRegisterInstance: {
      std::string err;
      auto m = GetRegister(&r, &err);
      if (!err.empty()) {
        return Invalid(err);
      }
      out = std::move(m);
      break;
    }
    case MsgType::kRegisterReply: {
      RegisterReply m;
      m.handle = r.U32();
      const auto st = r.U8();
      if (st > static_cast<std::uint8_t>(RegisterStatus::kDuplicateHandle)) {
        return Invalid("RegisterReply: nieznany status");
      }
      m.status = static_cast<RegisterStatus>(st);
      m.system_id = r.U8();
      m.component_id = r.U8();
      m.text = r.Str();
      out = std::move(m);
      break;
    }
    case MsgType::kUnregister: {
      Unregister m;
      m.handle = r.U32();
      out = m;
      break;
    }
    case MsgType::kEvent: {
      Event m;
      GetElementPayload(&r, &m);
      out = std::move(m);
      break;
    }
    case MsgType::kRequest: {
      Request m;
      GetElementPayload(&r, &m);
      out = std::move(m);
      break;
    }
    case MsgType::kResponse: {
      Response m;
      GetElementPayload(&r, &m);
      out = std::move(m);
      break;
    }
    case MsgType::kSendError: {
      SendError m;
      m.handle = r.U32();
      m.element_id = r.U16();
      m.seq = r.U16();
      const auto c = r.U8();
      if (c < 1 || c > static_cast<std::uint8_t>(SendErrc::kQueueFull)) {
        return Invalid("SendError: nieznany kod");
      }
      m.code = static_cast<SendErrc>(c);
      out = m;
      break;
    }
    case MsgType::kEventIn: {
      EventIn m;
      GetDelivery(&r, &m);
      out = std::move(m);
      break;
    }
    case MsgType::kRequestIn: {
      RequestIn m;
      GetDelivery(&r, &m);
      out = std::move(m);
      break;
    }
    case MsgType::kResponseIn: {
      ResponseIn m;
      GetDelivery(&r, &m);
      out = std::move(m);
      break;
    }
    case MsgType::kLinkStatus: {
      LinkStatus m;
      m.handle = r.U32();
      m.link = r.Str();
      m.ready = r.Bool();
      out = std::move(m);
      break;
    }
    default:
      return Invalid("nieznany typ komunikatu " + std::to_string(packet[0]));
  }
  if (!r.ok()) {
    return Invalid("komunikat urwany");
  }
  if (!r.done()) {
    return Invalid("nadmiarowe bajty w komunikacie");
  }
  return out;
}

}  // namespace daemon
}  // namespace mavlink
}  // namespace com
}  // namespace ara
