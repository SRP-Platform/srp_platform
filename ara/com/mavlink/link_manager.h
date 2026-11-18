/**
 * Copyright (c) 2024
 *
 * @file link_manager.h
 * @brief Węzeł runtime MAVLink: łącza procesu (współdzielone przez bindingi),
 * endpointy (instancje usług provide/require), potok RX z rev. 4
 * (CRC -> podpis -> target -> instancja -> dedup -> dispatch), TX z
 * redundancją na wszystkie łącza instancji, metody (seq/timeout/status),
 * wykluczenia i limity eventów.
 */
#ifndef ARA_COM_MAVLINK_LINK_MANAGER_H_
#define ARA_COM_MAVLINK_LINK_MANAGER_H_

#include <atomic>
#include <chrono>  // NOLINT
#include <condition_variable>  // NOLINT
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <mutex>  // NOLINT
#include <set>
#include <string>
#include <thread>  // NOLINT
#include <vector>

#include "ara/com/mavlink/dedup_table.h"
#include "ara/com/mavlink/link_transport_registry.h"
#include "ara/com/mavlink/mavlink_codec.h"
#include "ara/com/mavlink/mavlink_link.h"
#include "ara/core/model/mavlink_model_item.h"
#include "ara/core/result.h"

namespace ara {
namespace com {
namespace mavlink {

class LinkManager;

struct EndpointCounterSnapshot {
  std::uint64_t tx_events{0};
  std::uint64_t tx_excluded{0};      ///< Update eventu wyłączonego (no-op)
  std::uint64_t tx_requests{0};
  std::uint64_t tx_responses{0};
  std::uint64_t rx_events{0};
  std::uint64_t rx_requests{0};      ///< REQ wykonane
  std::uint64_t rx_responses{0};
  std::uint64_t rx_rejected{0};      ///< wykluczony element / nieznana instancja
  std::uint64_t rx_duplicate{0};
  std::uint64_t call_timeouts{0};
  std::uint64_t call_unavailable{0};  ///< metoda wykluczona / brak łącza (bez nadawania)
};

/// Instancja usługi (provide = skeleton, require = proxy) na MAVLinku.
class MavlinkEndpoint {
 public:
  using MethodHandler = std::function<ara::core::Result<std::vector<std::uint8_t>>(
      std::uint16_t method_id, const std::vector<std::uint8_t>& payload)>;
  using EventHandler = std::function<void(std::uint16_t event_id,
                                          const std::vector<std::uint8_t>& payload)>;

  ~MavlinkEndpoint();

  void SetMethodHandler(MethodHandler h);  ///< skeleton: wykonanie REQ
  /// Proxy: event od skeletonu. Handlery są wołane pod `deliver_mutex_` (jeden event naraz);
  /// handler NIE MOŻE wołać DetachEndpoint/Shutdown tej instancji (zakleszczenie: Detach czeka
  /// na handler w toku) ani blokować na pracy zależnej od zakończenia Detach.
  void SetEventHandler(EventHandler h);

  /// Skeleton: wysyła event na wszystkie łącza instancji (wykluczony = no-op + licznik).
  ara::core::Result<void> PublishEvent(std::uint16_t event_id,
                                       const std::vector<std::uint8_t>& payload);
  /// Proxy: REQ na wszystkie łącza; z `out` czeka na RESP do timeout_ms, void = od razu.
  ara::core::Result<std::vector<std::uint8_t>> CallMethod(
      std::uint16_t method_id, const std::vector<std::uint8_t>& payload);

  /// Event istnieje i nie jest wykluczony (subskrypcja proxy => kSubscribe).
  bool IsEventAvailable(std::uint16_t event_id) const;
  bool AnyLinkReady() const;
  const std::string& name() const { return model_.com_id_; }
  std::uint16_t instance() const { return model_.instance_; }
  bool is_skeleton() const { return skeleton_; }
  EndpointCounterSnapshot counters() const;
  const std::vector<std::shared_ptr<MavlinkLink>>& links() const { return links_; }

 private:
  friend class LinkManager;
  struct ElementState {
    ara::core::model::ModelMavlinkElement def;
    std::shared_ptr<MessageCodec> req;
    std::shared_ptr<MessageCodec> resp;
    bool available{false};       ///< enabled && !excluded
    std::chrono::milliseconds min_interval{0};
    std::uint16_t next_seq{0};
  };
  struct Pending {
    std::mutex mutex;
    std::condition_variable cv;
    bool done{false};
    std::uint8_t status{0};
    std::vector<std::uint8_t> payload;
  };
  struct Job {
    std::size_t element{0};
    MessageHeader req_header;
    std::uint8_t sender_sys{0};
    std::uint8_t sender_comp{0};
    std::vector<std::uint8_t> payload;
    std::string arrival_link;
  };

  MavlinkEndpoint(LinkManager* manager, ara::core::model::ModelMavlink model, bool skeleton);

  ElementState* FindElement(std::uint16_t element_id);
  const ElementState* FindElement(std::uint16_t element_id) const;
  std::uint16_t NextSeq(ElementState* el);
  void EnqueueJob(Job job);
  void WorkerLoop(std::stop_token st);
  void SendResponse(const ElementState& el, const MessageHeader& req_header,
                    std::uint8_t sender_sys, std::uint8_t sender_comp, std::uint8_t status,
                    const std::vector<std::uint8_t>& someip);
  void DeliverEvent(const ElementState& el, const std::vector<std::uint8_t>& someip);
  void CompletePending(const ElementState& el, const MessageHeader& h,
                       const std::vector<std::uint8_t>& someip);

