/**
 * Copyright (c) 2024
 *
 * @file mavlink_endpoint.h
 * @brief A service instance (provide = skeleton, require = proxy) on `on: mavlink` on the
 * application side. A thin client of the MavlinkDaemon: it encodes/decodes the payload (SOME/IP
 * BE <-> MAVLink LE, `MessageCodec`), while links, queues, signing, dedup and event rate limits
 * belong to the daemon. Registration and the daemon connection are handled by `DaemonClient`.
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
  std::uint64_t tx_excluded{0};       ///< Update of an excluded event (no-op)
  std::uint64_t tx_dropped{0};        ///< event/RESP not sent: no daemon or no registration
  std::uint64_t tx_requests{0};
  std::uint64_t tx_responses{0};
  std::uint64_t rx_events{0};
  std::uint64_t rx_requests{0};       ///< REQ executed
  std::uint64_t rx_responses{0};
  std::uint64_t rx_dropped{0};        ///< received but rejected (full queue, bad payload)
  std::uint64_t call_timeouts{0};
  std::uint64_t call_unavailable{0};  ///< method excluded / no daemon / no link
  std::uint64_t call_rejected{0};     ///< the daemon rejected a REQ (SendError)
};

class MavlinkEndpoint {
 public:
  using MethodHandler = std::function<ara::core::Result<std::vector<std::uint8_t>>(
      std::uint16_t method_id, const std::vector<std::uint8_t>& payload)>;
  using EventHandler = std::function<void(std::uint16_t event_id,
                                          const std::vector<std::uint8_t>& payload)>;

  ~MavlinkEndpoint();

  /// Handlers are fixed when the endpoint is attached (`DaemonClient::AttachEndpoint`), so they
  /// need no lock. They are called from the instance thread (one message at a time); a handler may
  /// call CallMethod but MUST NOT call DetachEndpoint of its own instance.
  struct Handlers {
    MethodHandler method;  ///< skeleton: executes a REQ
    EventHandler event;    ///< proxy: an event from the skeleton
  };

  /// Skeleton: sends an event to the daemon (excluded = no-op + counter; no daemon = drop + counter).
  ara::core::Result<void> PublishEvent(std::uint16_t event_id,
                                       const std::vector<std::uint8_t>& payload);
  /// Proxy: a REQ to the daemon; with a RESP it waits up to timeout_ms, without it returns at
  /// once. No daemon, registration or ready link = an immediate error.
  ara::core::Result<std::vector<std::uint8_t>> CallMethod(std::uint16_t method_id,
                                                         const std::vector<std::uint8_t>& payload);

  /// The event exists and is not excluded (a proxy subscription => kSubscribe).
  bool IsEventAvailable(std::uint16_t event_id) const;
  /// The daemon accepted the registration of this instance (on the current connection).
  bool IsRegistered() const { return registered_.load(); }
  /// Registered and at least one link of the instance is ready.
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
    std::atomic<std::uint16_t> next_seq{0};
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
                  bool skeleton, Handlers handlers);

  /// Instance description for the daemon; nullopt = an instance without MAVLink elements (not
  /// registered).
  std::optional<daemon::RegisterInstance> BuildRegistration() const;

  // calls from DaemonClient (IO thread)
  void OnRegistered();
  void OnDisconnected();
  void OnLinkStatus(const std::string& link, bool ready);
  void OnDelivery(daemon::Delivery d);
  void OnResponse(daemon::Delivery d);
  void OnSendError(const daemon::SendError& e);

  ElementState* FindElement(std::uint16_t element_id);
  const ElementState* FindElement(std::uint16_t element_id) const;
  static std::uint16_t NextSeq(ElementState* el) { return el->next_seq.fetch_add(1); }
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

  const Handlers handlers_;
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
};

}  // namespace mavlink
}  // namespace com
}  // namespace ara

#endif  // ARA_COM_MAVLINK_CLIENT_MAVLINK_ENDPOINT_H_
