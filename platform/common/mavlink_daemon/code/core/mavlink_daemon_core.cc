/**
 * Copyright (c) 2024
 *
 * @file mavlink_daemon_core.cc
 */
#include "platform/common/mavlink_daemon/code/core/mavlink_daemon_core.h"

#include <algorithm>
#include <chrono>  // NOLINT
#include <optional>
#include <tuple>
#include <utility>
#include <variant>

#include "ara/com/mavlink/mavlink_frame.h"
#include "ara/com/mavlink/serial_radio.h"
#include "ara/com/mavlink/transport_error.h"
#include "core/common/condition.h"

namespace srp {
namespace mavlink_daemon {
namespace {
namespace dp = ara::com::mavlink::daemon;
using ara::com::mavlink::ExpandPayload;
using ara::com::mavlink::Frame;
using ara::com::mavlink::LogLevel;
using Clock = std::chrono::steady_clock;

/// A write to an application waits at most this long for room in its socket buffer; a slower
/// application is disconnected (it can never block a link: sending runs on its own thread).
constexpr std::uint32_t kDeliverTimeoutMs = 500;
/// Messages waiting for one application; beyond this it is disconnected.
constexpr std::size_t kMaxQueuedPerSession = 256;
constexpr auto kAcceptRetryPause = std::chrono::milliseconds(200);

bool SameDesc(const dp::MessageDesc& a, const dp::MessageDesc& b) {
  return a.msg_id == b.msg_id && a.crc_extra == b.crc_extra && a.full_len == b.full_len &&
         a.seq_offset == b.seq_offset && a.instance_offset == b.instance_offset &&
         a.target_system_offset == b.target_system_offset &&
         a.target_component_offset == b.target_component_offset && a.status_offset == b.status_offset;
}

/// RESP without content (status != 0) for a method element.
OutFrame BuildStatusResponse(const dp::ElementDesc& el, std::uint16_t seq, std::uint16_t instance,
                             std::uint8_t to_sys, std::uint8_t to_comp, std::uint8_t status) {
  OutFrame m;
  m.msg_id = el.resp.msg_id;
  m.crc_extra = el.resp.crc_extra;
  m.payload.assign(el.resp.full_len, 0);
  ara::com::mavlink::MessageHeader h;
  h.seq = seq;
  h.instance = instance;
  h.target_system = to_sys;
  h.target_component = to_comp;
  h.status = status;
  dp::WriteHeader(el.resp, h, &m.payload);
  return m;
}

const char* KindName(dp::ElementKind kind) {
  switch (kind) {
    case dp::ElementKind::kEvent: return "event";
    case dp::ElementKind::kRequest: return "REQ";
    case dp::ElementKind::kResponse: return "RESP";
  }
  return "?";
}
}  // namespace

MavlinkDaemonCore::MavlinkDaemonCore(DaemonConfig cfg, DaemonOptions opts)
    : cfg_{std::move(cfg)}, opts_{std::move(opts)}, registry_{opts_.log}, loader_{&registry_, opts_.log} {
  std::ignore = registry_.AddBuiltin(ara::com::mavlink::kSerialRadioId, ara::com::mavlink::SerialRadioFactory());
}

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
  std::string problems;
  const auto fail = [&](const std::string& p) {
    Log(LogLevel::kError, p);
    problems += (problems.empty() ? "" : "\n") + p;
  };
  if (!cfg_.driver_dir.empty()) {
    if (const auto r = loader_.LoadDirectory(cfg_.driver_dir); !r.HasValue()) {
      problems += (problems.empty() ? "" : "\n") + r.Error().SupportData();  // the loader logged the details
    }
  }
  if (cfg_.links.empty()) {
    Log(LogLevel::kWarn, "config without links — the daemon will not transmit anything");
  }
  std::map<std::string, std::shared_ptr<ara::com::mavlink::IRadioDriver>> drivers;
  for (const auto& l : cfg_.links) {
    auto d = registry_.Create(l.driver, l.driver_config);
    if (!d.HasValue()) {
      fail("link '" + l.name + "': " + d.Error().SupportData());
      continue;
    }
    drivers[l.name] = d.Value();
  }
  if (!problems.empty()) {
    return ara::com::mavlink::MakeTransportError(ara::com::mavlink::TransportErrc::kPluginError, problems);
  }
  for (const auto& l : cfg_.links) {
    RadioLinkConfig lc;
    lc.link = l;
    lc.system_id = cfg_.system_id;
    lc.component_id = cfg_.component_id;
    lc.restart_delay = cfg_.restart_delay;
    links_[l.name] = std::make_shared<RadioLink>(
        lc, drivers[l.name], [this](std::uint32_t id) { return CrcLookup(id); },
        [this](const std::string& name, const Frame& f) { return OnFrame(name, f); },
        [this](const std::string& name, bool ready) { OnLinkReady(name, ready); }, opts_.log, opts_.now);
  }
  if (const auto r = server_.Listen(cfg_.socket_path, static_cast<mode_t>(cfg_.socket_mode)); !r.HasValue()) {
    links_.clear();
    const std::string p = "IPC socket: " + r.Error().SupportData();
    Log(LogLevel::kError, p);
    return ara::com::mavlink::MakeTransportError(ara::com::mavlink::TransportErrc::kPersistent, p);
  }
  started_ = true;
  stopping_ = false;
  for (auto& kv : links_) {
    kv.second->Start();
  }
  accept_thread_ = std::jthread([this](std::stop_token st) { AcceptLoop(st); });
  Log(LogLevel::kInfo, "daemon ready: system_id=" + std::to_string(cfg_.system_id) + " component_id=" +
                           std::to_string(cfg_.component_id) + " links=" + std::to_string(links_.size()) +
                           " socket=" + cfg_.socket_path);
  return {};
}

void MavlinkDaemonCore::Stop() {
  if (!started_) {
    return;
  }
  stopping_ = true;
  accept_thread_.request_stop();
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
  links_.clear();
  started_ = false;
}

// ------------------------------------------------------------------ introspection

std::shared_ptr<RadioLink> MavlinkDaemonCore::FindLink(const std::string& name) const {
  const auto it = links_.find(name);
  return it == links_.end() ? nullptr : it->second;
}

DaemonCounters MavlinkDaemonCore::counters() const {
  DaemonCounters c;
  c.duplicates = duplicates_.load();
  c.deliver_failed = deliver_failed_.load();
  c.register_rejected = register_rejected_.load();
  c.send_rejected = send_rejected_.load();
  return c;
}

// ------------------------------------------------------------------ IPC sessions

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
      s->thread.join();  // from the accept thread, never from its own
    }
  }
  if (!finished.empty()) {
    // Erase ONLY the sessions just joined: another one may have finished in the meantime, and
    // destroying a Session with a joinable thread is a std::terminate.
    std::lock_guard<std::mutex> lock{mutex_};
    sessions_.erase(std::remove_if(sessions_.begin(), sessions_.end(),
                                   [&finished](const std::shared_ptr<Session>& s) {
                                     return std::find(finished.begin(), finished.end(), s) != finished.end();
                                   }),
                    sessions_.end());
  }
}

