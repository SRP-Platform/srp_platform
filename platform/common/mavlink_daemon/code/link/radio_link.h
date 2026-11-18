/**
 * Copyright (c) 2024
 *
 * @file radio_link.h
 * @brief MavlinkDaemon link: one radio driver (IRadioDriver) + TX queues, HEARTBEAT, v2
 * signing, the RX parser and counters.
 *
 * Link threads:
 *  - supervisor: Start() of the driver (failure => retry after `restart_delay`), the TX loop
 *    (HEARTBEAT -> REQ/RESP FIFO -> coalesced, rate-limited events), Stop() of the driver after
 *    a fatal error and a new Start() after `restart_delay` (30 s);
 *  - RX: copies chunks from OnReceive (driver thread) into a queue and parses frames (CRC,
 *    signature).
 *
 * TX: a frame is built and signed right before Send(). `kBusy` holds EXACTLY that frame until
 * OnTxReady() (transmit order = signature timestamp order; nothing overtakes the held frame,
 * nothing is written partially or transmitted twice).
 *
 * RX: the signature is verified inside the parser filter (a bad signature makes the parser
 * rescan), but the replay window of the signer only moves once the daemon core accepted the frame
 * after all of its own filters (instance link, target/source, dedup).
 */
#ifndef PLATFORM_COMMON_MAVLINK_DAEMON_CODE_LINK_RADIO_LINK_H_
#define PLATFORM_COMMON_MAVLINK_DAEMON_CODE_LINK_RADIO_LINK_H_

#include <atomic>
#include <chrono>  // NOLINT
#include <condition_variable>  // NOLINT
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>  // NOLINT
#include <optional>
#include <string>
#include <thread>  // NOLINT
#include <vector>

#include "ara/com/mavlink/mavlink_frame.h"
#include "ara/com/mavlink/mavlink_log.h"
#include "ara/com/mavlink/mavlink_signing.h"
#include "ara/com/mavlink/radio_driver.h"
#include "ara/core/result.h"
#include "platform/common/mavlink_daemon/code/config/daemon_config.h"

namespace srp {
namespace mavlink_daemon {

struct RadioLinkConfig {
  LinkConfigEntry link;        ///< driver-independent link settings from the daemon config
  std::uint8_t system_id{1};
  std::uint8_t component_id{1};
  /// Pause before restarting the driver after an error (fatal, or Start() failed).
  std::chrono::milliseconds restart_delay{30000};
  /// After this long in continuous kBusy a Warn is logged (once per hold).
  std::chrono::milliseconds busy_warn_after{5000};
  std::size_t fifo_limit{32};  ///< REQ/RESP FIFO per link
};

enum class RadioLinkState {
  kStopped,
  kStarting,      ///< driver Start() in progress
  kRunning,
  kRestartWait,   ///< error; driver Start() again after restart_delay
  kInitError      ///< e.g. a bad signing key: the link does not transmit
};

struct RadioLinkCounters {
  std::uint64_t rx_ok{0};
  std::uint64_t rx_crc_err{0};
  std::uint64_t rx_sig_err{0};
  std::uint64_t rx_dropped{0};     ///< unknown msgid / flags / RX buffer overflow
  std::uint64_t tx_busy{0};        ///< how many times Send() returned kBusy
  std::uint64_t tx_dropped{0};     ///< RESP FIFO full / transient driver error / frame held during a restart
  std::uint64_t tx_expired{0};     ///< REQs that exceeded the method timeout in the FIFO
  std::uint64_t heartbeats_rx{0};
  std::uint64_t start_errors{0};
  std::uint64_t fatal_errors{0};
};

/// A message ready to transmit (full payload, zeros not truncated).
struct OutFrame {
  std::uint32_t msg_id{0};
  std::uint8_t crc_extra{0};
  std::vector<std::uint8_t> payload;
  /// A REQ still waiting in the FIFO after this time is dropped (the reply would be late anyway).
  std::chrono::steady_clock::time_point deadline{std::chrono::steady_clock::time_point::max()};
};

class RadioLink final : public ara::com::mavlink::IRadioListener {
 public:
  using Clock = std::chrono::steady_clock;
  using CrcLookup = ara::com::mavlink::FrameParser::CrcExtraLookup;
  /// Called on the RX thread after CRC and signature. Returns true when the frame passed every
  /// further filter of the daemon core and was accepted; only then the replay window moves.
  using FrameHandler = std::function<bool(const std::string& link, const ara::com::mavlink::Frame&)>;
  /// Link readiness change (true = can transmit). Called from the supervisor thread.
  using ReadyHandler = std::function<void(const std::string& link, bool ready)>;

