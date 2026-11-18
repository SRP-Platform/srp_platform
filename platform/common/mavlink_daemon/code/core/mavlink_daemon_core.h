/**
 * Copyright (c) 2024
 *
 * @file mavlink_daemon_core.h
 * @brief Rdzeń MavlinkDaemona (bez ara::exec): łącza z driverów radia, serwer IPC aplikacji,
 * rejestr instancji i wiadomości, routing RX (CRC -> podpis -> łącze instancji -> target/źródło ->
 * dedup -> dostarczenie) i TX (event/REQ/RESP -> kolejki łączy z redundancją).
 *
 * Aplikacje łączą się gniazdem (daemon_protocol.h), rejestrują instancje `on: mavlink` i
 * wymieniają PEŁNE payloady MAVLink; daemon nie zna modeli SOME/IP. Konflikt instancji
 * (com_id, instance, rola) albo msg_id (inna definicja / inna usługa) = odrzucenie rejestracji
 * z powodem w RegisterReply. Rozłączenie aplikacji sprząta jej rejestracje (msg_id wracają do puli,
 * eventy z kolejek znikają). Restart daemona = aplikacja łączy się i rejestruje od nowa.
 *
 * Tablica crc_extra dla parsera łączy powstaje z rejestracji (wiadomości zarejestrowanych instancji).
 */
#ifndef PLATFORM_COMMON_MAVLINK_DAEMON_CODE_CORE_MAVLINK_DAEMON_CORE_H_
#define PLATFORM_COMMON_MAVLINK_DAEMON_CODE_CORE_MAVLINK_DAEMON_CORE_H_

#include <atomic>
#include <chrono>  // NOLINT
#include <cstdint>
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
#include "ara/com/mavlink/dedup_table.h"
#include "ara/com/mavlink/mavlink_log.h"
#include "ara/com/mavlink/mavlink_signing.h"
#include "ara/com/mavlink/radio_driver_loader.h"
#include "ara/core/result.h"
#include "platform/common/mavlink_daemon/code/config/daemon_config.h"
#include "platform/common/mavlink_daemon/code/link/radio_link.h"

namespace srp {
namespace mavlink_daemon {

struct DaemonOptions {
  ara::com::mavlink::LogFn log;
  ara::com::mavlink::LinkSigner::NowFn now{ara::com::mavlink::SystemTimestamp10us};
  /// Dodatkowe (wbudowane) drivery — testy i hosty z własnym driverem w procesie.
  std::function<void(ara::com::mavlink::RadioDriverRegistry*)> extra_drivers;
  /// Timeout wysyłki do aplikacji (wolna aplikacja nie może blokować RX łącza w nieskończoność).
  std::uint32_t deliver_timeout_ms{500};
  std::chrono::milliseconds busy_warn_after{5000};
};

struct DaemonCounters {
  std::uint64_t duplicates{0};
  std::uint64_t unknown_messages{0};
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

  /// Ładuje pluginy, tworzy drivery i łącza, otwiera gniazdo, startuje wątki. Błąd startu =
  /// jedna wiadomość ze wszystkimi problemami (każdy też w logu); daemon nic nie uruchamia.
  /// Rdzeń jest JEDNORAZOWY: po `Stop()` ponowny `Start()` na tym samym obiekcie zwraca błąd (drivery
  /// wbudowane i pluginy są rejestrowane raz, łącza zostają po Stop) — twórz nowy obiekt.
  ara::core::Result<void> Start();
  void Stop();

  std::shared_ptr<RadioLink> FindLink(const std::string& name) const;
  std::size_t instance_count() const;
  std::size_t session_count() const;
  /// Ile wiadomości o tym msg_id jest zarejestrowanych (0 = msg_id wolny).
  bool IsMsgIdRegistered(std::uint32_t msg_id) const;
  DaemonCounters counters() const;
  const std::string& socket_path() const { return cfg_.socket_path; }