  LinkManager* manager_;
  ara::core::model::ModelMavlink model_;
  bool skeleton_;
  std::vector<ElementState> elements_;
  std::set<std::string> exclude_;
  std::vector<std::shared_ptr<MavlinkLink>> links_;
  std::mutex handlers_mutex_;
  /// Serializuje dostarczanie eventów do handlera proxy (RX z wielu łączy) i pozwala
  /// DetachEndpoint poczekać na handler w toku; po detach nic już nie jest dostarczane.
  std::mutex deliver_mutex_;
  bool detached_{false};  // pod deliver_mutex_
  MethodHandler method_handler_;
  EventHandler event_handler_;
  std::mutex seq_mutex_;
  std::mutex pending_mutex_;
  std::map<std::uint32_t, std::shared_ptr<Pending>> pending_;
  std::mutex job_mutex_;
  std::condition_variable_any job_cv_;
  std::deque<Job> jobs_;
  std::jthread worker_;
  struct Counters {
    std::atomic<std::uint64_t> tx_events{0}, tx_excluded{0}, tx_requests{0}, tx_responses{0},
        rx_events{0}, rx_requests{0}, rx_responses{0}, rx_rejected{0}, rx_duplicate{0},
        call_timeouts{0}, call_unavailable{0};
  };
  Counters counters_;
  bool logged_excluded_{false};
};

class LinkManager {
 public:
  struct Options {
    /// Rejestr transportów łączy (domyślnie: globalny).
    LinkTransportRegistry* registry{nullptr};
    /// Podmiana tworzenia transportu łącza (testy); domyślnie rejestr RegisterLinkTransport.
    std::function<MavlinkLink::TransportProvider(const ara::core::model::ModelMavlinkLink&)>
        transport_factory;
    LinkSigner::NowFn now{SystemTimestamp10us};
    LogFn log;
    /// Skrócenie czasów łącza w testach.
    std::chrono::milliseconds retry_interval{30000};
    std::chrono::milliseconds transport_warn_after{5000};
    std::uint32_t read_timeout_ms{100};
    std::uint32_t write_timeout_ms{500};
    /// TYLKO TESTY: wymusza początkowy `seq` każdego elementu (zamiast losowego, R2), by móc
    /// odtworzyć kolizję seq+payload po restarcie nadawcy. Produkcja: nie ustawiać.
    std::optional<std::uint16_t> test_initial_seq;
  };

  LinkManager();
  explicit LinkManager(Options opts);
  std::optional<std::uint16_t> test_initial_seq() const { return opts_.test_initial_seq; }
  ~LinkManager();
  LinkManager(const LinkManager&) = delete;
  LinkManager& operator=(const LinkManager&) = delete;

  /// Manager procesu (używany przez bindingi); log przez ara::log, gdy zainicjowany.
  static LinkManager& Instance();

  /// Rejestruje instancję usługi i startuje jej łącza (współdzielone po nazwie).
  std::shared_ptr<MavlinkEndpoint> AttachEndpoint(const ara::core::model::ModelMavlink& model,
                                                  bool skeleton);
  void DetachEndpoint(const std::shared_ptr<MavlinkEndpoint>& ep);
  /// Zatrzymuje wszystkie łącza i endpointy (Deinitialize).
  void Shutdown();

  std::shared_ptr<MavlinkLink> FindLink(const std::string& name) const;
  std::uint64_t duplicates() const { return duplicates_.load(); }
  std::uint64_t unknown_messages() const { return unknown_.load(); }

 private:
  friend class MavlinkEndpoint;
  struct MsgInfo {
    std::shared_ptr<MessageCodec> codec;
    enum Kind { kEvent, kReq, kResp } kind{kEvent};
    std::uint16_t element_id{0};
    std::string service_key;
  };
  struct LinkEntry {
    std::shared_ptr<MavlinkLink> link;
    int refs{0};
  };

  std::shared_ptr<MavlinkLink> AcquireLink(const ara::core::model::ModelMavlinkLink& cfg,
                                           std::uint8_t sys, std::uint8_t comp);
  void ReleaseLink(const std::string& name);
  void RegisterMessages(const MavlinkEndpoint& ep);
  void OnFrame(const std::string& link, const Frame& f);
  std::optional<std::uint8_t> CrcLookup(std::uint32_t msgid) const;
  MavlinkLink::TransportProvider MakeProvider(const ara::core::model::ModelMavlinkLink& l);
  void Log(LogLevel l, const std::string& m) const;

  Options opts_;
  LinkTransportRegistry* registry_;
  mutable std::mutex mutex_;
  std::map<std::string, LinkEntry> links_;
  std::vector<std::shared_ptr<MavlinkEndpoint>> endpoints_;
  std::map<std::uint32_t, MsgInfo> messages_;
  DedupTable dedup_;
  std::atomic<std::uint64_t> duplicates_{0};
  std::atomic<std::uint64_t> unknown_{0};
};

}  // namespace mavlink
}  // namespace com
}  // namespace ara

#endif  // ARA_COM_MAVLINK_LINK_MANAGER_H_
