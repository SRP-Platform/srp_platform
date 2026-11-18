/**
 * Copyright (c) 2024
 *
 * @file radio_link.h
 * @brief Łącze MavlinkDaemona: jeden driver radia (IRadioDriver) + kolejki TX, HEARTBEAT, podpis
 * v2, parser RX i liczniki.
 *
 * Wątki łącza:
 *  - nadzorczy: Start() drivera (błąd => ponowienie po `restart_delay`), pętla TX (HEARTBEAT ->
 *    REQ/RESP FIFO -> eventy z koalescencją i limitem), Stop() drivera po błędzie fatalnym i
 *    ponowny Start() po `restart_delay` (30 s);
 *  - RX: kopiuje kawałki z OnReceive (wątek drivera) do kolejki i parsuje ramki (CRC, podpis).
 *
 * TX: ramka jest budowana i podpisywana tuż przed Send(). `kBusy` wstrzymuje DOKŁADNIE tę ramkę
 * do OnTxReady() (kolejność nadawania = kolejność znaczników czasu podpisu; nic nie wyprzedza
 * wstrzymanej ramki, nic nie jest zapisywane częściowo ani nadawane dwa razy).
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

namespace srp {
namespace mavlink_daemon {

struct RadioLinkConfig {
  std::string name;
  std::uint8_t version{2};     ///< wersja nadawania (odbiór przyjmuje obie)
  double heartbeat_hz{1.0};    ///< 0 = bez HEARTBEAT
  bool signing_enabled{false};
  std::string key_file;
  std::uint8_t signing_link_id{0};
  bool accept_unsigned{false};
  std::string state_file;      ///< persist znacznika czasu podpisu
  std::uint8_t system_id{1};
  std::uint8_t component_id{1};
  /// Przerwa przed ponownym Start() drivera po błędzie (fatal albo Start() nie powiódł się).
  std::chrono::milliseconds restart_delay{30000};
  /// Po tylu ms ciągłego kBusy w logu pojawia się Warn (raz na wstrzymanie).
  std::chrono::milliseconds busy_warn_after{5000};
  std::size_t fifo_limit{32};  ///< REQ/RESP FIFO na łącze
};

enum class RadioLinkState {
  kStopped,
  kStarting,      ///< Start() drivera w toku
  kRunning,
  kRestartWait,   ///< błąd; ponowny Start() po restart_delay
  kInitError      ///< np. zły klucz podpisu: łącze nie nadaje
};

struct RadioLinkCounters {
  std::uint64_t rx_ok{0};
  std::uint64_t rx_crc_err{0};
  std::uint64_t rx_sig_err{0};
  std::uint64_t rx_dropped{0};     ///< nieznany msgid / flagi / przepełnienie bufora RX
  std::uint64_t tx{0};
  std::uint64_t tx_bytes{0};
  std::uint64_t tx_busy{0};        ///< ile razy Send() zwróciło kBusy
  std::uint64_t tx_dropped{0};     ///< pełne FIFO RESP / błąd przejściowy drivera / ramka wstrzymana przy restarcie
  std::uint64_t tx_expired{0};     ///< REQ, które w FIFO przekroczyły timeout metody
  std::uint64_t heartbeats_tx{0};
  std::uint64_t heartbeats_rx{0};
  std::uint64_t starts{0};
  std::uint64_t start_errors{0};
  std::uint64_t fatal_errors{0};
};

/// Wiadomość gotowa do nadania (pełny payload, bez obcinania zer).
struct OutFrame {
  std::uint32_t msg_id{0};
  std::uint8_t crc_extra{0};
  std::vector<std::uint8_t> payload;
  /// REQ czekający w FIFO po tym czasie jest porzucany (odpowiedź i tak byłaby spóźniona).
  std::chrono::steady_clock::time_point deadline{std::chrono::steady_clock::time_point::max()};
};

class RadioLink final : public ara::com::mavlink::IRadioListener {
 public:
  using Clock = std::chrono::steady_clock;
  using CrcLookup = ara::com::mavlink::FrameParser::CrcExtraLookup;
  /// Wołane w wątku RX po CRC i podpisie (kolejne filtry robi rdzeń daemona).
  using FrameHandler = std::function<void(const std::string& link, const ara::com::mavlink::Frame&)>;
  /// Zmiana gotowości łącza (true = można nadawać). Wołane z wątku nadzorczego.
  using ReadyHandler = std::function<void(const std::string& link, bool ready)>;

  RadioLink(RadioLinkConfig cfg, std::shared_ptr<ara::com::mavlink::IRadioDriver> driver, CrcLookup crc_lookup,
            FrameHandler on_frame, ReadyHandler on_ready, ara::com::mavlink::LogFn log = {},
            ara::com::mavlink::LinkSigner::NowFn now = ara::com::mavlink::SystemTimestamp10us);
  ~RadioLink() override;
  RadioLink(const RadioLink&) = delete;
  RadioLink& operator=(const RadioLink&) = delete;

  void Start();
  void Stop();

  const RadioLinkConfig& config() const { return cfg_; }
  RadioLinkState state() const { return state_.load(); }
  /// Gotowe do nadawania; ustawiane PRZED wywołaniem ReadyHandler (spójne z powiadomieniami).
  bool IsReady() const { return ready_.load(); }
  bool WaitForState(RadioLinkState s, std::chrono::milliseconds timeout);
  /// Ile razy łącze weszło w kRunning (testy restartu).
  std::uint64_t running_count() const { return running_count_.load(); }

  /// REQ: błąd (false), gdy FIFO pełne.
  bool EnqueueRequest(OutFrame m);
  /// RESP: przy pełnym FIFO drop + tx_dropped, zwraca false.
  bool EnqueueResponse(OutFrame m);
  /// Event: koalescencja do najnowszej wartości per klucz; `min_interval` = limit częstotliwości.
  void EnqueueEvent(std::uint64_t key, OutFrame m, std::chrono::milliseconds min_interval);
  /// Usuwa z kolejek eventy o danym kluczu (wyrejestrowanie instancji).
  void DropEvents(std::uint64_t key_mask, std::uint64_t key_value);

  RadioLinkCounters counters() const;
  ara::com::mavlink::LinkSigner* signer() { return signer_.get(); }

  // IRadioListener (wątek drivera)
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
    std::atomic<std::uint64_t> rx_ok{0}, rx_crc_err{0}, rx_sig_err{0}, rx_dropped{0}, tx{0}, tx_bytes{0},
        tx_busy{0}, tx_dropped{0}, tx_expired{0}, heartbeats_tx{0}, heartbeats_rx{0}, starts{0},
        start_errors{0}, fatal_errors{0};
  };

  void Log(ara::com::mavlink::LogLevel l, const std::string& m) const;
  void SetState(RadioLinkState s);
  void SetReady(bool ready);
  void Supervise(std::stop_token st);
  /// Pętla TX jednej sesji drivera; false = trzeba zrestartować driver.
  bool TxLoop(std::stop_token st);
  void RxLoop(std::stop_token st);
  /// Buduje, podpisuje i wysyła; false = utrata drivera (restart), true = zrobione/porzucone.
  bool SendFrame(std::stop_token st, const OutFrame& m, bool heartbeat);
  std::optional<OutFrame> PickNext(Clock::time_point now, Clock::time_point* wake);
  bool SleepInterruptible(std::chrono::milliseconds d, std::stop_token st);

  RadioLinkConfig cfg_;
  std::shared_ptr<ara::com::mavlink::IRadioDriver> driver_;
  CrcLookup crc_lookup_;
  FrameHandler on_frame_;
  ReadyHandler on_ready_;
  ara::com::mavlink::LogFn log_;
  ara::com::mavlink::LinkSigner::NowFn now_;
  std::unique_ptr<ara::com::mavlink::LinkSigner> signer_;

  std::atomic<RadioLinkState> state_{RadioLinkState::kStopped};
  std::atomic<std::uint64_t> running_count_{0};
  std::atomic<bool> ready_{false};
  mutable std::mutex state_mutex_;
  std::condition_variable state_cv_;

  // TX: kolejki, sygnały od drivera
  std::mutex mutex_;
  std::condition_variable_any cv_;
  std::deque<OutFrame> fifo_;
  std::map<std::uint64_t, EventSlot> events_;
  std::deque<std::uint64_t> event_order_;
  std::uint64_t work_gen_{0};
  bool tx_ready_{false};        // OnTxReady po kBusy (pod mutex_)
  bool fatal_{false};           // OnError(fatal) (pod mutex_)
  std::string fatal_text_;
  std::uint8_t tx_seq_{0};

  // RX: kolejka kawałków od drivera
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
