/**
 * Copyright (c) 2024
 *
 * @file link_manager.cc
 */
#include "ara/com/mavlink/link_manager.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <random>
#include <string>
#include <utility>

#include "ara/com/com_error_domain.h"

namespace ara {
namespace com {
namespace mavlink {
namespace {
using ara::com::ComErrc;
using Clock = std::chrono::steady_clock;

std::uint16_t RandomSeqStart() {
  static std::mutex m;
  static std::random_device rd;
  static std::mt19937 rng(rd());
  std::lock_guard<std::mutex> lock{m};
  return static_cast<std::uint16_t>(rng() & 0xFFFF);
}

ara::core::ErrorCode Err(ComErrc c, const std::string& msg) {
  return ara::com::MakeErrorCode(c, msg);
}

std::vector<std::uint8_t> Padded(const std::vector<std::uint8_t>& v, std::size_t n) {
  std::vector<std::uint8_t> out = v;
  if (out.size() < n) {
    out.resize(n, 0);
  }
  return out;
}

std::uint32_t PendingKey(std::uint16_t element_id, std::uint16_t seq) {
  return (static_cast<std::uint32_t>(element_id) << 16) | seq;
}
}  // namespace

// =============================================================== MavlinkEndpoint

MavlinkEndpoint::MavlinkEndpoint(LinkManager* manager, ara::core::model::ModelMavlink model,
                                 bool skeleton)
    : manager_{manager},
      model_{std::move(model)},
      skeleton_{skeleton},
      exclude_{model_.options_.exclude.begin(), model_.options_.exclude.end()} {
  for (const auto& def : model_.options_.elements) {
    ElementState st;
    st.def = def;
    if (def.enabled) {
      st.req = std::make_shared<MessageCodec>(def.req);
      if (def.has_resp) {
        st.resp = std::make_shared<MessageCodec>(def.resp);
      }
    }
    st.available = def.enabled && exclude_.count(def.name) == 0;
    if (def.is_event) {
      double rate = def.max_rate_hz;
      const auto it = model_.options_.max_rate_hz.find(def.name);
      if (it != model_.options_.max_rate_hz.end()) {
        rate = it->second;  // nadpisanie instancji; 0 = bez limitu
      }
      if (rate > 0) {
        // sufit z 1000/rate bez std::ceil: toolchain ARM (bbb) nie dokleja libm, a każda aplikacja
        // linkująca link_manager (RadioApp/EM) dostałaby undefined reference 'ceil'
        const double ms = 1000.0 / rate;
        auto whole = static_cast<long long>(ms);  // NOLINT
        if (static_cast<double>(whole) < ms) {
          ++whole;
        }
        st.min_interval = std::chrono::milliseconds(whole);
      }
    }
    st.next_seq = (manager_ != nullptr && manager_->test_initial_seq().has_value())
                      ? *manager_->test_initial_seq()
                      : RandomSeqStart();  // losowy start per (instancja, element) (R2)
    elements_.push_back(std::move(st));
  }
}

MavlinkEndpoint::~MavlinkEndpoint() {
  worker_.request_stop();
  job_cv_.notify_all();
}

void MavlinkEndpoint::SetMethodHandler(MethodHandler h) {
  std::lock_guard<std::mutex> lock{handlers_mutex_};
  method_handler_ = std::move(h);
}

void MavlinkEndpoint::SetEventHandler(EventHandler h) {
  std::lock_guard<std::mutex> lock{handlers_mutex_};
  event_handler_ = std::move(h);
}

MavlinkEndpoint::ElementState* MavlinkEndpoint::FindElement(std::uint16_t id) {
  for (auto& e : elements_) {
    if (e.def.id == id) return &e;
  }
  return nullptr;
}

const MavlinkEndpoint::ElementState* MavlinkEndpoint::FindElement(std::uint16_t id) const {
  for (const auto& e : elements_) {
    if (e.def.id == id) return &e;
  }
  return nullptr;
}

std::uint16_t MavlinkEndpoint::NextSeq(ElementState* el) {
  std::lock_guard<std::mutex> lock{seq_mutex_};
  return el->next_seq++;
}

bool MavlinkEndpoint::IsEventAvailable(std::uint16_t event_id) const {
  const auto* el = FindElement(event_id);
  return el != nullptr && el->def.is_event && el->available;
}

bool MavlinkEndpoint::AnyLinkReady() const {
  return std::any_of(links_.begin(), links_.end(),
                     [](const std::shared_ptr<MavlinkLink>& l) { return l->IsReady(); });
}

EndpointCounterSnapshot MavlinkEndpoint::counters() const {
  EndpointCounterSnapshot s;
  s.tx_events = counters_.tx_events.load();
  s.tx_excluded = counters_.tx_excluded.load();
  s.tx_requests = counters_.tx_requests.load();
  s.tx_responses = counters_.tx_responses.load();
  s.rx_events = counters_.rx_events.load();
  s.rx_requests = counters_.rx_requests.load();
  s.rx_responses = counters_.rx_responses.load();
  s.rx_rejected = counters_.rx_rejected.load();
  s.rx_duplicate = counters_.rx_duplicate.load();
  s.call_timeouts = counters_.call_timeouts.load();
  s.call_unavailable = counters_.call_unavailable.load();
  return s;
}

ara::core::Result<void> MavlinkEndpoint::PublishEvent(std::uint16_t event_id,
                                                      const std::vector<std::uint8_t>& payload) {
  auto* el = FindElement(event_id);
  if (el == nullptr || !el->def.is_event) {
    return Err(ComErrc::kServiceNotAvailable,
               "event " + std::to_string(event_id) + " nie należy do usługi");
  }
  if (!el->available) {
    counters_.tx_excluded++;
    if (!logged_excluded_) {
      logged_excluded_ = true;
      manager_->Log(LogLevel::kDebug, "event '" + el->def.name +
                                          "' wyłączony z MAVLinka — Update jest no-op");
    }
    return {};
  }
  MessageHeader h;
  h.seq = NextSeq(el);
  h.instance = model_.instance_;
  const auto enc = el->req->Encode(h, payload);
  if (!enc.HasValue()) {
    return enc.Error();
  }
  OutMessage m;
  m.msg_id = el->req->msg_id();
  m.crc_extra = el->req->crc_extra();
  m.payload = enc.Value();
  const std::uint64_t key = (static_cast<std::uint64_t>(model_.instance_) << 16) | event_id;
  for (const auto& link : links_) {
    link->EnqueueEvent(key, m, el->min_interval);
  }
  counters_.tx_events++;
  return {};
}

ara::core::Result<std::vector<std::uint8_t>> MavlinkEndpoint::CallMethod(
    std::uint16_t method_id, const std::vector<std::uint8_t>& payload) {
  auto* el = FindElement(method_id);
  if (el == nullptr || el->def.is_event) {
    return Err(ComErrc::kServiceNotAvailable,
               "metoda " + std::to_string(method_id) + " nie należy do usługi");
  }
  if (!el->available) {
    counters_.call_unavailable++;
    manager_->Log(LogLevel::kWarn, "metoda '" + el->def.name +
                                       "' niedostępna po mavlink (wyłączona) — bez nadawania");
    return Err(ComErrc::kServiceNotAvailable, "niedostępne po mavlink");
  }
  if (!AnyLinkReady()) {
    counters_.call_unavailable++;
    return Err(ComErrc::kCommunicationLinkError, "żadne łącze instancji nie jest gotowe");
  }
  MessageHeader h;
  h.seq = NextSeq(el);
  h.instance = model_.instance_;
  h.target_system = model_.target_system_id_;
  h.target_component = model_.target_component_id_;
  const auto enc = el->req->Encode(h, payload);
  if (!enc.HasValue()) {
    return enc.Error();
  }
  OutMessage m;
  m.msg_id = el->req->msg_id();
  m.crc_extra = el->req->crc_extra();
  m.payload = enc.Value();

  const bool has_resp = el->def.has_resp;
  std::shared_ptr<Pending> pend;
  const auto key = PendingKey(method_id, h.seq);
  if (has_resp) {
    pend = std::make_shared<Pending>();
    std::lock_guard<std::mutex> lock{pending_mutex_};
    pending_[key] = pend;
  }
  bool any_ok = false;
  std::optional<ara::core::ErrorCode> last_error;
  for (const auto& link : links_) {
    const auto r = link->EnqueueRequest(m);
    if (r.HasValue()) {
      any_ok = true;
    } else {
      last_error.emplace(r.Error());
    }
  }
  if (!any_ok) {
    if (has_resp) {
      std::lock_guard<std::mutex> lock{pending_mutex_};
      pending_.erase(key);
    }
    counters_.call_unavailable++;
    return *last_error;
  }
  counters_.tx_requests++;
  if (!has_resp) {
    return std::vector<std::uint8_t>{};  // fire-and-forget
  }
  bool done = false;
  {
    std::unique_lock<std::mutex> lock{pend->mutex};
    done = pend->cv.wait_for(lock, std::chrono::milliseconds(el->def.timeout_ms),
                             [&] { return pend->done; });
  }
  {
    std::lock_guard<std::mutex> lock{pending_mutex_};
    pending_.erase(key);
  }
  if (!done) {
    counters_.call_timeouts++;
    return Err(ComErrc::kPeerIsUnreachable,
               "timeout metody '" + el->def.name + "' (" +
                   std::to_string(el->def.timeout_ms) + " ms)");
  }
  if (pend->status == 2) {
    return Err(ComErrc::kServiceNotAvailable, "zdalnie: metoda nieznana/wykluczona");
  }
  if (pend->status != 0) {
    return Err(ComErrc::kCommunicationStackError, "zdalnie: błąd metody");
  }
  return pend->payload;
}

void MavlinkEndpoint::CompletePending(const ElementState& el, const MessageHeader& h,
                                      const std::vector<std::uint8_t>& someip) {
  std::shared_ptr<Pending> pend;
  {
    std::lock_guard<std::mutex> lock{pending_mutex_};
    const auto it = pending_.find(PendingKey(el.def.id, h.seq));
    if (it == pending_.end()) {
      return;  // spóźniona/powtórzona RESP
    }
    pend = it->second;
  }
  counters_.rx_responses++;
  {
    std::lock_guard<std::mutex> lock{pend->mutex};
    if (pend->done) {
      return;  // pierwsza RESP wygrywa
    }
    pend->done = true;
    pend->status = h.status;
    pend->payload = someip;
  }
  pend->cv.notify_all();
}

void MavlinkEndpoint::DeliverEvent(const ElementState& el, const std::vector<std::uint8_t>& someip) {
  EventHandler h;
  {
    std::lock_guard<std::mutex> lock{handlers_mutex_};
    h = event_handler_;
  }
  std::lock_guard<std::mutex> lock{deliver_mutex_};  // jeden event naraz na endpoint
  if (detached_) {
    return;
  }
  counters_.rx_events++;
  if (h) {
    h(el.def.id, someip);
  }
}

void MavlinkEndpoint::EnqueueJob(Job job) {
  {
    std::lock_guard<std::mutex> lock{job_mutex_};
    jobs_.push_back(std::move(job));
  }
  job_cv_.notify_all();
}

void MavlinkEndpoint::WorkerLoop(std::stop_token st) {
  for (;;) {
    Job job;
    {
      std::unique_lock<std::mutex> lock{job_mutex_};
      job_cv_.wait(lock, st, [&] { return !jobs_.empty(); });
      if (st.stop_requested()) {
        return;
      }
      job = std::move(jobs_.front());
      jobs_.pop_front();
    }
    const auto& el = elements_[job.element];
    MethodHandler handler;
    {
      std::lock_guard<std::mutex> lock{handlers_mutex_};
      handler = method_handler_;
    }
    std::uint8_t status = 0;
    std::vector<std::uint8_t> out;
    if (!handler) {
      status = 1;
    } else {
      try {
        const auto r = handler(el.def.id, job.payload);
        if (r.HasValue()) {
          out = r.Value();
        } else {
          status = 1;
        }
      } catch (...) {
        status = 1;
      }
    }
    counters_.rx_requests++;
    if (el.def.has_resp) {
      SendResponse(el, job.req_header, job.sender_sys, job.sender_comp, status, out);
    }
  }
}

void MavlinkEndpoint::SendResponse(const ElementState& el, const MessageHeader& req_header,
                                   std::uint8_t sender_sys, std::uint8_t sender_comp,
                                   std::uint8_t status, const std::vector<std::uint8_t>& someip) {
  if (!el.resp) {
    return;
  }
  MessageHeader h;
  h.seq = req_header.seq;  // RESP: seq = seq REQ
  h.instance = req_header.instance;
  h.target_system = sender_sys;  // target = nadawca REQ
  h.target_component = sender_comp;
  h.status = status;
  const auto enc = el.resp->Encode(h, Padded(status == 0 ? someip : std::vector<std::uint8_t>{},
                                             el.resp->someip_len()));
  if (!enc.HasValue()) {
    manager_->Log(LogLevel::kWarn, "RESP '" + el.def.name + "': " + enc.Error().SupportData());
    return;
  }
  OutMessage m;
  m.msg_id = el.resp->msg_id();
  m.crc_extra = el.resp->crc_extra();
  m.payload = enc.Value();
  for (const auto& link : links_) {
    link->EnqueueResponse(m);
  }
  counters_.tx_responses++;
}

// =============================================================== LinkManager

LinkManager::LinkManager() : LinkManager(Options{}) {}

LinkManager::LinkManager(Options opts)
    : opts_{std::move(opts)},
      registry_{opts_.registry != nullptr ? opts_.registry : &GlobalLinkTransportRegistry()} {}

LinkManager::~LinkManager() { Shutdown(); }

void LinkManager::Log(LogLevel l, const std::string& m) const {
  if (opts_.log) {
    opts_.log(l, "[mavlink] " + m);
  }
}

std::optional<std::uint8_t> LinkManager::CrcLookup(std::uint32_t msgid) const {
  std::lock_guard<std::mutex> lock{mutex_};
  const auto it = messages_.find(msgid);
  if (it == messages_.end()) {
    return std::nullopt;
  }
  return it->second.codec->crc_extra();
}

MavlinkLink::TransportProvider LinkManager::MakeProvider(
    const ara::core::model::ModelMavlinkLink& l) {
  if (opts_.transport_factory) {
    return opts_.transport_factory(l);
  }
  auto* reg = registry_;
  const std::string name = l.name;
  return [reg, name](std::chrono::milliseconds wait, std::stop_token st) {
    return reg->WaitFor(name, wait, st);
  };
}

std::shared_ptr<MavlinkLink> LinkManager::AcquireLink(
    const ara::core::model::ModelMavlinkLink& cfg, std::uint8_t sys, std::uint8_t comp) {
  std::shared_ptr<MavlinkLink> link;
  bool created = false;
  {
    std::lock_guard<std::mutex> lock{mutex_};
    auto it = links_.find(cfg.name);
    if (it != links_.end()) {
      it->second.refs++;
      return it->second.link;
    }
    LinkConfig lc;
    lc.name = cfg.name;
    lc.version = cfg.version;
    lc.heartbeat_hz = cfg.heartbeat_hz;
    lc.signing_enabled = cfg.signing_enabled;
    lc.key_file = cfg.signing_key_file;
    lc.signing_link_id = static_cast<std::uint8_t>(cfg.signing_link_id);
    lc.accept_unsigned = cfg.accept_unsigned;
    lc.state_file = cfg.signing_state_file;
    lc.system_id = sys;
    lc.component_id = comp;
    lc.retry_interval = opts_.retry_interval;
    lc.transport_warn_after = opts_.transport_warn_after;
    lc.read_timeout_ms = opts_.read_timeout_ms;
    lc.write_timeout_ms = opts_.write_timeout_ms;
    link = std::make_shared<MavlinkLink>(
        lc, MakeProvider(cfg),
        [this](std::uint32_t id) { return CrcLookup(id); },
        [this](const std::string& name, const Frame& f) { OnFrame(name, f); }, opts_.log,
        opts_.now);
    links_[cfg.name] = LinkEntry{link, 1};
    created = true;
  }
  if (created) {
    link->Start();
  }
  return link;
}

void LinkManager::ReleaseLink(const std::string& name) {
  std::shared_ptr<MavlinkLink> to_stop;
  {
    std::lock_guard<std::mutex> lock{mutex_};
    auto it = links_.find(name);
    if (it == links_.end()) {
      return;
    }
    if (--it->second.refs <= 0) {
      to_stop = std::move(it->second.link);
      links_.erase(it);
    }
  }
  if (to_stop) {
    to_stop->Stop();
  }
}

void LinkManager::RegisterMessages(const MavlinkEndpoint& ep) {
  std::lock_guard<std::mutex> lock{mutex_};
  for (const auto& el : ep.elements_) {
    if (!el.def.enabled) {
      continue;
    }
    auto add = [&](const std::shared_ptr<MessageCodec>& codec, MsgInfo::Kind kind) {
      if (!codec) return;
      const auto it = messages_.find(codec->msg_id());
      if (it != messages_.end()) {
        if (it->second.codec->name() != codec->name() ||
            it->second.codec->crc_extra() != codec->crc_extra()) {
          Log(LogLevel::kError, "konflikt definicji msgid " + std::to_string(codec->msg_id()) +
                                    ": " + it->second.codec->name() + " vs " + codec->name());
        }
        return;
      }
      MsgInfo info;
      info.codec = codec;
      info.kind = kind;
      info.element_id = el.def.id;
      messages_[codec->msg_id()] = std::move(info);
    };
    add(el.req, el.def.is_event ? MsgInfo::kEvent : MsgInfo::kReq);
    add(el.resp, MsgInfo::kResp);
  }
}

std::shared_ptr<MavlinkEndpoint> LinkManager::AttachEndpoint(
    const ara::core::model::ModelMavlink& model, bool skeleton) {
  std::shared_ptr<MavlinkEndpoint> ep{new MavlinkEndpoint(this, model, skeleton)};
  RegisterMessages(*ep);  // tablica wiadomości przed startem łączy (lookup CRC)
  for (const auto& l : model.links_) {
    ep->links_.push_back(AcquireLink(l, model.system_id_, model.component_id_));
  }
  {
    std::lock_guard<std::mutex> lock{mutex_};
    endpoints_.push_back(ep);
  }
  if (skeleton) {
    ep->worker_ = std::jthread([raw = ep.get()](std::stop_token st) { raw->WorkerLoop(st); });
  }
  return ep;
}

void LinkManager::DetachEndpoint(const std::shared_ptr<MavlinkEndpoint>& ep) {
  if (!ep) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock{mutex_};
    endpoints_.erase(std::remove(endpoints_.begin(), endpoints_.end(), ep), endpoints_.end());
  }
  {
    // Czeka na handler eventu w toku; od teraz DeliverEvent nic nie wywoła (use-after-free
    // obiektu usera po Detach).
    std::lock_guard<std::mutex> lock{ep->deliver_mutex_};
    ep->detached_ = true;
  }
  ep->worker_.request_stop();
  ep->job_cv_.notify_all();
  if (ep->worker_.joinable()) {
    ep->worker_.join();
  }
  // links_ jest niezmienne po Attach (wątki RX mogą po nim iterować w OnFrame/SendResponse);
  // sprzątamy tylko referencje menedżera, shared_ptr utrzymuje łącza przy życiu.
  for (const auto& l : ep->links_) {
    ReleaseLink(l->config().name);
  }
}

void LinkManager::Shutdown() {
  std::vector<std::shared_ptr<MavlinkEndpoint>> eps;
  {
    std::lock_guard<std::mutex> lock{mutex_};
    eps = endpoints_;
  }
  for (const auto& ep : eps) {
    DetachEndpoint(ep);
  }
  std::map<std::string, LinkEntry> rest;
  {
    std::lock_guard<std::mutex> lock{mutex_};
    rest.swap(links_);
  }
  for (auto& kv : rest) {
    kv.second.link->Stop();
  }
}

std::shared_ptr<MavlinkLink> LinkManager::FindLink(const std::string& name) const {
  std::lock_guard<std::mutex> lock{mutex_};
  const auto it = links_.find(name);
  return it == links_.end() ? nullptr : it->second.link;
}

void LinkManager::OnFrame(const std::string& link_name, const Frame& f) {
  MsgInfo info;
  std::vector<std::shared_ptr<MavlinkEndpoint>> eps;
  {
    std::lock_guard<std::mutex> lock{mutex_};
    const auto it = messages_.find(f.msgid);
    if (it == messages_.end()) {
      unknown_++;
      return;
    }
    info = it->second;
    eps = endpoints_;
  }
  const auto dec = info.codec->Decode(f.payload);
  if (!dec.HasValue()) {
    unknown_++;
    return;
  }
  const MessageHeader& h = dec.Value().header;
  const bool want_skeleton = info.kind == MsgInfo::kReq;

  // Endpointy właściwej roli, które znają tę wiadomość.
  std::vector<std::shared_ptr<MavlinkEndpoint>> candidates;
  for (const auto& ep : eps) {
    if (ep->is_skeleton() != want_skeleton) continue;
    const auto* el = ep->FindElement(info.element_id);
    if (el != nullptr && el->def.enabled) candidates.push_back(ep);
  }
  if (candidates.empty()) {
    return;  // nie jesteśmy odbiorcą tej usługi
  }
  std::shared_ptr<MavlinkEndpoint> target;
  for (const auto& ep : candidates) {
    if (ep->instance() == h.instance) {
      target = ep;
      break;
    }
  }
  if (!target) {
    // Znana wiadomość, nieznana instancja: REQ z resp_id -> RESP status=2 (rev. 4/7).
    const auto& ep = candidates.front();
    ep->counters_.rx_rejected++;
    if (info.kind == MsgInfo::kReq) {
      const auto* el = ep->FindElement(info.element_id);
      if (el->def.has_resp &&
          (h.target_system == 0 || h.target_system == ep->model_.system_id_) &&
          (h.target_component == 0 || h.target_component == ep->model_.component_id_)) {
        // RESP idzie łączem, którym przyszło REQ (instancja nie istnieje).
        const auto link = FindLink(link_name);
        if (link && el->resp) {
          MessageHeader out;
          out.seq = h.seq;
          out.instance = h.instance;
          out.target_system = f.sysid;
          out.target_component = f.compid;
          out.status = 2;
          const auto enc = el->resp->Encode(out, std::vector<std::uint8_t>(el->resp->someip_len(), 0));
          if (enc.HasValue()) {
            OutMessage m;
            m.msg_id = el->resp->msg_id();
            m.crc_extra = el->resp->crc_extra();
            m.payload = enc.Value();
            link->EnqueueResponse(std::move(m));
          }
        }
      }
    }
    return;
  }
  auto& ep = target;
  const auto& model = ep->model_;
  // Ramka przyjmowana tylko z łącza należącego do instancji: podpis/ochrona łącza
  // jednej instancji nie może być obejściem przez łącze innej (QA M2a-2 #1).
  const bool on_instance_link = std::any_of(
      ep->links_.begin(), ep->links_.end(),
      [&link_name](const std::shared_ptr<MavlinkLink>& l) { return l->config().name == link_name; });
  if (!on_instance_link) {
    ep->counters_.rx_rejected++;
    return;
  }
  // Filtry targetu / źródła.
  if (info.kind == MsgInfo::kReq) {
    if ((h.target_system != 0 && h.target_system != model.system_id_) ||
        (h.target_component != 0 && h.target_component != model.component_id_)) {
      return;  // nie do nas
    }
  } else if (info.kind == MsgInfo::kResp) {
    if (h.target_system != model.system_id_ || h.target_component != model.component_id_) {
      return;
    }
  } else {
    const auto& o = model.options_;
    if ((o.source_system_id != 0 && f.sysid != o.source_system_id) ||
        (o.source_component_id != 0 && f.compid != o.source_component_id)) {
      return;
    }
  }
  // Dedup: seq + CRC32 payloadu w oknie TTL.
  const DedupTable::Key key{f.sysid, f.compid, f.msgid, h.instance,
                            info.kind == MsgInfo::kResp ? h.target_system : std::uint8_t{0},
                            info.kind == MsgInfo::kResp ? h.target_component : std::uint8_t{0}};
  if (dedup_.TestAndInsert(key, h.seq, Crc32(f.payload),
                           std::chrono::milliseconds(model.options_.dedup_ttl_ms), Clock::now())) {
    duplicates_++;
    ep->counters_.rx_duplicate++;
    return;
  }
  const auto* el = ep->FindElement(info.element_id);
  if (!el->available) {
    ep->counters_.rx_rejected++;  // element wyłączony w tej instancji
    if (info.kind == MsgInfo::kReq && el->def.has_resp) {
      ep->SendResponse(*el, h, f.sysid, f.compid, 2, {});
    }
    return;
  }
  switch (info.kind) {
    case MsgInfo::kEvent:
      ep->DeliverEvent(*el, dec.Value().someip);
      break;
    case MsgInfo::kReq: {
      MavlinkEndpoint::Job job;
      for (std::size_t i = 0; i < ep->elements_.size(); ++i) {
        if (&ep->elements_[i] == el) job.element = i;
      }
      job.req_header = h;
      job.sender_sys = f.sysid;
      job.sender_comp = f.compid;
      job.payload = dec.Value().someip;
      job.arrival_link = link_name;
      ep->EnqueueJob(std::move(job));
      break;
    }
    case MsgInfo::kResp:
      ep->CompletePending(*el, h, dec.Value().someip);
      break;
  }
}

}  // namespace mavlink
}  // namespace com
}  // namespace ara
