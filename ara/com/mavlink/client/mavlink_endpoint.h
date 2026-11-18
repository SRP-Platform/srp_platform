/**
 * Copyright (c) 2024
 *
 * @file mavlink_endpoint.h
 * @brief Instancja usługi (provide = skeleton, require = proxy) na `on: mavlink` po stronie
 * aplikacji. Cienki klient MavlinkDaemon: koduje/dekoduje payload (SOME/IP BE <-> MAVLink LE,
 * `MessageCodec`), a łącza, kolejki, podpis, dedup i limity eventów należą do daemona.
 * Rejestracją i połączeniem z daemonem zarządza `DaemonClient`.
 */
#ifndef ARA_COM_MAVLINK_CLIENT_MAVLINK_ENDPOINT_H_
#define ARA_COM_MAVLINK_CLIENT_MAVLINK_ENDPOINT_H_

#include <atomic>
#include <condition_variable>  // NOLINT
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>  // NOLINT
#include <optional>
#include <set>
#include <string>
#include <thread>  // NOLINT
#include <vector>

#include "ara/com/mavlink/daemon_protocol.h"
#include "ara/com/mavlink/mavlink_codec.h"
#include "ara/core/model/mavlink_model_item.h"
#include "ara/core/result.h"

namespace ara {
namespace com {
namespace mavlink {

class DaemonClient;

struct EndpointCounterSnapshot {
  std::uint64_t tx_events{0};
  std::uint64_t tx_excluded{0};       ///< Update eventu wyłączonego (no-op)
  std::uint64_t tx_dropped{0};        ///< event/RESP nie wysłany: brak daemona albo rejestracji
  std::uint64_t tx_requests{0};
  std::uint64_t tx_responses{0};
  std::uint64_t rx_events{0};
  std::uint64_t rx_requests{0};       ///< REQ wykonane
  std::uint64_t rx_responses{0};
  std::uint64_t rx_dropped{0};        ///< odebrane, ale odrzucone (przepełniona kolejka, zły payload)
  std::uint64_t call_timeouts{0};
  std::uint64_t call_unavailable{0};  ///< metoda wykluczona / brak daemona / brak łącza
  std::uint64_t call_rejected{0};     ///< daemon odrzucił REQ (SendError)
};

class MavlinkEndpoint {
 public:
  using MethodHandler = std::function<ara::core::Result<std::vector<std::uint8_t>>(
      std::uint16_t method_id, const std::vector<std::uint8_t>& payload)>;
  using EventHandler = std::function<void(std::uint16_t event_id,
                                          const std::vector<std::uint8_t>& payload)>;

  ~MavlinkEndpoint();

  void SetMethodHandler(MethodHandler h);  ///< skeleton: wykonanie REQ
  /// Proxy: event od skeletonu. Handlery są wołane z wątku instancji (jeden komunikat naraz);
  /// handler może wołać CallMethod, ale NIE MOŻE wołać DetachEndpoint tej instancji.
  void SetEventHandler(EventHandler h);

  /// Skeleton: wysyła event do daemona (wykluczony = no-op + licznik; brak daemona = drop + licznik).
  ara::core::Result<void> PublishEvent(std::uint16_t event_id,
                                       const std::vector<std::uint8_t>& payload);
  /// Proxy: REQ do daemona; z RESP czeka do timeout_ms, bez RESP wraca od razu. Brak daemona,
  /// rejestracji albo gotowego łącza = natychmiastowy błąd.
  ara::core::Result<std::vector<std::uint8_t>> CallMethod(std::uint16_t method_id,
                                                         const std::vector<std::uint8_t>& payload);

  /// Event istnieje i nie jest wykluczony (subskrypcja proxy => kSubscribe).
  bool IsEventAvailable(std::uint16_t event_id) const;
  /// Daemon przyjął rejestrację tej instancji (w bieżącym połączeniu).
  bool IsRegistered() const { return registered_.load(); }
  /// Zarejestrowana i przynajmniej jedno łącze instancji gotowe.
  bool AnyLinkReady() const;
  const std::string& name() const { return model_.com_id_; }
  std::uint16_t instance() const { return model_.instance_; }
  bool is_skeleton() const { return skeleton_; }
  EndpointCounterSnapshot counters() const;

 private:
  friend class DaemonClient;
  struct ElementState {
    ara::core::model::ModelMavlinkElement def;
    std::shared_ptr<MessageCodec> req;
    std::shared_ptr<MessageCodec> resp;
    bool available{false};  ///< enabled && !excluded
    std::uint32_t min_interval_ms{0};
    std::uint16_t next_seq{0};
  };
  struct Pending {
    std::mutex mutex;
    std::condition_variable cv;
    bool done{false};
    std::optional<ara::core::ErrorCode> error;
    std::uint8_t status{0};
    std::vector<std::uint8_t> payload;
  };
  struct Job {
    bool request{false};
    std::size_t element{0};
    daemon::Delivery in;
  };

  MavlinkEndpoint(DaemonClient* client, std::uint32_t handle, ara::core::model::ModelMavlink model,
                  bool skeleton);

  /// Opis instancji dla daemona; nullopt = instancja bez elementów na MAVLinku (nie rejestrujemy).
  std::optional<daemon::RegisterInstance> BuildRegistration() const;

  // wywołania DaemonClient (wątek IO)
  void OnRegistered();
  void OnDisconnected();
  void OnLinkStatus(const std::string& link, bool ready);
  void OnDelivery(bool request, daemon::Delivery d);
  void OnResponse(daemon::Delivery d);
  void OnSendError(const daemon::SendError& e);

  ElementState* FindElement(std::uint16_t element_id);
  const ElementState* FindElement(std::uint16_t element_id) const;
  std::uint16_t NextSeq(ElementState* el);
  void WorkerLoop(std::stop_token st);
  void HandleEvent(const ElementState& el, const daemon::Delivery& d);
  void HandleRequest(const ElementState& el, const daemon::Delivery& d);
  void FailAllPending(const ara::core::ErrorCode& e);
  void StopWorker();

  DaemonClient* client_;
  std::uint32_t handle_;
  ara::core::model::ModelMavlink model_;
  bool skeleton_;
  std::vector<ElementState> elements_;
  std::set<std::string> exclude_;

  std::atomic<bool> registered_{false};
  mutable std::mutex state_mutex_;
  std::set<std::string> ready_links_;

  std::mutex handlers_mutex_;
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
    std::atomic<std::uint64_t> tx_events{0}, tx_excluded{0}, tx_dropped{0}, tx_requests{0},
        tx_responses{0}, rx_events{0}, rx_requests{0}, rx_responses{0}, rx_dropped{0},
        call_timeouts{0}, call_unavailable{0}, call_rejected{0};
  };
  Counters counters_;
  bool logged_excluded_{false};
};

}  // namespace mavlink
}  // namespace com
}  // namespace ara

#endif  // ARA_COM_MAVLINK_CLIENT_MAVLINK_ENDPOINT_H_
