/**
 * Copyright (c) 2024
 *
 * @file mavlink_link.cc
 */
#include "ara/com/mavlink/mavlink_link.h"

#include <algorithm>
#include <utility>

#include "ara/com/com_error_domain.h"
#include "ara/com/mavlink/transport_error.h"

namespace ara {
namespace com {
namespace mavlink {
namespace {
using Clock = std::chrono::steady_clock;

const std::vector<std::uint8_t>& HeartbeatPayload() {
  // custom_mode(4) | type=MAV_TYPE_ONBOARD_CONTROLLER(18) | autopilot=INVALID(8) |
  // base_mode | system_status=ACTIVE(4) | mavlink_version(3)
  static const std::vector<std::uint8_t> p{0, 0, 0, 0, 18, 8, 0, 4, 3};
  return p;
}
}  // namespace

MavlinkLink::MavlinkLink(LinkConfig cfg, TransportProvider provider,
                         FrameParser::CrcExtraLookup crc_lookup, FrameHandler handler,
                         LogFn log, LinkSigner::NowFn now)
    : cfg_{std::move(cfg)},
      provider_{std::move(provider)},
      crc_lookup_{std::move(crc_lookup)},
      handler_{std::move(handler)},
      log_{std::move(log)},
      now_{std::move(now)} {}

MavlinkLink::~MavlinkLink() { Stop(); }

void MavlinkLink::Log(LogLevel l, const std::string& m) const {
  if (log_) {
    log_(l, "[mavlink:" + cfg_.name + "] " + m);
  }
}

void MavlinkLink::SetState(LinkState s) {
  {
    std::lock_guard<std::mutex> lock{state_mutex_};
    state_.store(s);
  }
  state_cv_.notify_all();
}

bool MavlinkLink::WaitForState(LinkState s, std::chrono::milliseconds timeout) {
  std::unique_lock<std::mutex> lock{state_mutex_};
  return state_cv_.wait_for(lock, timeout, [&] { return state_.load() == s; });
}

void MavlinkLink::Start() {
  if (supervisor_.joinable()) {
    return;
  }
  if (cfg_.signing_enabled && cfg_.version == 1) {
    Log(LogLevel::kWarn, "podpis wymaga MAVLink v2 — podpisywanie wyłączone na łączu v1");
    cfg_.signing_enabled = false;
  }
  if (cfg_.signing_enabled) {
    const auto key = LoadSigningKey(cfg_.key_file);
    if (!key.HasValue()) {
      Log(LogLevel::kError, "błąd inicjalizacji podpisu: " + key.Error().SupportData() +
                                " — łącze nie nadaje");
      SetState(LinkState::kInitError);
      return;
    }
    LinkSigner::Config sc;
    sc.key = key.Value();
    sc.link_id = cfg_.signing_link_id;
    sc.accept_unsigned = cfg_.accept_unsigned;
    sc.state_file = cfg_.state_file;
    signer_ = std::make_unique<LinkSigner>(
        sc, now_, [this](const std::string& m) { Log(LogLevel::kWarn, m); });
    signer_->LoadPersisted();
  }
  SetState(LinkState::kWaitingForTransport);
  supervisor_ = std::jthread([this](std::stop_token st) { Supervise(st); });
}

void MavlinkLink::Stop() {
  if (supervisor_.joinable()) {
    supervisor_.request_stop();
    cv_.notify_all();
    supervisor_.join();
  }
  if (signer_) {
    signer_->PersistNow();  // stan podpisu nie może zginąć przy zatrzymaniu < 10 s
  }
  if (state_.load() != LinkState::kInitError) {
    SetState(LinkState::kStopped);
  }
}

bool MavlinkLink::SleepInterruptible(std::chrono::milliseconds d, std::stop_token st) {
  std::mutex m;
  std::condition_variable_any cv;
  std::unique_lock<std::mutex> lock{m};
  cv.wait_for(lock, st, d, [] { return false; });
  return !st.stop_requested();
}

void MavlinkLink::RecordError(const ara::core::ErrorCode& e) {
  std::lock_guard<std::mutex> lock{err_mutex_};
  last_error_ = e.SupportData();
  last_error_persistent_ = IsTransportError(e, TransportErrc::kPersistent);
  last_error_closed_ = IsTransportError(e, TransportErrc::kClosed);
}

void MavlinkLink::Supervise(std::stop_token st) {
  auto outage_start = Clock::now();
  bool warned_missing = false;
  while (!st.stop_requested()) {
    const auto tr = provider_(std::chrono::milliseconds(200), st);
    if (!tr) {
      SetState(LinkState::kWaitingForTransport);
      if (!warned_missing && Clock::now() - outage_start >= cfg_.transport_warn_after) {
        warned_missing = true;
        Log(LogLevel::kWarn,
            "brak transportu: wywołaj RegisterLinkTransport(); ruch czeka w kolejkach");
      }
      continue;
    }
    warned_missing = false;
    SetState(LinkState::kOpening);
    counters_.opens++;
    const auto opened = tr->Open(st);
    if (!opened.HasValue()) {
      counters_.open_errors++;
      SetState(LinkState::kOpenError);
      const bool persistent = IsTransportError(opened.Error(), TransportErrc::kPersistent);
      if (persistent) {
        if (!persistent_logged_) {
          persistent_logged_ = true;
          Log(LogLevel::kError, "Open() nie powiodło się (trwały): " + opened.Error().SupportData());
        }
      } else if (!IsTransportError(opened.Error(), TransportErrc::kClosed)) {
        Log(LogLevel::kWarn, "Open() nie powiodło się: " + opened.Error().SupportData());
      }
      if (!SleepInterruptible(cfg_.retry_interval, st)) {
        break;
      }
      continue;
    }
    persistent_logged_ = false;
    outage_start = Clock::now();
    {
      std::lock_guard<std::mutex> lock{err_mutex_};
      last_error_.clear();
      last_error_closed_ = false;
      last_error_persistent_ = false;
    }
    RunSession(tr, st);
    tr->Close();
    if (st.stop_requested()) {
      break;
    }
    bool closed_externally = false;
    std::string err;
    bool persistent = false;
    {
      std::lock_guard<std::mutex> lock{err_mutex_};
      closed_externally = last_error_closed_;
      err = last_error_;
      persistent = last_error_persistent_;
    }
    if (closed_externally) {
      Log(LogLevel::kInfo, "transport zamknięty z zewnątrz (" + err + ") — ponowne otwarcie");
      continue;
    }
    SetState(LinkState::kOpenError);
    if (persistent) {
      if (!persistent_logged_) {
        persistent_logged_ = true;
        Log(LogLevel::kError, "błąd transportu (trwały): " + err);
      }
    } else {
      Log(LogLevel::kWarn, "błąd transportu: " + err);
    }
    if (!SleepInterruptible(cfg_.retry_interval, st)) {
      break;
    }
  }
  SetState(LinkState::kStopped);
}

void MavlinkLink::RunSession(const std::shared_ptr<IByteTransport>& tr, std::stop_token st) {
  std::stop_source session_stop;
  std::stop_callback propagate{st, [&session_stop] { session_stop.request_stop(); }};
  std::atomic<bool> failed{false};
  SetState(LinkState::kRunning);
  Log(LogLevel::kInfo, "łącze gotowe (" + tr->Name() + ")");
  {
    std::jthread tx{[&] { TxLoop(tr, session_stop.get_token(), &failed, &session_stop); }};
    RxLoop(tr, session_stop.get_token(), &failed, &session_stop);
    session_stop.request_stop();
    tr->Close();  // odblokowuje ewentualny Write w TX
    cv_.notify_all();
  }
}

void MavlinkLink::RxLoop(const std::shared_ptr<IByteTransport>& tr, std::stop_token st,
                         std::atomic<bool>* failed, std::stop_source* session_stop) {
  FrameParser parser{[this](std::uint32_t id) -> std::optional<std::uint8_t> {
    if (id == kHeartbeatMsgId) {
      return kHeartbeatCrcExtra;
    }
    return crc_lookup_ ? crc_lookup_(id) : std::nullopt;
  }};
  parser.SetFilter([this](Frame& f) {
    if (!signer_) {
      return true;
    }
    const auto v = signer_->Verify(f);
    f.sig_verdict = static_cast<std::uint8_t>(v);
    // zły podpis = ramka najpewniej urwana/sklejona: parser ma zrobić rescan od bajtu po STX
    return v != SignatureVerdict::kBadSignature;
  });
  FrameParser::Stats prev;
  std::vector<std::uint8_t> buf(512);
  std::vector<Frame> frames;
  while (!st.stop_requested()) {
    const auto r = tr->Read(std::span<std::uint8_t>{buf}, cfg_.read_timeout_ms, st);
    if (!r.HasValue()) {
      RecordError(r.Error());
      if (!IsTransportError(r.Error(), TransportErrc::kClosed)) {
        failed->store(true);
      }
      session_stop->request_stop();
      return;
    }
    if (r.Value() == 0) {
      continue;
    }
    frames.clear();
    parser.Feed(std::span<const std::uint8_t>{buf.data(), r.Value()}, &frames);
    const auto& cur = parser.stats();
    counters_.rx_crc_err += cur.crc_errors - prev.crc_errors;
    counters_.rx_sig_err += cur.filter_rejected - prev.filter_rejected;
    counters_.rx_dropped += (cur.unknown_msgid - prev.unknown_msgid) +
                            (cur.bad_incompat_flags - prev.bad_incompat_flags);
    prev = cur;
    for (const auto& f : frames) {
      if (signer_ && static_cast<SignatureVerdict>(f.sig_verdict) != SignatureVerdict::kOk) {
        counters_.rx_sig_err++;
        continue;
      }
      counters_.rx_ok++;
      if (f.msgid == kHeartbeatMsgId) {
        counters_.heartbeats_rx++;
        continue;
      }
      if (handler_) {
        handler_(cfg_.name, f);
      }
    }
  }
}

std::optional<OutMessage> MavlinkLink::PickNext(Clock::time_point now,
                                                Clock::time_point* wake) {
  if (!fifo_.empty()) {
    OutMessage m = std::move(fifo_.front());
    fifo_.pop_front();
    return m;
  }
  for (auto it = event_order_.begin(); it != event_order_.end(); ++it) {
    auto& slot = events_[*it];
    const auto ready_at = slot.ever_sent ? slot.last_sent + slot.min_interval : now;
    if (ready_at <= now) {
      OutMessage m = std::move(slot.msg);
      slot.pending = false;
      slot.ever_sent = true;
      slot.last_sent = now;
      event_order_.erase(it);
      return m;
    }
    *wake = std::min(*wake, ready_at);
  }
  return std::nullopt;
}

void MavlinkLink::TxLoop(const std::shared_ptr<IByteTransport>& tr, std::stop_token st,
                         std::atomic<bool>* failed, std::stop_source* session_stop) {
  const bool hb = cfg_.heartbeat_hz > 0;
  const auto period = hb ? std::chrono::duration_cast<Clock::duration>(
                               std::chrono::duration<double>(1.0 / cfg_.heartbeat_hz))
                         : Clock::duration::zero();
  auto next_hb = Clock::now();
  std::unique_lock<std::mutex> lock{mutex_};
  for (;;) {
    if (st.stop_requested()) {
      return;
    }
    // Licznik pracy czytamy PRZED przeglądem kolejek: enqueue w oknie, gdy zwalniamy
    // blokadę (wysyłka, persist), nie może zgubić wybudzenia.
    const auto gen = work_gen_;
    const auto now = Clock::now();
    if (hb && now >= next_hb) {
      lock.unlock();
      OutMessage m;
      m.msg_id = kHeartbeatMsgId;
      m.crc_extra = kHeartbeatCrcExtra;
      m.payload = HeartbeatPayload();
      const bool ok = SendFrame(tr.get(), st, m, true);
      lock.lock();
      if (!ok) {
        if (!st.stop_requested()) {
          failed->store(true);
        }
        session_stop->request_stop();
        return;
      }
      next_hb = std::max(next_hb + period, Clock::now() - period);  // bez lawiny po opóźnieniu
      continue;
    }
    auto wake = now + std::chrono::seconds(1);
    if (hb) {
      wake = std::min(wake, next_hb);
    }
    auto m = PickNext(now, &wake);
    if (m.has_value()) {
      lock.unlock();
      const bool ok = SendFrame(tr.get(), st, *m, false);
      lock.lock();
      if (!ok) {
        if (!st.stop_requested()) {
          failed->store(true);
        }
        session_stop->request_stop();
        return;
      }
      continue;
    }
    if (signer_) {
      lock.unlock();
      signer_->PersistIfDue(static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::milliseconds>(
              Clock::now().time_since_epoch()).count()));
      lock.lock();
    }
    cv_.wait_until(lock, st, wake, [&] { return work_gen_ != gen; });
  }
}

bool MavlinkLink::SendFrame(IByteTransport* tr, std::stop_token st, const OutMessage& m,
                            bool heartbeat) {
  const std::uint8_t seq = tx_seq_++;
  std::vector<std::uint8_t> bytes;
  if (signer_ && cfg_.version == 2) {
    const SigningParams sp{signer_->key(), signer_->link_id(), signer_->NextTimestamp()};
    bytes = BuildFrame(2, seq, cfg_.system_id, cfg_.component_id, m.msg_id, m.payload,
                       m.crc_extra, &sp);
  } else {
    bytes = BuildFrame(cfg_.version, seq, cfg_.system_id, cfg_.component_id, m.msg_id,
                       m.payload, m.crc_extra);
  }
  if (st.stop_requested()) {
    return false;
  }
  std::size_t written = 0;
  while (written < bytes.size()) {
    const auto t0 = Clock::now();
    const auto r = tr->Write(std::span<const std::uint8_t>{bytes.data() + written,
                                                            bytes.size() - written},
                             cfg_.write_timeout_ms, st);
    if (!r.HasValue()) {
      RecordError(r.Error());
      return false;
    }
    written += r.Value();
    if (written >= bytes.size()) {
      break;
    }
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count();
    if (r.Value() == 0 || elapsed >= static_cast<long long>(cfg_.write_timeout_ms) * 9 / 10) {  // NOLINT
      // R5: timeout w trakcie ramki — porzucamy resztę, następna ramka idzie w całości.
      if (written > 0) {
        counters_.tx_partial++;
      } else {
        counters_.tx_dropped++;
      }
      return true;
    }
  }
  counters_.tx++;
  counters_.tx_bytes += bytes.size();
  if (heartbeat) {
    counters_.heartbeats_tx++;
  }
  return true;
}

ara::core::Result<void> MavlinkLink::EnqueueRequest(OutMessage m) {
  {
    std::lock_guard<std::mutex> lock{mutex_};
    if (fifo_.size() >= cfg_.fifo_limit) {
      return ara::com::MakeErrorCode(ara::com::ComErrc::kCommunicationLinkError,
                                     "kolejka TX łącza '" + cfg_.name + "' pełna");
    }
    fifo_.push_back(std::move(m));
    ++work_gen_;
  }
  cv_.notify_all();
  return {};
}

bool MavlinkLink::EnqueueResponse(OutMessage m) {
  {
    std::lock_guard<std::mutex> lock{mutex_};
    if (fifo_.size() >= cfg_.fifo_limit) {
      counters_.tx_dropped++;
      return false;
    }
    fifo_.push_back(std::move(m));
    ++work_gen_;
  }
  cv_.notify_all();
  return true;
}

void MavlinkLink::EnqueueEvent(std::uint64_t key, OutMessage m,
                               std::chrono::milliseconds min_interval) {
  {
    std::lock_guard<std::mutex> lock{mutex_};
    auto& slot = events_[key];
    slot.msg = std::move(m);
    slot.min_interval = min_interval;
    if (!slot.pending) {
      slot.pending = true;
      event_order_.push_back(key);
    }
    ++work_gen_;
  }
  cv_.notify_all();
}

LinkCounterSnapshot MavlinkLink::counters() const {
  LinkCounterSnapshot s;
  s.rx_ok = counters_.rx_ok.load();
  s.rx_crc_err = counters_.rx_crc_err.load();
  s.rx_sig_err = counters_.rx_sig_err.load();
  s.rx_dropped = counters_.rx_dropped.load();
  s.tx = counters_.tx.load();
  s.tx_bytes = counters_.tx_bytes.load();
  s.tx_partial = counters_.tx_partial.load();
  s.tx_dropped = counters_.tx_dropped.load();
  s.heartbeats_tx = counters_.heartbeats_tx.load();
  s.heartbeats_rx = counters_.heartbeats_rx.load();
  s.opens = counters_.opens.load();
  s.open_errors = counters_.open_errors.load();
  return s;
}

}  // namespace mavlink
}  // namespace com
}  // namespace ara
