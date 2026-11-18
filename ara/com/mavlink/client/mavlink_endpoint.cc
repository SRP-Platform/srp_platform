/**
 * Copyright (c) 2024
 *
 * @file mavlink_endpoint.cc
 */
#include "ara/com/mavlink/client/mavlink_endpoint.h"

#include <algorithm>
#include <random>
#include <string>
#include <utility>

#include "ara/com/com_error_domain.h"
#include "ara/com/mavlink/client/daemon_client.h"

namespace ara {
namespace com {
namespace mavlink {
namespace {
using ara::com::ComErrc;
using Role = ara::core::model::ModelMavlinkField::Role;

/// Najwyżej tyle komunikatów czeka na handler instancji; nadmiar jest odrzucany (licznik).
constexpr std::size_t kMaxQueuedJobs = 256;

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

std::uint8_t Offset(const MessageCodec& c, Role role) {
  const auto off = c.HeaderOffset(role);
  return off.has_value() ? static_cast<std::uint8_t>(*off) : daemon::kNoOffset;
}

daemon::MessageDesc Describe(const MessageCodec& c) {
  daemon::MessageDesc d;
  d.msg_id = c.msg_id();
  d.crc_extra = c.crc_extra();
  d.full_len = static_cast<std::uint8_t>(c.full_payload_len());
  d.seq_offset = Offset(c, Role::kSeq);
  d.instance_offset = Offset(c, Role::kInstance);
  d.target_system_offset = Offset(c, Role::kTargetSystem);
  d.target_component_offset = Offset(c, Role::kTargetComponent);
  d.status_offset = Offset(c, Role::kStatus);
  d.name = c.name();
  return d;
}

// Najmniejszy odstęp w ms dla limitu Hz; sufit bez std::ceil: toolchain ARM (bbb) nie dokleja
// libm, a każda aplikacja linkująca ten kod dostałaby undefined reference 'ceil'.
std::uint32_t MinIntervalMs(double rate_hz) {
  if (rate_hz <= 0) {
    return 0;
  }
  const double ms = 1000.0 / rate_hz;
  auto whole = static_cast<std::uint32_t>(ms);
  if (static_cast<double>(whole) < ms) {
    ++whole;
  }
  return whole;
}
}  // namespace

MavlinkEndpoint::MavlinkEndpoint(DaemonClient* client, std::uint32_t handle,
                                 ara::core::model::ModelMavlink model, bool skeleton)
    : client_{client},
      handle_{handle},
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
      st.min_interval_ms = MinIntervalMs(rate);
    }
    st.next_seq = (client_ != nullptr && client_->opts_.test_initial_seq.has_value())
                      ? *client_->opts_.test_initial_seq
                      : RandomSeqStart();  // losowy start per (instancja, element) (rev. 5 R2)
    elements_.push_back(std::move(st));
  }
}

MavlinkEndpoint::~MavlinkEndpoint() { StopWorker(); }

void MavlinkEndpoint::StopWorker() {
  worker_.request_stop();
  job_cv_.notify_all();
  if (worker_.joinable() && worker_.get_id() != std::this_thread::get_id()) {
    worker_.join();
  }
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
  if (!registered_.load()) {
    return false;
  }
  std::lock_guard<std::mutex> lock{state_mutex_};
  return !ready_links_.empty();
}

EndpointCounterSnapshot MavlinkEndpoint::counters() const {
  EndpointCounterSnapshot s;
  s.tx_events = counters_.tx_events.load();
  s.tx_excluded = counters_.tx_excluded.load();
  s.tx_dropped = counters_.tx_dropped.load();
  s.tx_requests = counters_.tx_requests.load();
  s.tx_responses = counters_.tx_responses.load();
  s.rx_events = counters_.rx_events.load();
  s.rx_requests = counters_.rx_requests.load();
  s.rx_responses = counters_.rx_responses.load();
  s.rx_dropped = counters_.rx_dropped.load();
  s.call_timeouts = counters_.call_timeouts.load();
  s.call_unavailable = counters_.call_unavailable.load();
  s.call_rejected = counters_.call_rejected.load();
  return s;
}

std::optional<daemon::RegisterInstance> MavlinkEndpoint::BuildRegistration() const {
  daemon::RegisterInstance r;
  r.role = skeleton_ ? daemon::Role::kProvide : daemon::Role::kRequire;
  r.com_id = model_.options_.service_name.empty() ? model_.com_id_ : model_.options_.service_name;
  r.instance = model_.instance_;
  r.target_system = model_.target_system_id_;
  r.target_component = model_.target_component_id_;
  r.source_system = model_.options_.source_system_id;
  r.source_component = model_.options_.source_component_id;
  r.dedup_ttl_ms = model_.options_.dedup_ttl_ms;
  r.links = model_.links_;
  for (const auto& el : elements_) {
    if (!el.def.enabled) {
      continue;  // poza MAVLinkiem: daemon nie zna takiego elementu
    }
    daemon::ElementDesc d;
    d.id = el.def.id;
    d.is_event = el.def.is_event;
    d.available = el.available;
    d.has_resp = el.def.has_resp;
    d.timeout_ms = el.def.timeout_ms;
    d.min_interval_ms = el.min_interval_ms;
    d.req = Describe(*el.req);
    if (el.resp) {
      d.resp = Describe(*el.resp);
    }
    r.elements.push_back(std::move(d));
  }
  if (r.elements.empty()) {
    return std::nullopt;
  }
  return r;
}

