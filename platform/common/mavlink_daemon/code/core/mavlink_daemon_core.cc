/**
 * Copyright (c) 2024
 *
 * @file mavlink_daemon_core.cc
 */
#include "platform/common/mavlink_daemon/code/core/mavlink_daemon_core.h"

#include <algorithm>
#include <chrono>  // NOLINT
#include <optional>
#include <utility>
#include <variant>

#include "ara/com/mavlink/mavlink_frame.h"
#include "ara/com/mavlink/serial_radio.h"
#include "ara/com/mavlink/transport_error.h"

namespace srp {
namespace mavlink_daemon {
namespace {
namespace dp = ara::com::mavlink::daemon;
using ara::com::mavlink::ExpandPayload;
using ara::com::mavlink::Frame;
using ara::com::mavlink::LogLevel;
using Clock = std::chrono::steady_clock;

struct Header {
  std::uint16_t seq{0};
  std::uint16_t instance{0};
  std::uint8_t target_system{0};
  std::uint8_t target_component{0};
  std::uint8_t status{0};
};

std::uint16_t Le16(const std::vector<std::uint8_t>& p, std::uint8_t off) {
  if (off == dp::kNoOffset) {
    return 0;
  }
  return static_cast<std::uint16_t>(p[off] | (p[off + 1] << 8));
}

std::uint8_t U8At(const std::vector<std::uint8_t>& p, std::uint8_t off) {
  return off == dp::kNoOffset ? std::uint8_t{0} : p[off];
}

Header ReadHeader(const dp::MessageDesc& d, const std::vector<std::uint8_t>& full) {
  Header h;
  h.seq = Le16(full, d.seq_offset);
  h.instance = Le16(full, d.instance_offset);
  h.target_system = U8At(full, d.target_system_offset);
  h.target_component = U8At(full, d.target_component_offset);
  h.status = U8At(full, d.status_offset);
  return h;
}

void PutLe16(std::vector<std::uint8_t>* p, std::uint8_t off, std::uint16_t v) {
  if (off != dp::kNoOffset) {
    (*p)[off] = static_cast<std::uint8_t>(v & 0xFF);
    (*p)[off + 1] = static_cast<std::uint8_t>(v >> 8);
  }
}

void PutU8(std::vector<std::uint8_t>* p, std::uint8_t off, std::uint8_t v) {
  if (off != dp::kNoOffset) {
    (*p)[off] = v;
  }
}

bool SameDesc(const dp::MessageDesc& a, const dp::MessageDesc& b) {
  return a.msg_id == b.msg_id && a.crc_extra == b.crc_extra && a.full_len == b.full_len &&
         a.seq_offset == b.seq_offset && a.instance_offset == b.instance_offset &&
         a.target_system_offset == b.target_system_offset &&
         a.target_component_offset == b.target_component_offset && a.status_offset == b.status_offset;
}

/// RESP bez treści (status != 0) dla elementu metody.
OutFrame BuildStatusResponse(const dp::ElementDesc& el, std::uint16_t seq, std::uint16_t instance,
                             std::uint8_t to_sys, std::uint8_t to_comp, std::uint8_t status) {
  OutFrame m;
  m.msg_id = el.resp.msg_id;
  m.crc_extra = el.resp.crc_extra;
  m.payload.assign(el.resp.full_len, 0);
  PutLe16(&m.payload, el.resp.seq_offset, seq);
  PutLe16(&m.payload, el.resp.instance_offset, instance);
  PutU8(&m.payload, el.resp.target_system_offset, to_sys);
  PutU8(&m.payload, el.resp.target_component_offset, to_comp);
  PutU8(&m.payload, el.resp.status_offset, status);
  return m;
}

const char* KindName(int kind) {
  switch (kind) {
    case 0: return "event";
    case 1: return "REQ";
    default: return "RESP";
  }
}
}  // namespace

MavlinkDaemonCore::MavlinkDaemonCore(DaemonConfig cfg, DaemonOptions opts)
    : cfg_{std::move(cfg)}, opts_{std::move(opts)}, registry_{opts_.log}, loader_{&registry_, opts_.log} {}

MavlinkDaemonCore::~MavlinkDaemonCore() { Stop(); }

void MavlinkDaemonCore::Log(LogLevel l, const std::string& m) const {
  if (opts_.log) {
    opts_.log(l, "[mavlink_daemon] " + m);
  }
}

// ------------------------------------------------------------------ start / stop

ara::core::Result<void> MavlinkDaemonCore::Start() {
  if (started_) {
    return {};
  }
  if (used_) {
    return ara::com::mavlink::MakeTransportError(
        ara::com::mavlink::TransportErrc::kPersistent,
        "MavlinkDaemonCore jest jednorazowy: Start po Stop wymaga nowego obiektu");
  }
  used_ = true;
  std::string problems;
  const auto fail = [&](const std::string& p) {
    Log(LogLevel::kError, p);
    problems += (problems.empty() ? "" : "\n") + p;
  };
  if (const auto r = registry_.AddBuiltin(ara::com::mavlink::kSerialRadioId, ara::com::mavlink::SerialRadioFactory());
      !r.HasValue()) {
    fail(r.Error().SupportData());
  }
  if (opts_.extra_drivers) {
    opts_.extra_drivers(&registry_);
  }
  if (!cfg_.driver_dir.empty()) {
    if (const auto r = loader_.LoadDirectory(cfg_.driver_dir); !r.HasValue()) {
      problems += (problems.empty() ? "" : "\n") + r.Error().SupportData();  // loader zalogował szczegóły
    }
  }
  if (cfg_.links.empty()) {
    Log(LogLevel::kWarn, "konfiguracja bez łączy — daemon nic nie będzie nadawał");
  }
  std::map<std::string, std::shared_ptr<ara::com::mavlink::IRadioDriver>> drivers;
  for (const auto& l : cfg_.links) {
    auto d = registry_.Create(l.driver, l.driver_config);
    if (!d.HasValue()) {
      fail("łącze '" + l.name + "': " + d.Error().SupportData());
      continue;
    }
    drivers[l.name] = d.Value();
  }
  if (!problems.empty()) {
    return ara::com::mavlink::MakeTransportError(ara::com::mavlink::TransportErrc::kPluginError, problems);
  }
  for (const auto& l : cfg_.links) {
    RadioLinkConfig lc;
    lc.name = l.name;
    lc.version = l.version;
    lc.heartbeat_hz = l.heartbeat_hz;
    lc.signing_enabled = l.signing_enabled;
    lc.key_file = l.key_file;
    lc.signing_link_id = l.signing_link_id;
    lc.accept_unsigned = l.accept_unsigned;
    lc.state_file = l.state_file;
    lc.system_id = cfg_.system_id;
    lc.component_id = cfg_.component_id;
    lc.restart_delay = cfg_.restart_delay;
    lc.busy_warn_after = opts_.busy_warn_after;
    links_[l.name] = std::make_shared<RadioLink>(
        lc, drivers[l.name], [this](std::uint32_t id) { return CrcLookup(id); },
        [this](const std::string& name, const Frame& f) { OnFrame(name, f); },
        [this](const std::string& name, bool ready) { OnLinkReady(name, ready); }, opts_.log, opts_.now);
  }
  if (const auto r = server_.Listen(cfg_.socket_path, static_cast<mode_t>(cfg_.socket_mode)); !r.HasValue()) {
    links_.clear();
    const std::string p = "gniazdo IPC: " + r.Error().SupportData();
    Log(LogLevel::kError, p);
    return ara::com::mavlink::MakeTransportError(ara::com::mavlink::TransportErrc::kPersistent, p);
  }
  started_ = true;
  stopping_ = false;
  for (auto& kv : links_) {
    kv.second->Start();
  }
  accept_thread_ = std::thread([this] { AcceptLoop(); });
  Log(LogLevel::kInfo, "daemon gotowy: system_id=" + std::to_string(cfg_.system_id) + " component_id=" +
                           std::to_string(cfg_.component_id) + " łączy=" + std::to_string(links_.size()) +
                           " gniazdo=" + cfg_.socket_path);
  return {};
}

void MavlinkDaemonCore::Stop() {
  if (!started_) {
    return;
  }
  stopping_ = true;
  if (accept_thread_.joinable()) {
    accept_thread_.join();
  }
  std::vector<std::shared_ptr<Session>> sessions;
  {
    std::lock_guard<std::mutex> lock{mutex_};
    sessions = sessions_;
  }
  for (const auto& s : sessions) {
    s->socket.Shutdown();
  }
  for (const auto& s : sessions) {
    if (s->thread.joinable()) {
      s->thread.join();
    }
  }
  {
    std::lock_guard<std::mutex> lock{mutex_};
    sessions_.clear();
  }
  server_.Close();
  for (auto& kv : links_) {
    kv.second->Stop();
  }
  started_ = false;
}

// ------------------------------------------------------------------ introspekcja

std::shared_ptr<RadioLink> MavlinkDaemonCore::FindLink(const std::string& name) const {
  const auto it = links_.find(name);
  return it == links_.end() ? nullptr : it->second;
}

std::size_t MavlinkDaemonCore::instance_count() const {
  std::lock_guard<std::mutex> lock{mutex_};
  return instances_.size();
}

std::size_t MavlinkDaemonCore::session_count() const {
  std::lock_guard<std::mutex> lock{mutex_};
  return static_cast<std::size_t>(
      std::count_if(sessions_.begin(), sessions_.end(), [](const std::shared_ptr<Session>& s) { return !s->done; }));
}

bool MavlinkDaemonCore::IsMsgIdRegistered(std::uint32_t msg_id) const {
  std::lock_guard<std::mutex> lock{mutex_};
  return messages_.count(msg_id) != 0;
}

DaemonCounters MavlinkDaemonCore::counters() const {
  DaemonCounters c;
  c.duplicates = duplicates_.load();
  c.unknown_messages = unknown_.load();
  c.deliver_failed = deliver_failed_.load();
  c.register_rejected = register_rejected_.load();
  c.send_rejected = send_rejected_.load();
  return c;
}

// ------------------------------------------------------------------ sesje IPC

void MavlinkDaemonCore::ReapSessions() {
  std::vector<std::shared_ptr<Session>> finished;
  {
    std::lock_guard<std::mutex> lock{mutex_};
    for (const auto& s : sessions_) {
      if (s->done.load()) {
        finished.push_back(s);
      }
    }
  }
  for (const auto& s : finished) {
    if (s->thread.joinable()) {
      s->thread.join();  // z wątku accept, nigdy z własnego
    }
  }
  if (!finished.empty()) {
    // Kasujemy TYLKO sesje, które właśnie dołączyliśmy: inna mogła skończyć się w międzyczasie,
    // a niszczenie Session z joinowalnym wątkiem to std::terminate.
    std::lock_guard<std::mutex> lock{mutex_};
    sessions_.erase(std::remove_if(sessions_.begin(), sessions_.end(),
                                   [&finished](const std::shared_ptr<Session>& s) {
                                     return std::find(finished.begin(), finished.end(), s) != finished.end();
                                   }),
                    sessions_.end());
  }
}

void MavlinkDaemonCore::AcceptLoop() {
  while (!stopping_.load()) {
    auto acc = server_.Accept(100);
    if (!acc.HasValue()) {
      Log(LogLevel::kError, "accept: " + acc.Error().SupportData());
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    } else if (acc.Value().has_value()) {
      auto s = std::make_shared<Session>();
      s->socket = *acc.Value();
      {
        std::lock_guard<std::mutex> lock{mutex_};
        s->id = next_session_id_++;
        sessions_.push_back(s);
      }
      Log(LogLevel::kInfo, s->label() + " połączona");
      s->thread = std::thread([this, s] { SessionLoop(s); });
    }
    ReapSessions();
  }
}

void MavlinkDaemonCore::SessionLoop(const std::shared_ptr<Session>& s) {
  namespace d = ara::com::mavlink::daemon;
  while (!stopping_.load()) {
    auto r = s->socket.Receive(100);
    if (r.status == d::RecvStatus::kTimeout) {
      continue;
    }
    if (r.status == d::RecvStatus::kProtocolError) {
      Log(LogLevel::kWarn, s->label() + ": błąd protokołu — rozłączam");
      break;
    }
    if (r.status == d::RecvStatus::kClosed) {
      break;
    }
    const auto& m = *r.message;
    bool unexpected = false;
    if (const auto* x = std::get_if<d::RegisterInstance>(&m)) {
      HandleRegister(s, *x);
    } else if (const auto* x = std::get_if<d::Unregister>(&m)) {
      HandleUnregister(s, x->handle);
    } else if (const auto* x = std::get_if<d::Event>(&m)) {
      HandleEvent(s, *x);
    } else if (const auto* x = std::get_if<d::Request>(&m)) {
      HandleRequest(s, *x);
    } else if (const auto* x = std::get_if<d::Response>(&m)) {
      HandleResponse(s, *x);
    } else {
      unexpected = true;
    }
    if (unexpected) {
      Log(LogLevel::kWarn, s->label() + ": komunikat typu daemon->aplikacja od aplikacji — rozłączam");
      break;
    }
  }
  CleanupSession(s);
  s->socket.Shutdown();
  Log(LogLevel::kInfo, s->label() + " rozłączona");
  s->done = true;
}

void MavlinkDaemonCore::CleanupSession(const std::shared_ptr<Session>& s) {
  std::vector<std::shared_ptr<Instance>> removed;
  {
    std::lock_guard<std::mutex> lock{mutex_};
    for (auto& kv : s->by_handle) {
      RemoveInstanceLocked(kv.second);
      removed.push_back(kv.second);
    }
    s->by_handle.clear();
  }
  for (const auto& inst : removed) {
    for (const auto& l : inst->links) {
      l->DropEvents(~std::uint64_t{0xFFFF}, inst->id << 16);
    }
  }
}

void MavlinkDaemonCore::RemoveInstanceLocked(const std::shared_ptr<Instance>& inst) {
  inst->active = false;
  instances_.erase(std::remove(instances_.begin(), instances_.end(), inst), instances_.end());
  const auto release = [this](const dp::MessageDesc& d) {
    const auto it = messages_.find(d.msg_id);
    if (it != messages_.end() && --it->second.refs <= 0) {
      messages_.erase(it);
    }
  };
  for (const auto& el : inst->reg.elements) {
    release(el.req);
    if (el.has_resp) {
      release(el.resp);
    }
  }
}

std::shared_ptr<MavlinkDaemonCore::Instance> MavlinkDaemonCore::FindInstance(const std::shared_ptr<Session>& s,
                                                                             std::uint32_t handle) const {
  std::lock_guard<std::mutex> lock{mutex_};
  const auto it = s->by_handle.find(handle);
  return it == s->by_handle.end() ? nullptr : it->second;
}

// ------------------------------------------------------------------ rejestracja

std::string MavlinkDaemonCore::CheckMsgConflictsLocked(const dp::RegisterInstance& r) const {
  struct Use {
    MsgInfo::Kind kind;
    std::uint16_t element;
    const dp::MessageDesc* desc;
  };
  std::map<std::uint32_t, Use> local;
  std::string problems;
  const auto check = [&](const dp::MessageDesc& d, MsgInfo::Kind kind, std::uint16_t element) {
    const std::string what = "msg_id " + std::to_string(d.msg_id) + " (" + d.name + ", " + KindName(kind) +
                             " elementu " +
                             std::to_string(element) + " usługi '" + r.com_id + "')";
    if (d.msg_id == ara::com::mavlink::kHeartbeatMsgId) {
      problems += (problems.empty() ? "" : "; ") + what + ": msg_id 0 jest zarezerwowany dla HEARTBEAT";
      return;
    }
    const auto lit = local.find(d.msg_id);
    if (lit != local.end()) {
      if (lit->second.kind != kind || lit->second.element != element || !SameDesc(*lit->second.desc, d)) {
        problems += (problems.empty() ? "" : "; ") + what + ": użyty dwa razy w jednej rejestracji";
      }
      return;
    }
    local[d.msg_id] = Use{kind, element, &d};
    const auto it = messages_.find(d.msg_id);
    if (it == messages_.end()) {
      return;
    }
    const MsgInfo& e = it->second;
    if (e.com_id != r.com_id) {
      problems += (problems.empty() ? "" : "; ") + what + ": zajęty przez usługę '" + e.com_id + "' (" +
                  e.desc.name + ")";
    } else if (e.kind != kind || e.element_id != element || !SameDesc(e.desc, d)) {
      problems += (problems.empty() ? "" : "; ") + what + ": inna definicja niż zarejestrowana (" + e.desc.name + ", " +
                  KindName(e.kind) + " elementu " + std::to_string(e.element_id) + ")";
    }
  };
  for (const auto& el : r.elements) {
    check(el.req, el.is_event ? MsgInfo::kEvent : MsgInfo::kReq, el.id);
    if (el.has_resp) {
      check(el.resp, MsgInfo::kResp, el.id);
    }
  }
  return problems;
}

void MavlinkDaemonCore::AddMessagesLocked(const dp::RegisterInstance& r) {
  const auto add = [&](const dp::MessageDesc& d, MsgInfo::Kind kind, std::uint16_t element) {
    auto it = messages_.find(d.msg_id);
    if (it != messages_.end()) {
      ++it->second.refs;
      return;
    }
    MsgInfo info;
    info.desc = d;
    info.kind = kind;
    info.com_id = r.com_id;
    info.element_id = element;
    info.refs = 1;
    messages_[d.msg_id] = std::move(info);
  };
  for (const auto& el : r.elements) {
    add(el.req, el.is_event ? MsgInfo::kEvent : MsgInfo::kReq, el.id);
    if (el.has_resp) {
      add(el.resp, MsgInfo::kResp, el.id);
    }
  }
}

void MavlinkDaemonCore::HandleRegister(const std::shared_ptr<Session>& s, const dp::RegisterInstance& r) {
  dp::RegisterReply reply;
  reply.handle = r.handle;
  reply.system_id = cfg_.system_id;
  reply.component_id = cfg_.component_id;
  const auto reject = [&](dp::RegisterStatus st, const std::string& text) {
    reply.status = st;
    reply.text = text.size() > dp::kMaxStringLen ? text.substr(0, dp::kMaxStringLen) : text;
    ++register_rejected_;
    Log(LogLevel::kWarn, s->label() + ": odrzucono rejestrację '" + r.com_id + "' instancja " +
                             std::to_string(r.instance) + ": " + text);
    s->socket.Send(reply);
  };
  if (r.protocol_version != dp::kProtocolVersion) {
    reject(dp::RegisterStatus::kBadVersion, "wersja protokołu " + std::to_string(r.protocol_version) +
                                               ", daemon obsługuje " + std::to_string(dp::kProtocolVersion));
    return;
  }
  if (const auto err = dp::Validate(r); !err.empty()) {
    reject(dp::RegisterStatus::kInvalid, err);
    return;
  }
  std::vector<std::shared_ptr<RadioLink>> links;
  for (const auto& name : r.links) {
    const auto link = FindLink(name);
    if (!link) {
      std::string known;
      for (const auto& kv : links_) {
        known += (known.empty() ? "" : ", ") + kv.first;
      }
      reject(dp::RegisterStatus::kUnknownLink, "nieznane łącze '" + name + "' (w configu daemona: " +
                                                   (known.empty() ? "brak" : known) + ")");
      return;
    }
    links.push_back(link);
  }
  auto inst = std::make_shared<Instance>();
  std::optional<std::pair<dp::RegisterStatus, std::string>> rejection;
  {
    std::lock_guard<std::mutex> lock{mutex_};
    if (s->by_handle.count(r.handle) != 0) {
      rejection.emplace(dp::RegisterStatus::kDuplicateHandle,
                        "handle " + std::to_string(r.handle) + " już użyty na tym połączeniu");
    }
    for (const auto& other : instances_) {
      if (!rejection && other->reg.com_id == r.com_id && other->reg.instance == r.instance &&
          other->reg.role == r.role) {
        rejection.emplace(dp::RegisterStatus::kConflictInstance,
                          std::string("instancja ") + std::to_string(r.instance) + " usługi '" + r.com_id + "' (" +
                              (r.role == dp::Role::kProvide ? "provide" : "require") +
                              ") jest już zarejestrowana przez " + other->session_label);
      }
    }
    if (!rejection) {
      if (const auto conflict = CheckMsgConflictsLocked(r); !conflict.empty()) {
        rejection.emplace(dp::RegisterStatus::kConflictMsgId, conflict);
      }
    }
    if (!rejection) {
      AddMessagesLocked(r);
      inst->id = next_instance_id_++;
      inst->session = s;
      inst->session_label = s->label();
      inst->reg = r;
      inst->links = links;
      for (std::size_t i = 0; i < r.elements.size(); ++i) {
        inst->element_index[r.elements[i].id] = i;
      }
      instances_.push_back(inst);
      s->by_handle[r.handle] = inst;
    }
  }
  if (rejection) {
    reject(rejection->first, rejection->second);  // odpowiedź poza mutexem rdzenia
    return;
  }
  reply.status = dp::RegisterStatus::kOk;
  Log(LogLevel::kInfo, s->label() + ": zarejestrowano '" + r.com_id + "' instancja " + std::to_string(r.instance) +
                           (r.role == dp::Role::kProvide ? " (provide)" : " (require)") + ", elementów " +
                           std::to_string(r.elements.size()));
  if (!s->socket.Send(reply).HasValue()) {
    return;  // rozłączona: sesja sprzątnie rejestrację
  }
  {
    std::lock_guard<std::mutex> lock{inst->status_mutex};
    inst->active = true;  // od teraz dostarczamy ramki i LinkStatus
    for (const auto& l : inst->links) {
      dp::LinkStatus st;
      st.handle = r.handle;
      st.link = l->config().name;
      st.ready = l->IsReady();
      s->socket.Send(st);
    }
  }
}

void MavlinkDaemonCore::HandleUnregister(const std::shared_ptr<Session>& s, std::uint32_t handle) {
  std::shared_ptr<Instance> inst;
  {
    std::lock_guard<std::mutex> lock{mutex_};
    const auto it = s->by_handle.find(handle);
    if (it == s->by_handle.end()) {
      return;
    }
    inst = it->second;
    s->by_handle.erase(it);
    RemoveInstanceLocked(inst);
  }
  for (const auto& l : inst->links) {
    l->DropEvents(~std::uint64_t{0xFFFF}, inst->id << 16);
  }
  Log(LogLevel::kInfo, s->label() + ": wyrejestrowano '" + inst->reg.com_id + "' instancja " +
                           std::to_string(inst->reg.instance));
}

// ------------------------------------------------------------------ TX (app -> radio)

void MavlinkDaemonCore::HandleEvent(const std::shared_ptr<Session>& s, const dp::Event& m) {
  const auto inst = FindInstance(s, m.handle);
  if (!inst || inst->reg.role != dp::Role::kProvide) {
    ++send_rejected_;
    return;
  }
  const auto it = inst->element_index.find(m.element_id);
  if (it == inst->element_index.end()) {
    ++send_rejected_;
    return;
  }
  const auto& el = inst->reg.elements[it->second];
  if (!el.is_event || m.payload.size() != el.req.full_len) {
    ++send_rejected_;
    return;
  }
  if (!el.available) {
    return;  // wykluczony: Update jest no-op
  }
  OutFrame f;
  f.msg_id = el.req.msg_id;
  f.crc_extra = el.req.crc_extra;
  f.payload = m.payload;
  const std::uint64_t key = (inst->id << 16) | el.id;
  for (const auto& l : inst->links) {
    l->EnqueueEvent(key, f, std::chrono::milliseconds(el.min_interval_ms));
  }
}

void MavlinkDaemonCore::HandleRequest(const std::shared_ptr<Session>& s, const dp::Request& m) {
  const auto inst = FindInstance(s, m.handle);
  std::uint16_t seq = 0;
  const dp::ElementDesc* el = nullptr;
  if (inst && inst->reg.role == dp::Role::kRequire) {
    const auto it = inst->element_index.find(m.element_id);
    if (it != inst->element_index.end()) {
      el = &inst->reg.elements[it->second];
    }
  }
  const auto reject = [&](dp::SendErrc code) {
    ++send_rejected_;
    dp::SendError e;
    e.handle = m.handle;
    e.element_id = m.element_id;
    e.seq = seq;
    e.code = code;
    s->socket.Send(e);
  };
  if (el == nullptr || el->is_event || m.payload.size() != el->req.full_len) {
    reject(dp::SendErrc::kUnknownElement);
    return;
  }
  seq = Le16(m.payload, el->req.seq_offset);
  if (!el->available) {
    reject(dp::SendErrc::kUnavailable);
    return;
  }
  std::vector<std::shared_ptr<RadioLink>> ready;
  for (const auto& l : inst->links) {
    if (l->IsReady()) {
      ready.push_back(l);
    }
  }
  if (ready.empty()) {
    reject(dp::SendErrc::kNoLink);  // metoda bez łącza = natychmiast błąd
    return;
  }
  OutFrame f;
  f.msg_id = el->req.msg_id;
  f.crc_extra = el->req.crc_extra;
  f.payload = m.payload;
  f.deadline = Clock::now() + std::chrono::milliseconds(el->timeout_ms);
  bool accepted = false;
  for (const auto& l : ready) {
    accepted = l->EnqueueRequest(f) || accepted;
  }
  if (!accepted) {
    reject(dp::SendErrc::kQueueFull);
  }
}

void MavlinkDaemonCore::HandleResponse(const std::shared_ptr<Session>& s, const dp::Response& m) {
  const auto inst = FindInstance(s, m.handle);
  if (!inst || inst->reg.role != dp::Role::kProvide) {
    ++send_rejected_;
    return;
  }
  const auto it = inst->element_index.find(m.element_id);
  if (it == inst->element_index.end()) {
    ++send_rejected_;
    return;
  }
  const auto& el = inst->reg.elements[it->second];
  if (el.is_event || !el.has_resp || m.payload.size() != el.resp.full_len) {
    ++send_rejected_;
    return;
  }
  OutFrame f;
  f.msg_id = el.resp.msg_id;
  f.crc_extra = el.resp.crc_extra;
  f.payload = m.payload;
  for (const auto& l : inst->links) {
    l->EnqueueResponse(f);  // RESP na wszystkie łącza instancji (redundancja)
  }
}

// ------------------------------------------------------------------ RX (radio -> app)

std::optional<std::uint8_t> MavlinkDaemonCore::CrcLookup(std::uint32_t msg_id) const {
  std::lock_guard<std::mutex> lock{mutex_};
  const auto it = messages_.find(msg_id);
  if (it == messages_.end()) {
    return std::nullopt;
  }
  return it->second.desc.crc_extra;
}

bool MavlinkDaemonCore::Deliver(const std::shared_ptr<Instance>& inst, const dp::Message& m) {
  const auto s = inst->session.lock();
  if (!s) {
    return false;
  }
  const auto r = s->socket.Send(m, opts_.deliver_timeout_ms);
  if (!r.HasValue()) {
    ++deliver_failed_;
    Log(LogLevel::kWarn, s->label() + " nie odbiera (" + r.Error().SupportData() + ") — rozłączam");
    s->socket.Shutdown();  // wątek sesji zobaczy zamknięcie i sprzątnie rejestracje
    return false;
  }
  return true;
}

void MavlinkDaemonCore::OnLinkReady(const std::string& link_name, bool ready) {
  std::vector<std::shared_ptr<Instance>> eps;
  {
    std::lock_guard<std::mutex> lock{mutex_};
    eps = instances_;
  }
  for (const auto& inst : eps) {
    const bool uses = std::any_of(inst->links.begin(), inst->links.end(), [&](const std::shared_ptr<RadioLink>& l) {
      return l->config().name == link_name;
    });
    if (!uses) {
      continue;
    }
    std::lock_guard<std::mutex> lock{inst->status_mutex};
    if (!inst->active.load()) {
      continue;  // stan początkowy poleci razem z rejestracją
    }
    dp::LinkStatus st;
    st.handle = inst->reg.handle;
    st.link = link_name;
    st.ready = ready;
    Deliver(inst, st);
  }
}

void MavlinkDaemonCore::SendUnknownInstanceResponse(const dp::ElementDesc& el, std::uint16_t seq,
                                                    std::uint16_t instance, std::uint8_t to_sys,
                                                    std::uint8_t to_comp,
                                                    const std::shared_ptr<RadioLink>& link) {
  if (link) {
    link->EnqueueResponse(BuildStatusResponse(el, seq, instance, to_sys, to_comp, 2));
  }
}

void MavlinkDaemonCore::OnFrame(const std::string& link_name, const Frame& f) {
  MsgInfo info;
  std::vector<std::shared_ptr<Instance>> eps;
  {
    std::lock_guard<std::mutex> lock{mutex_};
    const auto it = messages_.find(f.msgid);
    if (it == messages_.end()) {
      ++unknown_;
      return;
    }
    info = it->second;
    eps = instances_;
  }
  if (f.payload.size() > info.desc.full_len) {
    ++unknown_;
    return;
  }
  const auto full = ExpandPayload(f.payload, info.desc.full_len);
  const Header h = ReadHeader(info.desc, full);
  const bool want_provide = info.kind == MsgInfo::kReq;

  // Instancje właściwej roli, które znają tę wiadomość.
  std::vector<std::shared_ptr<Instance>> candidates;
  for (const auto& inst : eps) {
    if ((inst->reg.role == dp::Role::kProvide) != want_provide || inst->reg.com_id != info.com_id ||
        !inst->active.load()) {
      continue;
    }
    if (inst->element_index.count(info.element_id) != 0) {
      candidates.push_back(inst);
    }
  }
  if (candidates.empty()) {
    return;  // nie jesteśmy odbiorcą tej usługi
  }
  std::shared_ptr<Instance> target;
  for (const auto& inst : candidates) {
    if (inst->reg.instance == h.instance) {
      target = inst;
      break;
    }
  }
  const auto arrival = FindLink(link_name);
  if (!target) {
    // Znana wiadomość, nieznana instancja: REQ z RESP -> RESP status=2 łączem, którym przyszło REQ.
    const auto& inst = candidates.front();
    ++inst->rx_rejected;
    if (info.kind == MsgInfo::kReq) {
      const auto& el = inst->reg.elements[inst->element_index.at(info.element_id)];
      if (el.has_resp && (h.target_system == 0 || h.target_system == cfg_.system_id) &&
          (h.target_component == 0 || h.target_component == cfg_.component_id)) {
        SendUnknownInstanceResponse(el, h.seq, h.instance, f.sysid, f.compid, arrival);
      }
    }
    return;
  }
  const auto& inst = target;
  // Ramka przyjmowana tylko z łącza należącego do instancji: podpis/ochrona łącza jednej
  // instancji nie może być obejściem przez łącze innej.
  const bool on_instance_link =
      std::any_of(inst->links.begin(), inst->links.end(),
                  [&](const std::shared_ptr<RadioLink>& l) { return l->config().name == link_name; });
  if (!on_instance_link) {
    ++inst->rx_rejected;
    return;
  }
  // Filtry targetu / źródła.
  if (info.kind == MsgInfo::kReq) {
    if ((h.target_system != 0 && h.target_system != cfg_.system_id) ||
        (h.target_component != 0 && h.target_component != cfg_.component_id)) {
      return;  // nie do nas
    }
  } else if (info.kind == MsgInfo::kResp) {
    if (h.target_system != cfg_.system_id || h.target_component != cfg_.component_id) {
      return;
    }
  } else {
    if ((inst->reg.source_system != 0 && f.sysid != inst->reg.source_system) ||
        (inst->reg.source_component != 0 && f.compid != inst->reg.source_component)) {
      return;
    }
  }
  // Dedup: seq + CRC32 payloadu w oknie TTL (kopie z wielu łączy są bajtowo identyczne).
  const ara::com::mavlink::DedupTable::Key key{f.sysid, f.compid, f.msgid, h.instance,
                                               info.kind == MsgInfo::kResp ? h.target_system : std::uint8_t{0},
                                               info.kind == MsgInfo::kResp ? h.target_component : std::uint8_t{0}};
  if (dedup_.TestAndInsert(key, h.seq, ara::com::mavlink::Crc32(f.payload),
                           std::chrono::milliseconds(inst->reg.dedup_ttl_ms), Clock::now())) {
    ++duplicates_;
    ++inst->rx_duplicate;
    return;
  }
  const auto& el = inst->reg.elements[inst->element_index.at(info.element_id)];
  if (!el.available) {
    ++inst->rx_rejected;  // element wyłączony w tej instancji
    if (info.kind == MsgInfo::kReq && el.has_resp) {
      const auto resp = BuildStatusResponse(el, h.seq, h.instance, f.sysid, f.compid, 2);
      for (const auto& l : inst->links) {
        l->EnqueueResponse(resp);
      }
    }
    return;
  }
  switch (info.kind) {
    case MsgInfo::kEvent: {
      dp::EventIn m;
      m.handle = inst->reg.handle;
      m.element_id = el.id;
      m.sender_system = f.sysid;
      m.sender_component = f.compid;
      m.link = link_name;
      m.payload = full;
      Deliver(inst, m);
      break;
    }
    case MsgInfo::kReq: {
      dp::RequestIn m;
      m.handle = inst->reg.handle;
      m.element_id = el.id;
      m.sender_system = f.sysid;
      m.sender_component = f.compid;
      m.link = link_name;
      m.payload = full;
      Deliver(inst, m);
      break;
    }
    case MsgInfo::kResp: {
      dp::ResponseIn m;
      m.handle = inst->reg.handle;
      m.element_id = el.id;
      m.sender_system = f.sysid;
      m.sender_component = f.compid;
      m.link = link_name;
      m.payload = full;
      Deliver(inst, m);
      break;
    }
  }
}

}  // namespace mavlink_daemon
}  // namespace srp
