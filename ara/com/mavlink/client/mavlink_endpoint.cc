/**
 * Copyright (c) 2024
 *
 * @file mavlink_endpoint.cc
 */
#include "ara/com/mavlink/client/mavlink_endpoint.h"

#include <algorithm>
#include <exception>
#include <random>
#include <string>
#include <utility>

#include "ara/com/com_error_domain.h"
#include "ara/com/mavlink/mavlink_frame.h"
#include "ara/com/mavlink/client/daemon_client.h"

namespace ara {
namespace com {
namespace mavlink {
namespace {
using ara::com::ComErrc;
using Role = ara::core::model::ModelMavlinkField::Role;

/// At most this many messages wait for the instance handler; the excess is dropped (counted).
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

// Smallest interval in ms for a rate limit in Hz; a ceiling without std::ceil: the ARM (bbb)
// toolchain does not link libm, and every application linking this code would get an undefined
// reference to 'ceil'.
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
                                 ara::core::model::ModelMavlink model, bool skeleton, Handlers handlers)
    : client_{client},
      handle_{handle},
      model_{std::move(model)},
      skeleton_{skeleton},
      elements_(model_.options_.elements.size()),
      handlers_{std::move(handlers)} {
  for (std::size_t i = 0; i < elements_.size(); ++i) {
    const auto& def = model_.options_.elements[i];
    ElementState& st = elements_[i];
    st.def = def;
    if (def.enabled) {
      st.req = std::make_shared<MessageCodec>(def.req);
      if (def.has_resp) {
        st.resp = std::make_shared<MessageCodec>(def.resp);
      }
    }
    st.available = def.enabled && !def.links.empty();
    // Random start per (instance, element): after a sender restart the sequence does not repeat
    // from 0, so a receiver that tells copies apart by `seq` does not take new messages for copies of old ones.
    st.next_seq = RandomSeqStart();
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

bool MavlinkEndpoint::ElementLinkReady(std::uint16_t element_id) const {
  const auto* el = FindElement(element_id);
  if (el == nullptr || !registered_.load()) {
    return false;
  }
  std::lock_guard<std::mutex> lock{state_mutex_};
  return std::any_of(el->def.links.begin(), el->def.links.end(),
                     [this](const auto& l) { return ready_links_.count(l.link) != 0; });
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
  r.links = model_.links_;
  for (const auto& el : elements_) {
    if (!el.available) {
      continue;  // on no radio: the daemon knows no such element
    }
    daemon::ElementDesc d;
    d.id = el.def.id;
    d.is_event = el.def.is_event;
    d.has_resp = el.def.has_resp;
    for (const auto& l : el.def.links) {
      d.links.push_back({l.link, el.def.is_event ? MinIntervalMs(l.max_rate_hz) : 0U});
    }
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

// ---------------------------------------------------------------- state from the daemon

void MavlinkEndpoint::OnRegistered(const daemon::RegisterReply& reply) {
  for (const auto& t : reply.method_timeouts) {  // before registered_: a call sees the new value
    if (auto* el = FindElement(t.element_id); el != nullptr) {
      el->method_timeout_ms = t.timeout_ms;
    }
  }
  registered_ = true;
}

void MavlinkEndpoint::OnDisconnected() {
  registered_ = false;
  {
    std::lock_guard<std::mutex> lock{state_mutex_};
    ready_links_.clear();
  }
  FailAllPending(Err(ComErrc::kCommunicationLinkError, "connection to the MavlinkDaemon lost"));
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
  std::string text = "the daemon rejected the REQ";
  switch (e.code) {
    case daemon::SendErrc::kUnknownElement:
      text = "daemon: unknown element / payload mismatch";
      break;
    case daemon::SendErrc::kUnavailable:
      text = "daemon: element not registered";
      break;
    case daemon::SendErrc::kNoLink:
      code = ComErrc::kCommunicationLinkError;
      text = "daemon: no link of the instance is ready";
      break;
    case daemon::SendErrc::kQueueFull:
      code = ComErrc::kCommunicationStackError;
      text = "daemon: REQ/RESP queue full";
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
      return;  // late / repeated RESP
    }
    pend = it->second;
  }
  counters_.rx_responses++;
  {
    std::lock_guard<std::mutex> lock{pend->mutex};
    if (pend->done) {
      return;  // the first RESP wins
    }
    pend->done = true;
    pend->status = h.status;
    pend->payload = dec.Value().someip;
  }
  pend->cv.notify_all();
}

void MavlinkEndpoint::OnDelivery(daemon::Delivery d) {
  const bool request = d.kind == daemon::ElementKind::kRequest;
  const auto* el = FindElement(d.element_id);
  // a proxy gets events and a skeleton gets REQ; anything else is a daemon bug or a model mismatch
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

// ---------------------------------------------------------------- instance thread

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
  counters_.rx_events++;
  if (handlers_.event) {
    handlers_.event(el.def.id, dec.Value().someip);
  }
}

void MavlinkEndpoint::HandleRequest(const ElementState& el, const daemon::Delivery& d) {
  const auto dec = el.req->Decode(d.payload);
  if (!dec.HasValue()) {
    counters_.rx_dropped++;
    return;
  }
  std::uint8_t status = 0;
  std::vector<std::uint8_t> out;
  if (!handlers_.method) {
    status = 1;
  } else {
    // An exception from the application handler is a bug of the application. It is not turned into
    // a "remote method error" (status 1), which would hide it from the application's author:
    // it is logged as an Error with its text and propagates (the handler thread ends the process,
    // like an uncaught exception in any other application thread).
    try {
      const auto r = handlers_.method(el.def.id, dec.Value().someip);
      if (r.HasValue()) {
        out = r.Value();
      } else {
        status = 1;
      }
    } catch (const std::exception& e) {
      client_->Log(LogLevel::kError, "method '" + el.def.name + "' of '" + name() + "' threw: " + e.what());
      throw;
    } catch (...) {
      client_->Log(LogLevel::kError, "method '" + el.def.name + "' of '" + name() + "' threw an unknown exception");
      throw;
    }
  }
  counters_.rx_requests++;
  if (!el.def.has_resp || !el.resp) {
    return;
  }
  const MessageHeader& req = dec.Value().header;
  MessageHeader h;
  h.seq = req.seq;  // RESP: seq = the REQ seq
  h.instance = req.instance;
  h.target_system = d.sender_system;  // target = the REQ sender
  h.target_component = d.sender_component;
  h.status = status;
  const auto enc = el.resp->Encode(
      h, ExpandPayload(status == 0 ? out : std::vector<std::uint8_t>{}, el.resp->someip_len()));
  if (!enc.HasValue()) {
    return;  // RESP encoding error: the sender's REQ ends with a timeout
  }
  daemon::ElementPayload resp;
  resp.kind = daemon::ElementKind::kResponse;
  resp.handle = handle_;
  resp.element_id = el.def.id;
  resp.payload = enc.Value();
  if (registered_.load() && client_->Send(resp).HasValue()) {
    counters_.tx_responses++;
  } else {
    counters_.tx_dropped++;
  }
}

// ---------------------------------------------------------------- application API

ara::core::Result<void> MavlinkEndpoint::PublishEvent(std::uint16_t event_id,
                                                      const std::vector<std::uint8_t>& payload) {
  auto* el = FindElement(event_id);
  if (el == nullptr || !el->def.is_event) {
    return Err(ComErrc::kServiceNotAvailable,
               "event " + std::to_string(event_id) + " does not belong to the service");
  }
  if (!el->available) {
    counters_.tx_excluded++;
    return {};  // excluded from MAVLink: Update is a no-op
  }
  MessageHeader h;
  h.seq = NextSeq(el);
  h.instance = model_.instance_;
  const auto enc = el->req->Encode(h, payload);
  if (!enc.HasValue()) {
    return enc.Error();
  }
  if (!registered_.load()) {
    counters_.tx_dropped++;  // daemon unavailable: the event is lost (Update is not an error)
    return {};
  }
  daemon::ElementPayload ev;
  ev.kind = daemon::ElementKind::kEvent;
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
               "method " + std::to_string(method_id) + " does not belong to the service");
  }
  if (!el->available) {
    counters_.call_unavailable++;
    return Err(ComErrc::kServiceNotAvailable, "method is on no radio (not in elements)");
  }
  if (!registered_.load()) {
    counters_.call_unavailable++;
    return Err(ComErrc::kCommunicationLinkError, "not connected to the MavlinkDaemon");
  }
  if (!ElementLinkReady(method_id)) {
    counters_.call_unavailable++;
    return Err(ComErrc::kCommunicationLinkError, "no link of the method is ready");
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
  daemon::ElementPayload req;
  req.kind = daemon::ElementKind::kRequest;
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
    return Err(ComErrc::kCommunicationLinkError, "sending the REQ to the MavlinkDaemon failed");
  }
  counters_.tx_requests++;
  if (!has_resp) {
    return std::vector<std::uint8_t>{};  // fire-and-forget
  }
  const std::uint32_t timeout_ms = el->method_timeout_ms.load();
  bool done = false;
  {
    std::unique_lock<std::mutex> lock{pend->mutex};
    done = pend->cv.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                             [&] { return pend->done; });
  }
  {
    std::lock_guard<std::mutex> lock{pending_mutex_};
    pending_.erase(key);
  }
  if (!done) {
    counters_.call_timeouts++;
    return Err(ComErrc::kPeerIsUnreachable,
               "timeout of method '" + el->def.name + "' (" + std::to_string(timeout_ms) +
                   " ms)");
  }
  if (pend->error.has_value()) {
    return *pend->error;
  }
  if (pend->status == 2) {
    return Err(ComErrc::kServiceNotAvailable, "remote: method unknown / not served");
  }
  if (pend->status != 0) {
    return Err(ComErrc::kCommunicationStackError, "remote: method error");
  }
  return pend->payload;
}

}  // namespace mavlink
}  // namespace com
}  // namespace ara
