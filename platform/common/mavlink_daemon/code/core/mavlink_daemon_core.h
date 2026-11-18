/**
 * Copyright (c) 2024
 *
 * @file mavlink_daemon_core.h
 * @brief Core of the MavlinkDaemon (without ara::exec): links built from radio drivers, the IPC
 * server for applications, the instance and message registry, RX routing (CRC -> signature ->
 * instance link -> target/source -> delivery) and TX (event/REQ/RESP -> link queues with
 * redundancy).
 *
 * Applications connect over a socket (daemon_protocol.h), register `on: mavlink` instances and
 * exchange FULL MAVLink payloads; the daemon does not know SOME/IP models. An instance conflict
 * (com_id, instance, role) or a msg_id conflict (different definition / different service) is a
 * rejected registration with the reason in the RegisterReply. Disconnecting an application cleans
 * up its registrations (msg_ids return to the pool, queued events disappear). A daemon restart
 * makes applications reconnect and register again.
 *
 * Everything the daemon sends to an application goes through a per-session queue drained by a
 * writer thread of that session: a slow application can delay only its own messages, never the
 * RX thread of a link or the supervisor thread. An application that stops reading is
 * disconnected.
 *
 * The crc_extra table for the link parsers is built from the registrations (messages of the
 * registered instances).
 */
#ifndef PLATFORM_COMMON_MAVLINK_DAEMON_CODE_CORE_MAVLINK_DAEMON_CORE_H_
#define PLATFORM_COMMON_MAVLINK_DAEMON_CODE_CORE_MAVLINK_DAEMON_CORE_H_

#include <atomic>
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

#include "ara/com/mavlink/daemon_ipc.h"
#include "ara/com/mavlink/daemon_protocol.h"
#include "ara/com/mavlink/mavlink_log.h"
#include "ara/com/mavlink/mavlink_signing.h"
#include "ara/com/mavlink/radio_driver_registry.h"
#include "ara/core/result.h"
#include "platform/common/mavlink_daemon/code/config/daemon_config.h"
#include "platform/common/mavlink_daemon/code/link/radio_link.h"

namespace srp {
namespace mavlink_daemon {

struct DaemonOptions {
  ara::com::mavlink::LogFn log;
  /// Driver registry; nullptr = the global one filled by SRP_MAVLINK_REGISTER_DRIVER (tests pass
  /// their own).
  ara::com::mavlink::RadioDriverRegistry* registry{nullptr};
  ara::com::mavlink::LinkSigner::NowFn now{ara::com::mavlink::SystemTimestamp10us};
  /// Called from the statistics thread once per link every `stats_period_ms`, the links one after another,
  /// `stats_period_ms / links` apart (the platform
  /// service MavlinkLinkService publishes its `LinkStats` event from here). Must not block.
  std::function<void(const ara::com::mavlink::daemon::LinkStats&)> on_link_stats;
};

struct DaemonCounters {
  std::uint64_t deliver_failed{0};
  std::uint64_t register_rejected{0};
  std::uint64_t send_rejected{0};
};

class MavlinkDaemonCore {
 public:
  explicit MavlinkDaemonCore(DaemonConfig cfg, DaemonOptions opts = {});
  ~MavlinkDaemonCore();
  MavlinkDaemonCore(const MavlinkDaemonCore&) = delete;
  MavlinkDaemonCore& operator=(const MavlinkDaemonCore&) = delete;

  /// Checks the driver registrations, creates drivers and links, opens the socket, starts the threads. A start
  /// failure is one message listing every problem (each also in the log); nothing is started.
  /// Start() after Stop() starts the daemon again.
  ara::core::Result<void> Start();
  void Stop();

  std::shared_ptr<RadioLink> FindLink(const std::string& name) const;
  DaemonCounters counters() const;
  /// Radio measurements + counters of one link (nullopt = unknown link) / of all links in config order.
  /// `link_index` = position in the daemon config (the order of the links in the file), `link_name` is
  /// cut to 16 characters (the service field).
  std::optional<ara::com::mavlink::daemon::LinkStats> GetLinkStats(const std::string& link) const;
  std::vector<ara::com::mavlink::daemon::LinkStats> AllLinkStats() const;
  const std::string& socket_path() const { return cfg_.socket_path; }

 private:
  friend class MavlinkDaemonCoreTestAccess;