void MavlinkDaemonCore::AcceptLoop(std::stop_token st) {
  while (!st.stop_requested()) {
    auto acc = server_.Accept(100);
    if (!acc.HasValue()) {
      Log(LogLevel::kError, "accept: " + acc.Error().SupportData());
      srp::core::condition::wait_for(kAcceptRetryPause, st);
    } else if (acc.Value().has_value()) {
      auto s = std::make_shared<Session>();
      s->socket = *acc.Value();
      {
        std::lock_guard<std::mutex> lock{mutex_};
        s->id = next_session_id_++;
        sessions_.push_back(s);
      }
      Log(LogLevel::kInfo, s->label() + " connected");
      s->writer = std::jthread([this, s](std::stop_token wst) { WriterLoop(s, wst); });
      s->thread = std::thread([this, s] { SessionLoop(s); });
    }
    ReapSessions();
  }
}

bool MavlinkDaemonCore::Enqueue(const std::shared_ptr<Session>& s, dp::Message m) {
  if (s->writer_dead.load()) {
    return false;
  }
  bool overflow = false;
  {
    std::lock_guard<std::mutex> lock{s->out_mutex};
    if (s->out.size() >= kMaxQueuedPerSession) {
      overflow = true;
    } else {
      s->out.push_back(std::move(m));
    }
  }
  if (overflow) {
    ++deliver_failed_;
    Log(LogLevel::kWarn, s->label() + " does not read its socket (" + std::to_string(kMaxQueuedPerSession) +
                             " messages queued) — disconnecting");
    s->writer_dead = true;
    s->socket.Shutdown();  // the session thread sees the close and cleans up the registrations
    return false;
  }
  s->out_cv.notify_one();
  return true;
}

