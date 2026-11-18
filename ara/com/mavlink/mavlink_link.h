/**
 * Copyright (c) 2024
 *
 * @file mavlink_link.h
 * @brief Jedno łącze MAVLink: wątek nadzorczy + RX (wątek łącza) + TX, stany
 * Open/kOpenError/retry, HEARTBEAT, podpis v2, kolejki wg rev. 4/9 + R4
 * i liczniki. Bajty dostarcza IByteTransport (RegisterLinkTransport).
 */
#ifndef ARA_COM_MAVLINK_MAVLINK_LINK_H_
#define ARA_COM_MAVLINK_MAVLINK_LINK_H_

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
#include <stop_token>  // NOLINT
#include <string>
#include <thread>  // NOLINT
#include <vector>

#include "ara/com/mavlink/i_byte_transport.h"
#include "ara/com/mavlink/mavlink_frame.h"
#include "ara/com/mavlink/mavlink_signing.h"
#include "ara/core/result.h"

namespace ara {
namespace com {
namespace mavlink {

enum class LogLevel { kDebug, kInfo, kWarn, kError };
using LogFn = std::function<void(LogLevel, const std::string&)>;

struct LinkConfig {
  std::string name;
  std::uint8_t version{2};          ///< wersja nadawania (odbiór przyjmuje obie)
  double heartbeat_hz{1.0};         ///< 0 = bez HEARTBEAT
  bool signing_enabled{false};
  std::string key_file;
  std::uint8_t signing_link_id{0};
  bool accept_unsigned{false};
  std::string state_file;           ///< persist znacznika czasu podpisu
  std::uint8_t system_id{1};
  std::uint8_t component_id{1};
  // czasy (testy skracają)
  std::chrono::milliseconds retry_interval{30000};
  std::chrono::milliseconds transport_warn_after{5000};
  std::uint32_t read_timeout_ms{100};
  std::uint32_t write_timeout_ms{500};
  std::size_t fifo_limit{32};       ///< R4: REQ/RESP FIFO na łącze
};

enum class LinkState {
  kStopped,
  kWaitingForTransport,  ///< brak rejestracji transportu
  kOpening,
  kRunning,
  kOpenError,            ///< Open()/Read/Write nie powiodły się; ponowienie po retry_interval
  kInitError             ///< np. zły klucz podpisu: łącze nie nadaje
};

struct LinkCounterSnapshot {
  std::uint64_t rx_ok{0};
  std::uint64_t rx_crc_err{0};
  std::uint64_t rx_sig_err{0};
  std::uint64_t rx_dropped{0};     ///< nieznany msgid / flagi / śmieci
  std::uint64_t tx{0};
  std::uint64_t tx_bytes{0};
  std::uint64_t tx_partial{0};     ///< R5: porzucona reszta ramki po timeout
  std::uint64_t tx_dropped{0};     ///< pełna kolejka RESP / ramka nie wystartowała
  std::uint64_t heartbeats_tx{0};
  std::uint64_t heartbeats_rx{0};
  std::uint64_t opens{0};
  std::uint64_t open_errors{0};
};

/// Wiadomość gotowa do nadania (pełny payload, bez obcinania zer).
struct OutMessage {
  std::uint32_t msg_id{0};
  std::uint8_t crc_extra{0};
  std::vector<std::uint8_t> payload;
};

class MavlinkLink {
 public:
  /// Zwraca transport (czeka do `wait`), nullptr = jeszcze niedostępny.
  using TransportProvider = std::function<std::shared_ptr<IByteTransport>(
      std::chrono::milliseconds wait, std::stop_token token)>;
  /// Wołane w wątku RX po CRC i podpisie (kolejne filtry robi LinkManager).
  using FrameHandler = std::function<void(const std::string& link, const Frame&)>;

