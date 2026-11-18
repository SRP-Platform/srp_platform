/**
 * Copyright (c) 2024
 *
 * @file radio_link.cc
 */
#include "platform/common/mavlink_daemon/code/link/radio_link.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>  // NOLINT
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "ara/com/mavlink/transport_error.h"
#include "core/common/condition.h"

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

constexpr std::size_t kFrameOverhead = 12;  // MAVLink 2 header 10 + CRC 2
constexpr std::size_t kMaxRxQueuedBytes = 64 * 1024;  // guard against a flood from the driver
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
      now_{std::move(now)} {
  if (cfg_.link.air_rate_bps > 0) {
    budget_bytes_per_s_ = static_cast<double>(cfg_.link.air_rate_bps) * cfg_.link.max_utilization / 8.0;
  }
}

RadioLink::~RadioLink() { Stop(); }

void RadioLink::Log(LogLevel l, const std::string& m) const {
  if (log_) {
    log_(l, "[mavlink_daemon:" + cfg_.link.name + "] " + m);
  }
}

void RadioLink::SetState(RadioLinkState s) { state_.store(s); }

void RadioLink::SetReady(bool ready) {
  if (ready_.exchange(ready) == ready) {
    return;
  }
  if (on_ready_) {
    on_ready_(cfg_.link.name, ready);
  }
}

void RadioLink::WarnWhenKeyFileIsReadableByOthers() {
  std::error_code ec;
  const auto perms = std::filesystem::status(cfg_.link.key_file, ec).permissions();
  if (ec) {
    return;  // a missing file is reported by LoadSigningKey
  }
  namespace fs = std::filesystem;
  if ((perms & (fs::perms::group_all | fs::perms::others_all)) != fs::perms::none) {
    char mode[8];
    std::snprintf(mode, sizeof(mode), "%04o", static_cast<unsigned>(perms & fs::perms::mask));
    Log(LogLevel::kWarn, "signing key file '" + cfg_.link.key_file + "' has permissions " + std::string(mode) +
                             ", wider than 0600: anyone who can read it can forge frames of this link");
  }
}

void RadioLink::Start() {
  if (supervisor_.joinable()) {
    return;
  }
  if (cfg_.link.signing_enabled) {
    WarnWhenKeyFileIsReadableByOthers();
    const auto key = ara::com::mavlink::LoadSigningKey(cfg_.link.key_file);
    if (!key.HasValue()) {
      Log(LogLevel::kError, "signing initialisation failed: " + key.Error().SupportData() +
                                " — the link does not transmit");
      SetState(RadioLinkState::kInitError);
      return;
    }
    ara::com::mavlink::LinkSigner::Config sc;
    sc.key = key.Value();
    sc.link_id = cfg_.link.signing_link_id;
    sc.accept_unsigned = cfg_.link.accept_unsigned;
    sc.state_file = cfg_.link.state_file;
    sc.min_timestamp = ara::com::mavlink::BuildTimestamp10us();
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
    supervisor_.join();  // also ends the driver Stop() (no callbacks after it)
  }
  if (rx_thread_.joinable()) {
    rx_thread_.request_stop();
    rx_cv_.notify_all();
    rx_thread_.join();
  }
  if (signer_) {
    signer_->PersistNow();  // the signing state must not be lost when stopping within 10 s
  }
  if (state_.load() != RadioLinkState::kInitError) {
    SetState(RadioLinkState::kStopped);
  }
}

// ------------------------------------------------------------- IRadioListener