// ---------------------------------------------------------------- stan z daemona

void MavlinkEndpoint::OnRegistered() { registered_ = true; }

void MavlinkEndpoint::OnDisconnected() {
  registered_ = false;
  {
    std::lock_guard<std::mutex> lock{state_mutex_};
    ready_links_.clear();
  }
  FailAllPending(Err(ComErrc::kCommunicationLinkError, "połączenie z MavlinkDaemon zerwane"));
}

void MavlinkEndpoint::OnLinkStatus(const std::string& link, bool ready) {
  std::lock_guard<std::mutex> lock{state_mutex_};
  if (ready) {
    ready_links_.insert(link);
  } else {
    ready_links_.erase(link);
  }
}

void MavlinkEndpoint::FailAllPending(const ara::core::ErrorCode& e) {
  std::vector<std::shared_ptr<Pending>> all;
  {
    std::lock_guard<std::mutex> lock{pending_mutex_};
    for (const auto& kv : pending_) {
      all.push_back(kv.second);
    }
  }
  for (const auto& p : all) {
    {
      std::lock_guard<std::mutex> lock{p->mutex};
      if (p->done) {
        continue;
      }
      p->done = true;
      p->error.emplace(e);
    }
    p->cv.notify_all();
  }
}

void MavlinkEndpoint::OnSendError(const daemon::SendError& e) {
  counters_.call_rejected++;
  std::shared_ptr<Pending> pend;
  {
    std::lock_guard<std::mutex> lock{pending_mutex_};
    const auto it = pending_.find(PendingKey(e.element_id, e.seq));
    if (it == pending_.end()) {
      return;
    }
    pend = it->second;
  }
  ComErrc code = ComErrc::kServiceNotAvailable;
  std::string text = "daemon odrzucił REQ";
  switch (e.code) {
    case daemon::SendErrc::kUnknownElement:
      text = "daemon: nieznany element/niezgodny payload";
      break;
    case daemon::SendErrc::kUnavailable:
      text = "daemon: element wykluczony";
      break;
    case daemon::SendErrc::kNoLink:
      code = ComErrc::kCommunicationLinkError;
      text = "daemon: żadne łącze instancji nie jest gotowe";
      break;
    case daemon::SendErrc::kQueueFull:
      code = ComErrc::kCommunicationStackError;
      text = "daemon: kolejka REQ/RESP pełna";
      break;
  }
  {
    std::lock_guard<std::mutex> lock{pend->mutex};
    if (pend->done) {
      return;
    }
    pend->done = true;
    pend->error.emplace(Err(code, text));
  }
  pend->cv.notify_all();
}

void MavlinkEndpoint::OnResponse(daemon::Delivery d) {
  const auto* el = FindElement(d.element_id);
  if (el == nullptr || !el->resp || skeleton_) {
    counters_.rx_dropped++;
    return;
  }
  const auto dec = el->resp->Decode(d.payload);
  if (!dec.HasValue()) {
    counters_.rx_dropped++;
    return;
  }
  const MessageHeader& h = dec.Value().header;
  std::shared_ptr<Pending> pend;
  {
    std::lock_guard<std::mutex> lock{pending_mutex_};
    const auto it = pending_.find(PendingKey(el->def.id, h.seq));
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
    pend->payload = dec.Value().someip;
  }
  pend->cv.notify_all();
}

void MavlinkEndpoint::OnDelivery(bool request, daemon::Delivery d) {
  const auto* el = FindElement(d.element_id);
  // proxy dostaje eventy, skeleton REQ; reszta to błąd daemona albo niezgodny model
  if (el == nullptr || !el->req ||
      !(request ? (skeleton_ && !el->def.is_event) : (!skeleton_ && el->def.is_event))) {
    counters_.rx_dropped++;
    return;
  }
  Job job;
  job.request = request;
  job.element = static_cast<std::size_t>(el - elements_.data());
  job.in = std::move(d);
  {
    std::lock_guard<std::mutex> lock{job_mutex_};
    if (jobs_.size() >= kMaxQueuedJobs) {
      counters_.rx_dropped++;
      return;
    }
    jobs_.push_back(std::move(job));
  }
  job_cv_.notify_all();
}