 private:
  struct MsgInfo {
    enum Kind { kEvent, kReq, kResp };
    ara::com::mavlink::daemon::MessageDesc desc;
    Kind kind{kEvent};
    std::string com_id;
    std::uint16_t element_id{0};
    int refs{0};
  };
  struct Session;
  struct Instance {
    std::uint64_t id{0};
    std::weak_ptr<Session> session;
    std::string session_label;
    ara::com::mavlink::daemon::RegisterInstance reg;
    std::vector<std::shared_ptr<RadioLink>> links;
    std::map<std::uint16_t, std::size_t> element_index;
    std::atomic<bool> active{false};  ///< po wysłaniu RegisterReply (przed tym nic nie jest dostarczane)
    std::mutex status_mutex;          ///< serializuje LinkStatus (stan początkowy vs zmiany)
    std::atomic<std::uint64_t> rx_rejected{0};
    std::atomic<std::uint64_t> rx_duplicate{0};
  };
  struct Session {
    std::uint64_t id{0};
    ara::com::mavlink::daemon::MessageSocket socket;
    std::map<std::uint32_t, std::shared_ptr<Instance>> by_handle;  // pod mutex_ rdzenia
    std::thread thread;  // dołączany przez rdzeń (nigdy z własnego wątku)
    std::atomic<bool> done{false};
    std::string label() const { return "aplikacja#" + std::to_string(id); }
  };

  void AcceptLoop();
  void ReapSessions();
  void SessionLoop(const std::shared_ptr<Session>& s);
  void HandleRegister(const std::shared_ptr<Session>& s, const ara::com::mavlink::daemon::RegisterInstance& r);
  void HandleUnregister(const std::shared_ptr<Session>& s, std::uint32_t handle);
  void HandleEvent(const std::shared_ptr<Session>& s, const ara::com::mavlink::daemon::Event& m);
  void HandleRequest(const std::shared_ptr<Session>& s, const ara::com::mavlink::daemon::Request& m);
  void HandleResponse(const std::shared_ptr<Session>& s, const ara::com::mavlink::daemon::Response& m);
  void CleanupSession(const std::shared_ptr<Session>& s);
  void RemoveInstanceLocked(const std::shared_ptr<Instance>& inst);

  std::shared_ptr<Instance> FindInstance(const std::shared_ptr<Session>& s, std::uint32_t handle) const;
  /// Pusty tekst = OK; w przeciwnym razie powód konfliktu msg_id.
  std::string CheckMsgConflictsLocked(const ara::com::mavlink::daemon::RegisterInstance& r) const;
  void AddMessagesLocked(const ara::com::mavlink::daemon::RegisterInstance& r);
  std::optional<std::uint8_t> CrcLookup(std::uint32_t msg_id) const;
  void OnFrame(const std::string& link_name, const ara::com::mavlink::Frame& f);
  void OnLinkReady(const std::string& link_name, bool ready);
  bool Deliver(const std::shared_ptr<Instance>& inst, const ara::com::mavlink::daemon::Message& m);
  void SendUnknownInstanceResponse(const ara::com::mavlink::daemon::ElementDesc& el,
                                   std::uint16_t seq, std::uint16_t instance, std::uint8_t to_sys,
                                   std::uint8_t to_comp, const std::shared_ptr<RadioLink>& link);
  void Log(ara::com::mavlink::LogLevel l, const std::string& m) const;

  DaemonConfig cfg_;
  DaemonOptions opts_;
  ara::com::mavlink::RadioDriverRegistry registry_;
  ara::com::mavlink::RadioDriverLoader loader_;
  std::map<std::string, std::shared_ptr<RadioLink>> links_;  // niezmienne po Start()
  ara::com::mavlink::daemon::MessageServer server_;

  mutable std::mutex mutex_;
  std::vector<std::shared_ptr<Instance>> instances_;
  std::vector<std::shared_ptr<Session>> sessions_;
  std::map<std::uint32_t, MsgInfo> messages_;
  std::uint64_t next_instance_id_{1};
  std::uint64_t next_session_id_{1};
  ara::com::mavlink::DedupTable dedup_;
  std::atomic<std::uint64_t> duplicates_{0}, unknown_{0}, deliver_failed_{0}, register_rejected_{0},
      send_rejected_{0};
  std::thread accept_thread_;
  std::atomic<bool> stopping_{false};
  bool started_{false};
  bool used_{false};  ///< Start() wywołano już raz (rdzeń jest jednorazowy)
};

}  // namespace mavlink_daemon
}  // namespace srp

#endif  // PLATFORM_COMMON_MAVLINK_DAEMON_CODE_CORE_MAVLINK_DAEMON_CORE_H_