void MavlinkDaemonCore::WriterLoop(const std::shared_ptr<Session>& s, std::stop_token st) {
  for (;;) {
    dp::Message m;
    {
      std::unique_lock<std::mutex> lock{s->out_mutex};
      s->out_cv.wait(lock, st, [&] { return !s->out.empty(); });
      if (st.stop_requested()) {
        return;
      }
      m = std::move(s->out.front());
      s->out.pop_front();
    }
    const auto r = s->socket.Send(m, kDeliverTimeoutMs);
    if (!r.HasValue()) {
      ++deliver_failed_;
      Log(LogLevel::kWarn, s->label() + " does not read its socket (" + r.Error().SupportData() +
                               ") — disconnecting");
      s->writer_dead = true;
      s->socket.Shutdown();  // the session thread sees the close and cleans up the registrations
      return;
    }
  }
}

void MavlinkDaemonCore::SessionLoop(const std::shared_ptr<Session>& s) {
  while (!stopping_.load()) {
    auto r = s->socket.Receive(100);
    if (r.status == dp::RecvStatus::kTimeout) {
      continue;
    }
    if (r.status == dp::RecvStatus::kProtocolError) {
      Log(LogLevel::kWarn, s->label() + ": protocol error — disconnecting");
      break;
    }
    if (r.status == dp::RecvStatus::kClosed) {
      break;
    }
    const auto& m = *r.message;
    bool unexpected = false;
    if (const auto* x = std::get_if<dp::RegisterInstance>(&m)) {
      HandleRegister(s, *x);
    } else if (const auto* x = std::get_if<dp::Unregister>(&m)) {
      HandleUnregister(s, x->handle);
    } else if (const auto* x = std::get_if<dp::ElementPayload>(&m)) {
      switch (x->kind) {
        case dp::ElementKind::kEvent: HandleEvent(s, *x); break;
        case dp::ElementKind::kRequest: HandleRequest(s, *x); break;
        case dp::ElementKind::kResponse: HandleResponse(s, *x); break;
      }
    } else {
      unexpected = true;
    }
    if (unexpected) {
      Log(LogLevel::kWarn, s->label() + ": daemon->application message received from an application — disconnecting");
      break;
    }
  }
  CleanupSession(s);
  s->socket.Shutdown();
  s->writer.request_stop();
  s->out_cv.notify_all();
  if (s->writer.joinable()) {
    s->writer.join();
  }
  Log(LogLevel::kInfo, s->label() + " disconnected");
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

// ------------------------------------------------------------------ registration

std::string MavlinkDaemonCore::CheckMsgConflictsLocked(const dp::RegisterInstance& r) const {
  struct Use {
    dp::ElementKind kind;
    std::uint16_t element;
    const dp::MessageDesc* desc;
  };
  std::map<std::uint32_t, Use> local;
  std::string problems;
  const auto check = [&](const dp::MessageDesc& d, dp::ElementKind kind, std::uint16_t element) {
    const std::string what = "msg_id " + std::to_string(d.msg_id) + " (" + d.name + ", " + KindName(kind) +
                             " of element " + std::to_string(element) + " of service '" + r.com_id + "')";
    if (d.msg_id == ara::com::mavlink::kHeartbeatMsgId) {
      problems += (problems.empty() ? "" : "; ") + what + ": msg_id 0 is reserved for HEARTBEAT";
      return;
    }
    const auto lit = local.find(d.msg_id);
    if (lit != local.end()) {
      if (lit->second.kind != kind || lit->second.element != element || !SameDesc(*lit->second.desc, d)) {
        problems += (problems.empty() ? "" : "; ") + what + ": used twice in one registration";
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
      problems += (problems.empty() ? "" : "; ") + what + ": taken by service '" + e.com_id + "' (" +
                  e.desc.name + ")";
    } else if (e.kind != kind || e.element_id != element || !SameDesc(e.desc, d)) {
      problems += (problems.empty() ? "" : "; ") + what + ": different definition than the registered one (" +
                  e.desc.name + ", " + KindName(e.kind) + " of element " + std::to_string(e.element_id) + ")";
    }
  };
  for (const auto& el : r.elements) {
    check(el.req, el.is_event ? dp::ElementKind::kEvent : dp::ElementKind::kRequest, el.id);
    if (el.has_resp) {
      check(el.resp, dp::ElementKind::kResponse, el.id);
    }
  }
  return problems;
}

void MavlinkDaemonCore::AddMessagesLocked(const dp::RegisterInstance& r) {
  const auto add = [&](const dp::MessageDesc& d, dp::ElementKind kind, std::uint16_t element) {
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
    add(el.req, el.is_event ? dp::ElementKind::kEvent : dp::ElementKind::kRequest, el.id);
    if (el.has_resp) {
      add(el.resp, dp::ElementKind::kResponse, el.id);
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
    Log(LogLevel::kWarn, s->label() + ": registration of '" + r.com_id + "' instance " +
                             std::to_string(r.instance) + " rejected: " + text);
    Enqueue(s, reply);
  };
  if (r.protocol_version != dp::kProtocolVersion) {
    reject(dp::RegisterStatus::kBadVersion, "protocol version " + std::to_string(r.protocol_version) +
                                               ", the daemon speaks " + std::to_string(dp::kProtocolVersion));
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
      reject(dp::RegisterStatus::kUnknownLink,
             "unknown link '" + name + "' (in the daemon config: " + (known.empty() ? "none" : known) + ")");
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
                        "handle " + std::to_string(r.handle) + " already used on this connection");
    }
    for (const auto& other : instances_) {
      if (!rejection && other->reg.com_id == r.com_id && other->reg.instance == r.instance &&
          other->reg.role == r.role) {
        rejection.emplace(dp::RegisterStatus::kConflictInstance,
                          std::string("instance ") + std::to_string(r.instance) + " of service '" + r.com_id + "' (" +
                              (r.role == dp::Role::kProvide ? "provide" : "require") +
                              ") is already registered by " + other->session_label);
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
    reject(rejection->first, rejection->second);  // reply outside the core mutex
    return;
  }
  reply.status = dp::RegisterStatus::kOk;
  Log(LogLevel::kInfo, s->label() + ": registered '" + r.com_id + "' instance " + std::to_string(r.instance) +
                           (r.role == dp::Role::kProvide ? " (provide)" : " (require)") + ", elements " +
                           std::to_string(r.elements.size()));
  if (!Enqueue(s, reply)) {
    return;  // disconnected: the session cleans the registration up
  }
  // The queue is drained by the writer thread, so holding status_mutex here never waits for the
  // application; the mutex only orders the initial LinkStatus against later changes.
  std::lock_guard<std::mutex> lock{inst->status_mutex};
  inst->active = true;  // from now on frames and LinkStatus are delivered
  for (const auto& l : inst->links) {
    dp::LinkStatus st;
    st.handle = r.handle;
    st.link = l->name();
    st.ready = l->IsReady();
    Enqueue(s, st);
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
  Log(LogLevel::kInfo, s->label() + ": unregistered '" + inst->reg.com_id + "' instance " +
                           std::to_string(inst->reg.instance));
}

// ------------------------------------------------------------------ TX (app -> radio)

void MavlinkDaemonCore::HandleEvent(const std::shared_ptr<Session>& s, const dp::ElementPayload& m) {
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
    return;  // excluded: Update is a no-op
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

void MavlinkDaemonCore::HandleRequest(const std::shared_ptr<Session>& s, const dp::ElementPayload& m) {
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
    Enqueue(s, e);
  };
  if (el == nullptr || el->is_event || m.payload.size() != el->req.full_len) {
    reject(dp::SendErrc::kUnknownElement);
    return;
  }
  seq = dp::ReadHeader(el->req, m.payload).seq;
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
    reject(dp::SendErrc::kNoLink);  // a method without a link fails at once
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

void MavlinkDaemonCore::HandleResponse(const std::shared_ptr<Session>& s, const dp::ElementPayload& m) {
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
    l->EnqueueResponse(f);  // RESP goes out on every link of the instance (redundancy)
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

bool MavlinkDaemonCore::Deliver(const std::shared_ptr<Instance>& inst, dp::Message m) {
  const auto s = inst->session.lock();
  return s != nullptr && Enqueue(s, std::move(m));
}

void MavlinkDaemonCore::OnLinkReady(const std::string& link_name, bool ready) {
  std::vector<std::shared_ptr<Instance>> eps;
  {
    std::lock_guard<std::mutex> lock{mutex_};
    eps = instances_;
  }
  for (const auto& inst : eps) {
    const bool uses = std::any_of(inst->links.begin(), inst->links.end(), [&](const std::shared_ptr<RadioLink>& l) {
      return l->name() == link_name;
    });
    if (!uses) {
      continue;
    }
    std::lock_guard<std::mutex> lock{inst->status_mutex};
    if (!inst->active.load()) {
      continue;  // the initial state goes out together with the registration
    }
    dp::LinkStatus st;
    st.handle = inst->reg.handle;
    st.link = link_name;
    st.ready = ready;
    Deliver(inst, st);
  }
}

bool MavlinkDaemonCore::OnFrame(const std::string& link_name, const Frame& f) {
  MsgInfo info;
  std::vector<std::shared_ptr<Instance>> eps;
  {
    std::lock_guard<std::mutex> lock{mutex_};
    const auto it = messages_.find(f.msgid);
    if (it == messages_.end()) {
      return false;
    }
    info = it->second;
    eps = instances_;
  }
  if (f.payload.size() > info.desc.full_len) {
    return false;
  }
  const auto full = ExpandPayload(f.payload, info.desc.full_len);
  const auto h = dp::ReadHeader(info.desc, full);
  const bool want_provide = info.kind == dp::ElementKind::kRequest;

  // Instances of the right role that know this message.
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
    return false;  // we are not a recipient of this service
  }
  std::shared_ptr<Instance> target;
  for (const auto& inst : candidates) {
    if (inst->reg.instance == h.instance) {
      target = inst;
      break;
    }
  }
  const bool for_us = (h.target_system == 0 || h.target_system == cfg_.system_id) &&
                      (h.target_component == 0 || h.target_component == cfg_.component_id);
  if (!target) {
    // Known message, unknown instance: a REQ with a RESP gets RESP status=2 on the arrival link.
    const auto& inst = candidates.front();
    if (info.kind == dp::ElementKind::kRequest) {
      const auto& el = inst->reg.elements[inst->element_index.at(info.element_id)];
      if (el.has_resp && for_us) {
        if (const auto arrival = FindLink(link_name)) {
          arrival->EnqueueResponse(BuildStatusResponse(el, h.seq, h.instance, f.sysid, f.compid, 2));
        }
      }
    }
    return false;
  }
  const auto& inst = target;
  // A frame is accepted only from a link that belongs to the instance: the protection of one
  // instance's link must not be bypassed through another instance's link.
  const bool on_instance_link =
      std::any_of(inst->links.begin(), inst->links.end(),
                  [&](const std::shared_ptr<RadioLink>& l) { return l->name() == link_name; });
  if (!on_instance_link) {
    return false;
  }
  // Target / source filters.
  if (info.kind == dp::ElementKind::kRequest) {
    if (!for_us) {
      return false;  // not for us
    }
  } else if (info.kind == dp::ElementKind::kResponse) {
    if (h.target_system != cfg_.system_id || h.target_component != cfg_.component_id) {
      return false;
    }
  } else {
    if ((inst->reg.source_system != 0 && f.sysid != inst->reg.source_system) ||
        (inst->reg.source_component != 0 && f.compid != inst->reg.source_component)) {
      return false;
    }
  }
  // Dedup: seq + CRC32 of the payload within the TTL window (copies from several links are
  // byte-identical).
  const bool is_resp = info.kind == dp::ElementKind::kResponse;
  const ara::com::mavlink::DedupTable::Key key{f.sysid, f.compid, f.msgid, h.instance,
                                               is_resp ? h.target_system : std::uint8_t{0},
                                               is_resp ? h.target_component : std::uint8_t{0}};
  if (dedup_.TestAndInsert(key, h.seq, ara::com::mavlink::Crc32(f.payload),
                           std::chrono::milliseconds(inst->reg.dedup_ttl_ms), Clock::now())) {
    ++duplicates_;
    return false;
  }
  const auto& el = inst->reg.elements[inst->element_index.at(info.element_id)];
  if (!el.available) {
    if (!inst->warned_excluded.exchange(true)) {
      Log(LogLevel::kWarn, inst->session_label + ": instance " + std::to_string(inst->reg.instance) + " of '" +
                               inst->reg.com_id + "' received " + KindName(info.kind) + " '" + info.desc.name +
                               "' for an excluded element — rejected (logged once per instance)");
    }
    if (info.kind == dp::ElementKind::kRequest && el.has_resp) {
      const auto resp = BuildStatusResponse(el, h.seq, h.instance, f.sysid, f.compid, 2);
      for (const auto& l : inst->links) {
        l->EnqueueResponse(resp);
      }
    }
    return false;
  }
  dp::Delivery m;
  m.kind = info.kind;
  m.handle = inst->reg.handle;
  m.element_id = el.id;
  m.sender_system = f.sysid;
  m.sender_component = f.compid;
  m.link = link_name;
  m.payload = full;
  Deliver(inst, std::move(m));
  return true;
}

}  // namespace mavlink_daemon
}  // namespace srp