// ---------------------------------------------------------------- wątek instancji

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
    if (job.request) {
      HandleRequest(el, job.in);
    } else {
      HandleEvent(el, job.in);
    }
  }
}

void MavlinkEndpoint::HandleEvent(const ElementState& el, const daemon::Delivery& d) {
  const auto dec = el.req->Decode(d.payload);
  if (!dec.HasValue()) {
    counters_.rx_dropped++;
    return;
  }
  EventHandler h;
  {
    std::lock_guard<std::mutex> lock{handlers_mutex_};
    h = event_handler_;
  }
  counters_.rx_events++;
  if (h) {
    h(el.def.id, dec.Value().someip);
  }
}

void MavlinkEndpoint::HandleRequest(const ElementState& el, const daemon::Delivery& d) {
  const auto dec = el.req->Decode(d.payload);
  if (!dec.HasValue()) {
    counters_.rx_dropped++;
    return;
  }
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
      const auto r = handler(el.def.id, dec.Value().someip);
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
  if (!el.def.has_resp || !el.resp) {
    return;
  }
  const MessageHeader& req = dec.Value().header;
  MessageHeader h;
  h.seq = req.seq;  // RESP: seq = seq REQ
  h.instance = req.instance;
  h.target_system = d.sender_system;  // target = nadawca REQ
  h.target_component = d.sender_component;
  h.status = status;
  const auto enc = el.resp->Encode(
      h, Padded(status == 0 ? out : std::vector<std::uint8_t>{}, el.resp->someip_len()));
  if (!enc.HasValue()) {
    return;  // błąd kodowania RESP: REQ nadawcy skończy się timeoutem
  }
  daemon::Response resp;
  resp.handle = handle_;
  resp.element_id = el.def.id;
  resp.payload = enc.Value();
  if (registered_.load() && client_->Send(resp).HasValue()) {
    counters_.tx_responses++;
  } else {
    counters_.tx_dropped++;
  }
}

// ---------------------------------------------------------------- API aplikacji

ara::core::Result<void> MavlinkEndpoint::PublishEvent(std::uint16_t event_id,
                                                      const std::vector<std::uint8_t>& payload) {
  auto* el = FindElement(event_id);
  if (el == nullptr || !el->def.is_event) {
    return Err(ComErrc::kServiceNotAvailable,
               "event " + std::to_string(event_id) + " nie należy do usługi");
  }
  if (!el->available) {
    counters_.tx_excluded++;
    return {};  // wyłączony z MAVLinka: Update jest no-op
  }
  MessageHeader h;
  h.seq = NextSeq(el);
  h.instance = model_.instance_;
  const auto enc = el->req->Encode(h, payload);
  if (!enc.HasValue()) {
    return enc.Error();
  }
  if (!registered_.load()) {
    counters_.tx_dropped++;  // daemon niedostępny: event ginie (Update nie jest błędem)
    return {};
  }
  daemon::Event ev;
  ev.handle = handle_;
  ev.element_id = event_id;
  ev.payload = enc.Value();
  if (!client_->Send(ev).HasValue()) {
    counters_.tx_dropped++;
    return {};
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
    return Err(ComErrc::kServiceNotAvailable, "niedostępne po mavlink");
  }
  if (!registered_.load()) {
    counters_.call_unavailable++;
    return Err(ComErrc::kCommunicationLinkError, "brak połączenia z MavlinkDaemon");
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
  const bool has_resp = el->def.has_resp;
  std::shared_ptr<Pending> pend;
  const auto key = PendingKey(method_id, h.seq);
  if (has_resp) {
    pend = std::make_shared<Pending>();
    std::lock_guard<std::mutex> lock{pending_mutex_};
    pending_[key] = pend;
  }
  daemon::Request req;
  req.handle = handle_;
  req.element_id = method_id;
  req.payload = enc.Value();
  const auto sent = client_->Send(req);
  if (!sent.HasValue()) {
    if (has_resp) {
      std::lock_guard<std::mutex> lock{pending_mutex_};
      pending_.erase(key);
    }
    counters_.call_unavailable++;
    return Err(ComErrc::kCommunicationLinkError, "wysłanie REQ do MavlinkDaemon nie powiodło się");
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
               "timeout metody '" + el->def.name + "' (" + std::to_string(el->def.timeout_ms) +
                   " ms)");
  }
  if (pend->error.has_value()) {
    return *pend->error;
  }
  if (pend->status == 2) {
    return Err(ComErrc::kServiceNotAvailable, "zdalnie: metoda nieznana/wykluczona");
  }
  if (pend->status != 0) {
    return Err(ComErrc::kCommunicationStackError, "zdalnie: błąd metody");
  }
  return pend->payload;
}

}  // namespace mavlink
}  // namespace com
}  // namespace ara