  RadioLink(RadioLinkConfig cfg, std::shared_ptr<ara::com::mavlink::IRadioDriver> driver, CrcLookup crc_lookup,
            FrameHandler on_frame, ReadyHandler on_ready, ara::com::mavlink::LogFn log = {},
            ara::com::mavlink::LinkSigner::NowFn now = ara::com::mavlink::SystemTimestamp10us);
  ~RadioLink() override;
  RadioLink(const RadioLink&) = delete;
  RadioLink& operator=(const RadioLink&) = delete;

  void Start();
  void Stop();

  const std::string& name() const { return cfg_.link.name; }
  RadioLinkState state() const { return state_.load(); }
  /// Ready to transmit; set BEFORE the ReadyHandler is called (consistent with notifications).
  bool IsReady() const { return ready_.load(); }

  /// REQ: fails (false) when the FIFO is full.
  bool EnqueueRequest(OutFrame m);
  /// RESP: when the FIFO is full the frame is dropped (tx_dropped) and false is returned.
  bool EnqueueResponse(OutFrame m);
  /// Event: coalesced to the latest value per key; `min_interval` = rate limit.
  void EnqueueEvent(std::uint64_t key, OutFrame m, std::chrono::milliseconds min_interval);
  /// Removes events with the given key from the queues (instance unregistered).
  void DropEvents(std::uint64_t key_mask, std::uint64_t key_value);

  RadioLinkCounters counters() const;

  // IRadioListener (driver thread)
  void OnReceive(std::span<const std::uint8_t> bytes) noexcept override;
  void OnTxReady() noexcept override;
  void OnError(ara::core::ErrorCode e, bool fatal) noexcept override;

 private:
  struct EventSlot {
    OutFrame msg;
    bool pending{false};
    Clock::time_point last_sent{};
    bool ever_sent{false};
    std::chrono::milliseconds min_interval{0};
  };
  struct Counters {
    std::atomic<std::uint64_t> rx_ok{0}, rx_crc_err{0}, rx_sig_err{0}, rx_dropped{0}, tx_busy{0},
        tx_dropped{0}, tx_expired{0}, heartbeats_rx{0}, start_errors{0}, fatal_errors{0};
  };

  void Log(ara::com::mavlink::LogLevel l, const std::string& m) const;
  void SetState(RadioLinkState s);
  void SetReady(bool ready);
  void Supervise(std::stop_token st);
  /// TX loop of one driver session; false = the driver must be restarted.
  bool TxLoop(std::stop_token st);
  void RxLoop(std::stop_token st);
  /// Builds, signs and sends; false = driver lost (restart), true = done / dropped.
  bool SendFrame(std::stop_token st, const OutFrame& m);
  /// Waits `restart_delay`; false when a stop was requested meanwhile.
  bool Pause(std::stop_token st);
  std::optional<OutFrame> PickNext(Clock::time_point now, Clock::time_point* wake);

  RadioLinkConfig cfg_;
  std::shared_ptr<ara::com::mavlink::IRadioDriver> driver_;
  CrcLookup crc_lookup_;
  FrameHandler on_frame_;
  ReadyHandler on_ready_;
  ara::com::mavlink::LogFn log_;
  ara::com::mavlink::LinkSigner::NowFn now_;
  std::unique_ptr<ara::com::mavlink::LinkSigner> signer_;

  std::atomic<RadioLinkState> state_{RadioLinkState::kStopped};
  std::atomic<bool> ready_{false};

  // TX: queues, signals from the driver
  std::mutex mutex_;
  std::condition_variable_any cv_;
  std::deque<OutFrame> fifo_;
  std::map<std::uint64_t, EventSlot> events_;
  std::deque<std::uint64_t> event_order_;
  std::uint64_t work_gen_{0};
  bool tx_ready_{false};        // OnTxReady after kBusy (under mutex_)
  bool fatal_{false};           // OnError(fatal) (under mutex_)
  std::string fatal_text_;
  std::uint8_t tx_seq_{0};

  // RX: queue of chunks from the driver
  std::mutex rx_mutex_;
  std::condition_variable_any rx_cv_;
  std::deque<std::vector<std::uint8_t>> rx_queue_;
  std::size_t rx_queued_bytes_{0};
  bool rx_reset_{false};

  Counters counters_;
  bool persistent_logged_{false};
  std::jthread supervisor_;
  std::jthread rx_thread_;
};

}  // namespace mavlink_daemon
}  // namespace srp

#endif  // PLATFORM_COMMON_MAVLINK_DAEMON_CODE_LINK_RADIO_LINK_H_