  struct MsgInfo {
    ara::com::mavlink::daemon::MessageDesc desc;
    ara::com::mavlink::daemon::ElementKind kind{ara::com::mavlink::daemon::ElementKind::kEvent};
    std::string com_id;
    std::uint16_t element_id{0};
    int refs{0};
  };
  struct Session;
  /// A link of an element with its event rate limit (resolved at registration).
  struct ElementLink {
    std::shared_ptr<RadioLink> link;
    std::chrono::milliseconds min_interval{0};
  };
  struct Instance {
    std::uint64_t id{0};
    std::weak_ptr<Session> session;
    std::string session_label;
    ara::com::mavlink::daemon::RegisterInstance reg;
    std::vector<std::shared_ptr<RadioLink>> links;  ///< the sum of the links of the elements
    /// Per element (same index as `reg.elements`): the links it goes over. TX uses only them
    /// and a frame of the element is accepted only from them.
    std::vector<std::vector<ElementLink>> element_links;
    std::map<std::uint16_t, std::size_t> element_index;
    std::atomic<bool> active{false};  ///< set after the RegisterReply is queued (nothing is delivered before)
    std::mutex status_mutex;          ///< serialises LinkStatus (initial state vs changes)
  };
  struct Session {
    std::uint64_t id{0};
    ara::com::mavlink::daemon::MessageSocket socket;
    std::map<std::uint32_t, std::shared_ptr<Instance>> by_handle;  // under the core mutex_
    std::thread thread;  // joined by the core (never from its own thread)
    std::atomic<bool> done{false};
    std::atomic<bool> stats_subscribed{false};  ///< SubscribeLinkStats received
    // Outgoing queue drained by `writer`.
    std::mutex out_mutex;
    std::condition_variable_any out_cv;
    std::deque<ara::com::mavlink::daemon::Message> out;
    std::atomic<bool> writer_dead{false};
    std::jthread writer;
    std::string label() const { return "app#" + std::to_string(id); }
  };

  void AcceptLoop(std::stop_token st);
  void ReapSessions();
  void SessionLoop(const std::shared_ptr<Session>& s);
  void WriterLoop(const std::shared_ptr<Session>& s, std::stop_token st);
  /// Queues a message for the application; never blocks. false = not queued (the session is
  /// dead or its queue overflowed, in which case the session is disconnected).
  bool Enqueue(const std::shared_ptr<Session>& s, ara::com::mavlink::daemon::Message m);
  void HandleRegister(const std::shared_ptr<Session>& s,
                      const ara::com::mavlink::daemon::RegisterInstance& r);
  void HandleUnregister(const std::shared_ptr<Session>& s, std::uint32_t handle);
  void HandleEvent(const std::shared_ptr<Session>& s, const ara::com::mavlink::daemon::ElementPayload& m);
  void HandleRequest(const std::shared_ptr<Session>& s, const ara::com::mavlink::daemon::ElementPayload& m);
  void HandleResponse(const std::shared_ptr<Session>& s, const ara::com::mavlink::daemon::ElementPayload& m);
  void StatsLoop(std::stop_token st);
  void HandleGetLinkStats(const std::shared_ptr<Session>& s, const ara::com::mavlink::daemon::GetLinkStats& g);
  void CleanupSession(const std::shared_ptr<Session>& s);
  void RemoveInstanceLocked(const std::shared_ptr<Instance>& inst);

  std::shared_ptr<Instance> FindInstance(const std::shared_ptr<Session>& s, std::uint32_t handle) const;
  /// Empty text = OK; otherwise the reason of the msg_id conflict.
  std::string CheckMsgConflictsLocked(const ara::com::mavlink::daemon::RegisterInstance& r) const;
  void AddMessagesLocked(const ara::com::mavlink::daemon::RegisterInstance& r);
  std::optional<std::uint8_t> CrcLookup(std::uint32_t msg_id) const;
  /// RX of a frame that passed CRC and signature. true = the frame was accepted for an
  /// instance (it passed every filter below); false = it was ignored or rejected.
  bool OnFrame(const std::string& link_name, const ara::com::mavlink::Frame& f);
  void OnLinkReady(const std::string& link_name, bool ready);
  bool Deliver(const std::shared_ptr<Instance>& inst, ara::com::mavlink::daemon::Message m);
  void Log(ara::com::mavlink::LogLevel l, const std::string& m) const;

  DaemonConfig cfg_;
  DaemonOptions opts_;
  ara::com::mavlink::RadioDriverRegistry* registry_;  // not owned: opts_.registry or the global one
  std::map<std::string, std::shared_ptr<RadioLink>> links_;  // immutable between Start() and Stop()
  ara::com::mavlink::daemon::MessageServer server_;

  mutable std::mutex mutex_;
  std::vector<std::shared_ptr<Instance>> instances_;
  std::vector<std::shared_ptr<Session>> sessions_;
  std::map<std::uint32_t, MsgInfo> messages_;
  std::uint64_t next_instance_id_{1};
  std::uint64_t next_session_id_{1};
  std::atomic<std::uint64_t> deliver_failed_{0}, register_rejected_{0}, send_rejected_{0};
  std::jthread accept_thread_;
  std::jthread stats_thread_;
  std::atomic<bool> stopping_{false};
  bool started_{false};
};

}  // namespace mavlink_daemon
}  // namespace srp

#endif  // PLATFORM_COMMON_MAVLINK_DAEMON_CODE_CORE_MAVLINK_DAEMON_CORE_H_