  MavlinkLink(LinkConfig cfg, TransportProvider provider, FrameParser::CrcExtraLookup crc_lookup,
              FrameHandler handler, LogFn log = {},
              LinkSigner::NowFn now = SystemTimestamp10us);
  ~MavlinkLink();
  MavlinkLink(const MavlinkLink&) = delete;
  MavlinkLink& operator=(const MavlinkLink&) = delete;

  void Start();
  void Stop();

  const LinkConfig& config() const { return cfg_; }
  LinkState state() const { return state_.load(); }
  /// Gotowe do nadawania (transport otwarty). Metoda bez gotowego łącza = natychmiast błąd.
  bool IsReady() const { return state_.load() == LinkState::kRunning; }
  bool WaitForState(LinkState s, std::chrono::milliseconds timeout);

  /// REQ: błąd (ComErrc), gdy FIFO pełne (R4).
  ara::core::Result<void> EnqueueRequest(OutMessage m);
  /// RESP: przy pełnym FIFO drop + tx_dropped, zwraca false.
  bool EnqueueResponse(OutMessage m);
  /// Event: koalescencja do najnowszej wartości per klucz; `min_interval` = limit częstotliwości.
  void EnqueueEvent(std::uint64_t key, OutMessage m, std::chrono::milliseconds min_interval);

  LinkCounterSnapshot counters() const;
  LinkSigner* signer() { return signer_.get(); }

 private:
  struct EventSlot {
    OutMessage msg;
    bool pending{false};
    std::chrono::steady_clock::time_point last_sent{};
    bool ever_sent{false};
    std::chrono::milliseconds min_interval{0};
  };
  struct Counters {
    std::atomic<std::uint64_t> rx_ok{0}, rx_crc_err{0}, rx_sig_err{0}, rx_dropped{0}, tx{0},
        tx_bytes{0}, tx_partial{0}, tx_dropped{0}, heartbeats_tx{0}, heartbeats_rx{0}, opens{0},
        open_errors{0};
  };

  void Log(LogLevel l, const std::string& m) const;
  void SetState(LinkState s);
  void Supervise(std::stop_token st);
  /// Jedna sesja na otwartym transporcie; zwraca po błędzie lub stop.
  void RunSession(const std::shared_ptr<IByteTransport>& tr, std::stop_token st);
  void TxLoop(const std::shared_ptr<IByteTransport>& tr, std::stop_token st,
              std::atomic<bool>* failed, std::stop_source* session_stop);
  void RxLoop(const std::shared_ptr<IByteTransport>& tr, std::stop_token st,
              std::atomic<bool>* failed, std::stop_source* session_stop);
  bool SendFrame(IByteTransport* tr, std::stop_token st, const OutMessage& m, bool heartbeat);
  std::optional<OutMessage> PickNext(std::chrono::steady_clock::time_point now,
                                     std::chrono::steady_clock::time_point* wake);
  bool SleepInterruptible(std::chrono::milliseconds d, std::stop_token st);

  LinkConfig cfg_;
  TransportProvider provider_;
  FrameParser::CrcExtraLookup crc_lookup_;
  FrameHandler handler_;
  LogFn log_;
  LinkSigner::NowFn now_;
  std::unique_ptr<LinkSigner> signer_;

  std::atomic<LinkState> state_{LinkState::kStopped};
  mutable std::mutex state_mutex_;
  std::condition_variable state_cv_;

  std::mutex mutex_;                 // kolejki TX
  std::condition_variable_any cv_;
  std::deque<OutMessage> fifo_;
  std::map<std::uint64_t, EventSlot> events_;
  std::deque<std::uint64_t> event_order_;
  std::uint64_t work_gen_{0};

  std::uint8_t tx_seq_{0};
  Counters counters_;
  std::jthread supervisor_;
  std::mutex err_mutex_;
  std::string last_error_;
  bool last_error_persistent_{false};
  bool last_error_closed_{false};
  bool persistent_logged_{false};
  void RecordError(const ara::core::ErrorCode& e);
};

}  // namespace mavlink
}  // namespace com
}  // namespace ara

#endif  // ARA_COM_MAVLINK_MAVLINK_LINK_H_