void RadioLink::OnReceive(std::span<const std::uint8_t> bytes) noexcept {
  try {
    {
      std::lock_guard<std::mutex> lock{rx_mutex_};
      if (rx_queued_bytes_ + bytes.size() > kMaxRxQueuedBytes) {
        counters_.rx_dropped++;  // RX cannot keep up: drop the chunk (the parser resynchronises)
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

void RadioLink::OnLinkStats(const ara::com::mavlink::RadioLinkStats& stats) noexcept {
  const auto now = SteadyNow();
  std::lock_guard<std::mutex> lock{stats_mutex_};
  link_stats_.stats = stats;
  link_stats_.have = true;
  link_stats_.at = now;
}

RadioLinkStatsSnapshot RadioLink::link_stats() const {
  std::lock_guard<std::mutex> lock{stats_mutex_};
  return link_stats_;
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

// ------------------------------------------------------------------ supervision

void RadioLink::Supervise(std::stop_token st) {
  while (!st.stop_requested()) {
    SetState(RadioLinkState::kStarting);
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
    const auto session_start = SteadyNow();
    if (!started.HasValue()) {
      counters_.start_errors++;
      driver_->Stop();
      SetState(RadioLinkState::kRestartWait);
      if (IsTransportError(started.Error(), TransportErrc::kPersistent)) {
        if (!persistent_logged_) {
          persistent_logged_ = true;
          Log(LogLevel::kError, "driver " + driver_->Name() + " failed to start (persistent): " +
                                    started.Error().SupportData());
        }
      } else if (!IsTransportError(started.Error(), TransportErrc::kClosed)) {
        Log(LogLevel::kWarn, "driver " + driver_->Name() + " failed to start: " +
                                 started.Error().SupportData());
      }
      if (!Pause(st, NextBackoff())) {
        break;
      }
      continue;
    }
    persistent_logged_ = false;
    Log(LogLevel::kInfo, "link ready (" + driver_->Name() + ")");
    SetReady(true);  // before kRunning: whoever sees kRunning also sees IsReady()
    SetState(RadioLinkState::kRunning);
    const bool restart = TxLoop(st);
    SetReady(false);
    driver_->Stop();  // no callbacks after it returns; the only place that calls Stop() on error
    if (st.stop_requested() || !restart) {
      break;
    }
    std::string why;
    {
      std::lock_guard<std::mutex> lock{mutex_};
      why = fatal_text_;
    }
    SetState(RadioLinkState::kRestartWait);
    if (SteadyNow() - session_start >= cfg_.stable_after) {
      next_backoff_ = std::chrono::milliseconds{0};  // a long start: the failure series begins anew
    }
    const auto delay = NextBackoff();
    Log(LogLevel::kError, "driver error: " + why + " — restarting in " + std::to_string(delay.count()) + " ms");
    if (!Pause(st, delay)) {
      break;
    }
  }
  SetReady(false);
  SetState(RadioLinkState::kStopped);
}

std::chrono::milliseconds RadioLink::NextBackoff() {
  const auto& b = cfg_.restart_backoff;
  const auto delay = next_backoff_.count() == 0 ? b.min : std::clamp(next_backoff_, b.min, b.max);
  next_backoff_ = std::min(delay * 2, b.max);
  return delay;
}

bool RadioLink::Pause(std::stop_token st, std::chrono::milliseconds delay) {
  if (cfg_.wait) {
    return cfg_.wait(delay, st);
  }
  srp::core::condition::wait_for(delay, st);
  return !st.stop_requested();
}

void RadioLink::RxLoop(std::stop_token st) {
  FrameParser parser{[this](std::uint32_t id) -> std::optional<std::uint8_t> {
    return crc_lookup_ ? crc_lookup_(id) : std::nullopt;
  }};
  parser.SetFilter([this](Frame& f, std::span<const std::uint8_t> raw) {
    if (!signer_) {
      return true;
    }
    const auto v = signer_->VerifySignature(f, raw);
    f.sig_verdict = static_cast<std::uint8_t>(v);
    // a bad signature = the frame is most likely cut/glued: the parser rescans from the byte
    // after STX. Replay state is NOT touched here (see the loop below).
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
        parser.Reset();  // new driver session: a truncated frame must not glue to the next one
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
        (cur.unknown_msgid - prev.unknown_msgid) + (cur.bad_incompat_flags - prev.bad_incompat_flags) +
        (cur.v1_rejected - prev.v1_rejected);
    prev = cur;
    for (const auto& f : frames) {
      if (signer_ && (static_cast<SignatureVerdict>(f.sig_verdict) != SignatureVerdict::kOk ||
                      signer_->CheckReplay(f) != SignatureVerdict::kOk)) {
        counters_.rx_sig_err++;
        continue;
      }
      counters_.rx_ok++;
      bool accepted = true;
      if (on_frame_) {
        accepted = on_frame_(cfg_.link.name, f);
      }
      if (signer_ && accepted) {
        signer_->Commit(f);  // only a fully accepted frame moves the replay window
      }
    }
  }
}

// ---------------------------------------------------------------------- TX

std::size_t RadioLink::AirSize(const OutFrame& m) const {
  return kFrameOverhead + m.payload.size() + (signer_ ? ara::com::mavlink::kSignatureLen : 0);
}

std::optional<OutFrame> RadioLink::PickNext(Clock::time_point now, Clock::time_point* wake) {
  if (budget_bytes_per_s_ > 0 && now < tx_free_at_) {
    // The budget is spent: nothing leaves yet. The frames wait here (events keep coalescing), but a
    // REQ must still expire on time, so the wake-up is also the earliest REQ deadline.
    for (auto it = fifo_.begin(); it != fifo_.end();) {
      if (it->deadline < now) {
        counters_.tx_expired++;
        it = fifo_.erase(it);
      } else {
        if (it->deadline != Clock::time_point::max()) {
          *wake = std::min(*wake, it->deadline + std::chrono::milliseconds(1));
        }
        ++it;
      }
    }
    *wake = std::min(*wake, tx_free_at_);
    return std::nullopt;
  }
  auto picked = PickUnpaced(now, wake);
  if (picked.has_value() && budget_bytes_per_s_ > 0) {
    // One-frame bucket (GCRA with zero tolerance): here `now >= tx_free_at_` always holds, so the next frame
    // may leave one frame time after this one, never earlier. Keeping the old grid instead (tx_free_at_ += cost)
    // would let two frames out back to back after a wait shorter than two frame times. The TX loop wakes up
    // exactly at tx_free_at_, so the jitter costs almost nothing.
    const auto cost = std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double>(static_cast<double>(AirSize(*picked)) / budget_bytes_per_s_));
    tx_free_at_ = now + cost;
  }
  return picked;
}

std::optional<OutFrame> RadioLink::PickUnpaced(Clock::time_point now, Clock::time_point* wake) {
  while (!fifo_.empty()) {
    OutFrame m = std::move(fifo_.front());
    fifo_.pop_front();
    if (m.deadline < now) {
      counters_.tx_expired++;  // REQ expired in the queue: the reply would be late anyway
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
  std::unique_lock<std::mutex> lock{mutex_};
  for (;;) {
    if (st.stop_requested()) {
      return false;
    }
    if (fatal_) {
      counters_.fatal_errors++;
      return true;
    }
    // The work counter is read BEFORE scanning the queues: an enqueue in the window while the
    // lock is released (send, persist) must not lose the wake-up.
    const auto gen = work_gen_;
    const auto now = Clock::now();
    auto wake = now + std::chrono::seconds(1);
    auto m = PickNext(now, &wake);
    if (m.has_value()) {
      lock.unlock();
      const bool ok = SendFrame(st, *m);
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

bool RadioLink::SendFrame(std::stop_token st, const OutFrame& m) {
  const std::uint8_t seq = tx_seq_++;
  std::vector<std::uint8_t> bytes;
  if (signer_) {
    const SigningParams sp{signer_->key(), signer_->link_id(), signer_->NextTimestamp()};
    bytes = BuildFrame(seq, cfg_.system_id, cfg_.component_id, m.msg_id, m.payload, m.crc_extra, &sp);
  } else {
    bytes = BuildFrame(seq, cfg_.system_id, cfg_.component_id, m.msg_id, m.payload, m.crc_extra);
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
        counters_.tx_dropped++;  // the held / unsent frame is lost together with the driver
        return false;
      }
      tx_ready_ = false;  // an old signal does not count; a new one may arrive right after kBusy
    }
    const auto r = driver_->Send(bytes);
    if (r.HasValue()) {
      counters_.tx_frames++;
      return true;
    }
    if (!IsTransportError(r.Error(), TransportErrc::kBusy)) {
      if (IsTransportError(r.Error(), TransportErrc::kRetryable)) {
        counters_.tx_dropped++;
        LogSendRejected(r.Error().SupportData());
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
    // kBusy: the frame waits for OnTxReady() (no partial write, no overtaking).
    counters_.tx_busy++;
    if (busy_since == Clock::time_point::max()) {
      busy_since = Clock::now();
    }
    std::unique_lock<std::mutex> lock{mutex_};
    while (!tx_ready_ && !fatal_ && !st.stop_requested()) {
      cv_.wait_for(lock, st, std::chrono::milliseconds(200), [&] { return tx_ready_ || fatal_; });
      if (!warned && Clock::now() - busy_since >= cfg_.busy_warn_after) {
        warned = true;
        Log(LogLevel::kWarn, "radio busy for " + std::to_string(cfg_.busy_warn_after.count()) +
                                 " ms (no OnTxReady) — frame held");
      }
    }
  }
}

void RadioLink::LogSendRejected(const std::string& error) {
  constexpr auto kInterval = std::chrono::seconds(10);
  const auto now = SteadyNow();
  ++send_reject_pending_;
  if (!send_reject_logged_) {
    send_reject_logged_ = true;
    send_reject_logged_at_ = now;
    send_reject_pending_ = 0;
    Log(LogLevel::kWarn, "Send() rejected (frame dropped): " + error);
    return;
  }
  if (now - send_reject_logged_at_ < kInterval) {
    return;
  }
  const auto n = send_reject_pending_;
  send_reject_logged_at_ = now;
  send_reject_pending_ = 0;
  Log(LogLevel::kWarn, "Send() rejected: " + std::to_string(n) + " frames dropped in the last 10 s: " + error);
}

bool RadioLink::EnqueueRequest(OutFrame m) {
  if (m.deadline == Clock::time_point::max()) {
    m.deadline = SteadyNow() + std::chrono::milliseconds(cfg_.link.method_timeout_ms);
  }
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
  s.tx_frames = counters_.tx_frames.load();
  s.tx_busy = counters_.tx_busy.load();
  s.tx_dropped = counters_.tx_dropped.load();
  s.tx_expired = counters_.tx_expired.load();
  s.start_errors = counters_.start_errors.load();
  s.fatal_errors = counters_.fatal_errors.load();
  return s;
}

}  // namespace mavlink_daemon
}  // namespace srp
