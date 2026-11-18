/**
 * Copyright (c) 2024
 *
 * @file radio_link.cc
 */
#include "platform/common/mavlink_daemon/code/link/radio_link.h"

#include <algorithm>
#include <utility>

#include "ara/com/mavlink/transport_error.h"

namespace srp {
namespace mavlink_daemon {
namespace {
using ara::com::mavlink::BuildFrame;
using ara::com::mavlink::Frame;
using ara::com::mavlink::FrameParser;
using ara::com::mavlink::IsTransportError;
using ara::com::mavlink::LogLevel;
using ara::com::mavlink::SignatureVerdict;
using ara::com::mavlink::SigningParams;
using ara::com::mavlink::TransportErrc;
using Clock = std::chrono::steady_clock;

constexpr std::size_t kMaxRxQueuedBytes = 64 * 1024;  // zabezpieczenie przed zalewem z drivera

const std::vector<std::uint8_t>& HeartbeatPayload() {
  // custom_mode(4) | type=MAV_TYPE_ONBOARD_CONTROLLER(18) | autopilot=INVALID(8) |
  // base_mode | system_status=ACTIVE(4) | mavlink_version(3)
  static const std::vector<std::uint8_t> p{0, 0, 0, 0, 18, 8, 0, 4, 3};
  return p;
}
}  // namespace

RadioLink::RadioLink(RadioLinkConfig cfg, std::shared_ptr<ara::com::mavlink::IRadioDriver> driver,
                     CrcLookup crc_lookup, FrameHandler on_frame, ReadyHandler on_ready,
                     ara::com::mavlink::LogFn log, ara::com::mavlink::LinkSigner::NowFn now)
    : cfg_{std::move(cfg)},
      driver_{std::move(driver)},
      crc_lookup_{std::move(crc_lookup)},
      on_frame_{std::move(on_frame)},
      on_ready_{std::move(on_ready)},
      log_{std::move(log)},
      now_{std::move(now)} {}

RadioLink::~RadioLink() { Stop(); }

void RadioLink::Log(LogLevel l, const std::string& m) const {
  if (log_) {
    log_(l, "[mavlink_daemon:" + cfg_.name + "] " + m);
  }
}

void RadioLink::SetState(RadioLinkState s) {
  {
    std::lock_guard<std::mutex> lock{state_mutex_};
    state_.store(s);
  }
  state_cv_.notify_all();
}

void RadioLink::SetReady(bool ready) {
  if (ready_.exchange(ready) == ready) {
    return;
  }
  if (on_ready_) {
    on_ready_(cfg_.name, ready);
  }
}

bool RadioLink::WaitForState(RadioLinkState s, std::chrono::milliseconds timeout) {
  std::unique_lock<std::mutex> lock{state_mutex_};
  return state_cv_.wait_for(lock, timeout, [&] { return state_.load() == s; });
}

void RadioLink::Start() {
  if (supervisor_.joinable()) {
    return;
  }
  if (cfg_.signing_enabled && cfg_.version == 1) {
    Log(LogLevel::kWarn, "podpis wymaga MAVLink v2 — podpisywanie wyłączone na łączu v1");
    cfg_.signing_enabled = false;
  }
  if (cfg_.signing_enabled) {
    const auto key = ara::com::mavlink::LoadSigningKey(cfg_.key_file);
    if (!key.HasValue()) {
      Log(LogLevel::kError, "błąd inicjalizacji podpisu: " + key.Error().SupportData() + " — łącze nie nadaje");
      SetState(RadioLinkState::kInitError);
      return;
    }
    ara::com::mavlink::LinkSigner::Config sc;
    sc.key = key.Value();
    sc.link_id = cfg_.signing_link_id;
    sc.accept_unsigned = cfg_.accept_unsigned;
    sc.state_file = cfg_.state_file;
    signer_ = std::make_unique<ara::com::mavlink::LinkSigner>(
        sc, now_, [this](const std::string& m) { Log(LogLevel::kWarn, m); });
    signer_->LoadPersisted();
  }
  SetState(RadioLinkState::kStarting);
  rx_thread_ = std::jthread([this](std::stop_token st) { RxLoop(st); });
  supervisor_ = std::jthread([this](std::stop_token st) { Supervise(st); });
}

void RadioLink::Stop() {
  if (supervisor_.joinable()) {
    supervisor_.request_stop();
    cv_.notify_all();
    supervisor_.join();  // kończy też Stop() drivera (zero callbacków po nim)
  }
  if (rx_thread_.joinable()) {
    rx_thread_.request_stop();
    rx_cv_.notify_all();
    rx_thread_.join();
  }
  if (signer_) {
    signer_->PersistNow();  // stan podpisu nie może zginąć przy zatrzymaniu < 10 s
  }
  if (state_.load() != RadioLinkState::kInitError) {
    SetState(RadioLinkState::kStopped);
  }
}

bool RadioLink::SleepInterruptible(std::chrono::milliseconds d, std::stop_token st) {
  std::mutex m;
  std::condition_variable_any cv;
  std::unique_lock<std::mutex> lock{m};
  cv.wait_for(lock, st, d, [] { return false; });
  return !st.stop_requested();
}

// ------------------------------------------------------------- IRadioListener

void RadioLink::OnReceive(std::span<const std::uint8_t> bytes) noexcept {
  try {
    {
      std::lock_guard<std::mutex> lock{rx_mutex_};
      if (rx_queued_bytes_ + bytes.size() > kMaxRxQueuedBytes) {
        counters_.rx_dropped++;  // RX nie nadąża: porzucamy kawałek (parser zresynchronizuje się)
        return;
      }
      rx_queue_.emplace_back(bytes.begin(), bytes.end());
      rx_queued_bytes_ += bytes.size();
    }
    rx_cv_.notify_one();
  } catch (...) {
    counters_.rx_dropped++;
  }
}

void RadioLink::OnTxReady() noexcept {
  {
    std::lock_guard<std::mutex> lock{mutex_};
    tx_ready_ = true;
  }
  cv_.notify_all();
}

void RadioLink::OnError(ara::core::ErrorCode e, bool fatal) noexcept {
  try {
    if (!fatal) {
      Log(LogLevel::kWarn, "driver: " + e.SupportData());
      return;
    }
    {
      std::lock_guard<std::mutex> lock{mutex_};
      fatal_ = true;
      fatal_text_ = e.SupportData();
    }
    cv_.notify_all();
  } catch (...) {
  }
}

// ------------------------------------------------------------------ nadzór

void RadioLink::Supervise(std::stop_token st) {
  while (!st.stop_requested()) {
    SetState(RadioLinkState::kStarting);
    counters_.starts++;
    {
      std::lock_guard<std::mutex> lock{mutex_};
      fatal_ = false;
      fatal_text_.clear();
      tx_ready_ = false;
    }
    {
      std::lock_guard<std::mutex> lock{rx_mutex_};
      rx_queue_.clear();
      rx_queued_bytes_ = 0;
      rx_reset_ = true;
    }
    const auto started = driver_->Start(*this, st);
    if (!started.HasValue()) {
      counters_.start_errors++;
      driver_->Stop();
      SetState(RadioLinkState::kRestartWait);
      if (IsTransportError(started.Error(), TransportErrc::kPersistent)) {
        if (!persistent_logged_) {
          persistent_logged_ = true;
          Log(LogLevel::kError, "start drivera " + driver_->Name() + " nie powiódł się (trwały): " +
                                    started.Error().SupportData());
        }
      } else if (!IsTransportError(started.Error(), TransportErrc::kClosed)) {
        Log(LogLevel::kWarn, "start drivera " + driver_->Name() + " nie powiódł się: " +
                                 started.Error().SupportData());
      }
      if (!SleepInterruptible(cfg_.restart_delay, st)) {
        break;
      }
      continue;
    }
    persistent_logged_ = false;
    running_count_++;
    Log(LogLevel::kInfo, "łącze gotowe (" + driver_->Name() + ")");
    SetReady(true);  // przed kRunning: kto zobaczy kRunning, widzi też IsReady()
    SetState(RadioLinkState::kRunning);
    const bool restart = TxLoop(st);
    SetReady(false);
    driver_->Stop();  // po powrocie zero callbacków; jedyne miejsce, które woła Stop() przy błędzie
    if (st.stop_requested() || !restart) {
      break;
    }
    std::string why;
    {
      std::lock_guard<std::mutex> lock{mutex_};
      why = fatal_text_;
    }
    SetState(RadioLinkState::kRestartWait);
    Log(LogLevel::kError, "błąd drivera: " + why + " — ponowny start za " +
                              std::to_string(cfg_.restart_delay.count()) + " ms");
    if (!SleepInterruptible(cfg_.restart_delay, st)) {
      break;
    }
  }
  SetReady(false);
  SetState(RadioLinkState::kStopped);
}

void RadioLink::RxLoop(std::stop_token st) {
  FrameParser parser{[this](std::uint32_t id) -> std::optional<std::uint8_t> {
    if (id == ara::com::mavlink::kHeartbeatMsgId) {
      return ara::com::mavlink::kHeartbeatCrcExtra;
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
  std::vector<Frame> frames;
  for (;;) {
    std::vector<std::uint8_t> chunk;
    {
      std::unique_lock<std::mutex> lock{rx_mutex_};
      rx_cv_.wait(lock, st, [&] { return !rx_queue_.empty() || rx_reset_; });
      if (st.stop_requested()) {
        return;
      }
      if (rx_reset_) {
        rx_reset_ = false;
        parser.Reset();  // nowa sesja drivera: ucięta ramka z poprzedniej nie może sklejać się z nową
      }
      if (rx_queue_.empty()) {
        continue;
      }
      chunk = std::move(rx_queue_.front());
      rx_queue_.pop_front();
      rx_queued_bytes_ -= chunk.size();
    }
    frames.clear();
    parser.Feed(chunk, &frames);
    const auto& cur = parser.stats();
    counters_.rx_crc_err += cur.crc_errors - prev.crc_errors;
    counters_.rx_sig_err += cur.filter_rejected - prev.filter_rejected;
    counters_.rx_dropped +=
        (cur.unknown_msgid - prev.unknown_msgid) + (cur.bad_incompat_flags - prev.bad_incompat_flags);
    prev = cur;
    for (const auto& f : frames) {
      if (signer_ && static_cast<SignatureVerdict>(f.sig_verdict) != SignatureVerdict::kOk) {
        counters_.rx_sig_err++;
        continue;
      }
      counters_.rx_ok++;
      if (f.msgid == ara::com::mavlink::kHeartbeatMsgId) {
        counters_.heartbeats_rx++;
        continue;
      }
      if (on_frame_) {
        on_frame_(cfg_.name, f);
      }
    }
  }
}

// ---------------------------------------------------------------------- TX

std::optional<OutFrame> RadioLink::PickNext(Clock::time_point now, Clock::time_point* wake) {
  while (!fifo_.empty()) {
    OutFrame m = std::move(fifo_.front());
    fifo_.pop_front();
    if (m.deadline < now) {
      counters_.tx_expired++;  // REQ przeterminowany w kolejce: odpowiedź i tak byłaby spóźniona
      continue;
    }
    return m;
  }
  for (auto it = event_order_.begin(); it != event_order_.end(); ++it) {
    auto& slot = events_[*it];
    const auto ready_at = slot.ever_sent ? slot.last_sent + slot.min_interval : now;
    if (ready_at <= now) {
      OutFrame m = std::move(slot.msg);
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

bool RadioLink::TxLoop(std::stop_token st) {
  const bool hb = cfg_.heartbeat_hz > 0;
  const auto period =
      hb ? std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / cfg_.heartbeat_hz))
         : Clock::duration::zero();
  auto next_hb = Clock::now();
  std::unique_lock<std::mutex> lock{mutex_};
  for (;;) {
    if (st.stop_requested()) {
      return false;
    }
    if (fatal_) {
      counters_.fatal_errors++;
      return true;
    }
    // Licznik pracy czytamy PRZED przeglądem kolejek: enqueue w oknie, gdy zwalniamy
    // blokadę (wysyłka, persist), nie może zgubić wybudzenia.
    const auto gen = work_gen_;
    const auto now = Clock::now();
    if (hb && now >= next_hb) {
      lock.unlock();
      OutFrame m;
      m.msg_id = ara::com::mavlink::kHeartbeatMsgId;
      m.crc_extra = ara::com::mavlink::kHeartbeatCrcExtra;
      m.payload = HeartbeatPayload();
      const bool ok = SendFrame(st, m, true);
      lock.lock();
      if (!ok) {
        if (!st.stop_requested()) {
          counters_.fatal_errors++;
        }
        return !st.stop_requested();
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
      const bool ok = SendFrame(st, *m, false);
      lock.lock();
      if (!ok) {
        if (!st.stop_requested()) {
          counters_.fatal_errors++;
        }
        return !st.stop_requested();
      }
      continue;
    }
    if (signer_) {
      lock.unlock();
      signer_->PersistIfDue(static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count()));
      lock.lock();
    }
    cv_.wait_until(lock, st, wake, [&] { return work_gen_ != gen || fatal_; });
  }
}

bool RadioLink::SendFrame(std::stop_token st, const OutFrame& m, bool heartbeat) {
  const std::uint8_t seq = tx_seq_++;
  std::vector<std::uint8_t> bytes;
  if (signer_ && cfg_.version == 2) {
    const SigningParams sp{signer_->key(), signer_->link_id(), signer_->NextTimestamp()};
    bytes = BuildFrame(2, seq, cfg_.system_id, cfg_.component_id, m.msg_id, m.payload, m.crc_extra, &sp);
  } else {
    bytes = BuildFrame(cfg_.version, seq, cfg_.system_id, cfg_.component_id, m.msg_id, m.payload, m.crc_extra);
  }
  auto busy_since = Clock::time_point::max();
  bool warned = false;
  for (;;) {
    if (st.stop_requested()) {
      return false;
    }
    {
      std::lock_guard<std::mutex> lock{mutex_};
      if (fatal_) {
        counters_.tx_dropped++;  // wstrzymana/niewysłana ramka przepada razem z driverem
        return false;
      }
      tx_ready_ = false;  // stary sygnał nie liczy się; nowy może przyjść zaraz po kBusy
    }
    const auto r = driver_->Send(bytes);
    if (r.HasValue()) {
      counters_.tx++;
      counters_.tx_bytes += bytes.size();
      if (heartbeat) {
        counters_.heartbeats_tx++;
      }
      return true;
    }
    if (!IsTransportError(r.Error(), TransportErrc::kBusy)) {
      if (IsTransportError(r.Error(), TransportErrc::kRetryable)) {
        counters_.tx_dropped++;
        Log(LogLevel::kWarn, "Send() odrzucone (ramka porzucona): " + r.Error().SupportData());
        return true;
      }
      {
        std::lock_guard<std::mutex> lock{mutex_};
        if (!fatal_) {
          fatal_ = true;
          fatal_text_ = r.Error().SupportData();
        }
      }
      counters_.tx_dropped++;
      return false;
    }
    // kBusy: ramka czeka do OnTxReady() (bez zapisu częściowego, bez wyprzedzania).
    counters_.tx_busy++;
    if (busy_since == Clock::time_point::max()) {
      busy_since = Clock::now();
    }
    std::unique_lock<std::mutex> lock{mutex_};
    while (!tx_ready_ && !fatal_ && !st.stop_requested()) {
      cv_.wait_for(lock, st, std::chrono::milliseconds(200), [&] { return tx_ready_ || fatal_; });
      if (!warned && Clock::now() - busy_since >= cfg_.busy_warn_after) {
        warned = true;
        Log(LogLevel::kWarn, "radio zajęte od " + std::to_string(cfg_.busy_warn_after.count()) +
                                 " ms (brak OnTxReady) — ramka wstrzymana");
      }
    }
  }
}

bool RadioLink::EnqueueRequest(OutFrame m) {
  {
    std::lock_guard<std::mutex> lock{mutex_};
    if (fifo_.size() >= cfg_.fifo_limit) {
      return false;
    }
    fifo_.push_back(std::move(m));
    ++work_gen_;
  }
  cv_.notify_all();
  return true;
}

bool RadioLink::EnqueueResponse(OutFrame m) {
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

void RadioLink::EnqueueEvent(std::uint64_t key, OutFrame m, std::chrono::milliseconds min_interval) {
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

void RadioLink::DropEvents(std::uint64_t key_mask, std::uint64_t key_value) {
  std::lock_guard<std::mutex> lock{mutex_};
  for (auto it = events_.begin(); it != events_.end();) {
    if ((it->first & key_mask) == key_value) {
      event_order_.erase(std::remove(event_order_.begin(), event_order_.end(), it->first), event_order_.end());
      it = events_.erase(it);
    } else {
      ++it;
    }
  }
}

RadioLinkCounters RadioLink::counters() const {
  RadioLinkCounters s;
  s.rx_ok = counters_.rx_ok.load();
  s.rx_crc_err = counters_.rx_crc_err.load();
  s.rx_sig_err = counters_.rx_sig_err.load();
  s.rx_dropped = counters_.rx_dropped.load();
  s.tx = counters_.tx.load();
  s.tx_bytes = counters_.tx_bytes.load();
  s.tx_busy = counters_.tx_busy.load();
  s.tx_dropped = counters_.tx_dropped.load();
  s.tx_expired = counters_.tx_expired.load();
  s.heartbeats_tx = counters_.heartbeats_tx.load();
  s.heartbeats_rx = counters_.heartbeats_rx.load();
  s.starts = counters_.starts.load();
  s.start_errors = counters_.start_errors.load();
  s.fatal_errors = counters_.fatal_errors.load();
  return s;
}

}  // namespace mavlink_daemon
}  // namespace srp
