/**
 * Copyright (c) 2024
 *
 * @file daemon_test_support.h
 * @brief TESTONLY: an IPC test client (an "application"), a reference service and builders of
 * "over the air" frames for the MavlinkDaemon tests.
 *
 * Reference service (all numbers little-endian, field layout as MessageCodec):
 *   event 0x8001  msg base+1: seq@0(2) instance@2(2) value@4(4)                       len 8
 *   event 0x8002  msg base+4: as above
 *   method 1      REQ  base+2: seq@0 instance@2 target_system@4 target_component@5 arg@6      len 7
 *                 RESP base+3: seq@0 instance@2 target_system@4 target_component@5 status@6 result@7  len 8
 *   method 2 (void, no RESP) REQ base+5: as the REQ of method 1
 */
#ifndef PLATFORM_COMMON_MAVLINK_DAEMON_CODE_UT_DAEMON_TEST_SUPPORT_H_
#define PLATFORM_COMMON_MAVLINK_DAEMON_CODE_UT_DAEMON_TEST_SUPPORT_H_

#include <algorithm>
#include <atomic>
#include <chrono>  // NOLINT
#include <condition_variable>  // NOLINT
#include <cstdint>
#include <deque>
#include <mutex>  // NOLINT
#include <optional>
#include <string>
#include <thread>  // NOLINT
#include <utility>
#include <variant>
#include <vector>

#include "ara/com/mavlink/daemon_ipc.h"
#include "ara/com/mavlink/daemon_protocol.h"

namespace srp {
namespace mavlink_daemon {
namespace testing {
namespace dp = ara::com::mavlink::daemon;

constexpr std::uint8_t kCrc = 42;
constexpr std::uint16_t kEv1 = 0x8001;
constexpr std::uint16_t kEv2 = 0x8002;
constexpr std::uint16_t kMethod = 1;
constexpr std::uint16_t kVoidMethod = 2;

inline dp::MessageDesc Desc(std::uint32_t id, std::uint8_t len, const std::string& name,
                            std::uint8_t tsys = dp::kNoOffset, std::uint8_t tcomp = dp::kNoOffset,
                            std::uint8_t status = dp::kNoOffset) {
  dp::MessageDesc d;
  d.msg_id = id;
  d.crc_extra = kCrc;
  d.full_len = len;
  d.seq_offset = 0;
  d.instance_offset = 2;
  d.target_system_offset = tsys;
  d.target_component_offset = tcomp;
  d.status_offset = status;
  d.name = name;
  return d;
}

/// Registration of the reference service. `base` = base msg_id (different services = different bases).
inline dp::RegisterInstance MakeRegistration(dp::Role role, const std::string& com_id, std::uint16_t instance,
                                             const std::vector<std::string>& links, std::uint32_t handle,
                                             std::uint32_t base = 50000, std::uint32_t event_interval_ms = 0) {
  dp::RegisterInstance r;
  r.handle = handle;
  r.role = role;
  r.com_id = com_id;
  r.instance = instance;
  r.links = links;
  dp::ElementDesc ev1;
  ev1.id = kEv1;
  ev1.is_event = true;
  ev1.req = Desc(base + 1, 8, com_id + "_EV1");
  dp::ElementDesc ev2;
  ev2.id = kEv2;
  ev2.is_event = true;
  ev2.req = Desc(base + 4, 8, com_id + "_EV2");
  dp::ElementDesc m1;
  m1.id = kMethod;
  m1.has_resp = true;
  m1.req = Desc(base + 2, 7, com_id + "_M1_REQ", 4, 5);
  m1.resp = Desc(base + 3, 8, com_id + "_M1_RESP", 4, 5, 6);
  dp::ElementDesc m2;
  m2.id = kVoidMethod;
  m2.req = Desc(base + 5, 7, com_id + "_M2_REQ", 4, 5);
  r.elements = {ev1, ev2, m1, m2};
  for (auto& e : r.elements) {  // by default every element goes over every link of the instance
    for (const auto& l : links) {
      e.links.push_back({l, e.id == kEv1 ? event_interval_ms : 0U});
    }
  }
  return r;
}

/// Restricts an element to the given links (name, min_interval_ms).
inline void SetLinks(dp::ElementDesc* e, std::vector<dp::ElementLink> links) {
  e->links = std::move(links);
}

/// Application -> daemon messages of the three kinds.
inline dp::ElementPayload Event(std::uint32_t handle, std::uint16_t element, std::vector<std::uint8_t> payload) {
  return dp::ElementPayload{dp::ElementKind::kEvent, handle, element, std::move(payload)};
}
inline dp::ElementPayload Request(std::uint32_t handle, std::uint16_t element, std::vector<std::uint8_t> payload) {
  return dp::ElementPayload{dp::ElementKind::kRequest, handle, element, std::move(payload)};
}
inline dp::ElementPayload Response(std::uint32_t handle, std::uint16_t element, std::vector<std::uint8_t> payload) {
  return dp::ElementPayload{dp::ElementKind::kResponse, handle, element, std::move(payload)};
}

inline std::vector<std::uint8_t> EventPayload(std::uint16_t seq, std::uint16_t instance, std::uint32_t value) {
  std::vector<std::uint8_t> p(8, 0);
  p[0] = static_cast<std::uint8_t>(seq);
  p[1] = static_cast<std::uint8_t>(seq >> 8);
  p[2] = static_cast<std::uint8_t>(instance);
  p[3] = static_cast<std::uint8_t>(instance >> 8);
  for (int i = 0; i < 4; ++i) p[4 + i] = static_cast<std::uint8_t>(value >> (8 * i));
  return p;
}

inline std::vector<std::uint8_t> ReqPayload(std::uint16_t seq, std::uint16_t instance, std::uint8_t tsys,
                                            std::uint8_t tcomp, std::uint8_t arg) {
  std::vector<std::uint8_t> p(7, 0);
  p[0] = static_cast<std::uint8_t>(seq);
  p[1] = static_cast<std::uint8_t>(seq >> 8);
  p[2] = static_cast<std::uint8_t>(instance);
  p[3] = static_cast<std::uint8_t>(instance >> 8);
  p[4] = tsys;
  p[5] = tcomp;
  p[6] = arg;
  return p;
}

inline std::vector<std::uint8_t> RespPayload(std::uint16_t seq, std::uint16_t instance, std::uint8_t tsys,
                                             std::uint8_t tcomp, std::uint8_t status, std::uint8_t result) {
  std::vector<std::uint8_t> p(8, 0);
  p[0] = static_cast<std::uint8_t>(seq);
  p[1] = static_cast<std::uint8_t>(seq >> 8);
  p[2] = static_cast<std::uint8_t>(instance);
  p[3] = static_cast<std::uint8_t>(instance >> 8);
  p[4] = tsys;
  p[5] = tcomp;
  p[6] = status;
  p[7] = result;
  return p;
}

inline std::uint16_t SeqOf(const std::vector<std::uint8_t>& p) {
  return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

/// An "application": an IPC connection + a receive thread collecting the daemon messages.
class TestClient {
 public:
  explicit TestClient(const std::string& path) {
    auto c = dp::MessageSocket::Connect(path);
    if (c.HasValue()) {
      socket_ = c.Value();
      reader_ = std::thread([this] { ReadLoop(); });
    }
  }
  ~TestClient() { Close(); }
  TestClient(const TestClient&) = delete;
  TestClient& operator=(const TestClient&) = delete;

  bool connected() const { return socket_.IsOpen(); }
  void Close() {
    stop_ = true;
    socket_.Shutdown();
    if (reader_.joinable()) reader_.join();
  }
  bool closed_by_peer() const { return closed_.load(); }

  bool Send(const dp::Message& m) { return socket_.Send(m).HasValue(); }

  /// Registers an instance and waits for the RegisterReply with that handle.
  std::optional<dp::RegisterReply> Register(const dp::RegisterInstance& r, std::uint32_t timeout_ms = 2000) {
    if (!Send(r)) return std::nullopt;
    return NextMatching<dp::RegisterReply>([&](const dp::RegisterReply& x) { return x.handle == r.handle; },
                                           timeout_ms);
  }

  template <typename T>
  std::optional<T> Next(std::uint32_t timeout_ms = 2000) {
    return NextMatching<T>([](const T&) { return true; }, timeout_ms);
  }

  template <typename T, typename Pred>
  std::optional<T> NextMatching(Pred pred, std::uint32_t timeout_ms = 2000) {
    std::unique_lock<std::mutex> lock{mutex_};
    std::optional<T> out;
    cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&] {
      for (auto it = inbox_.begin(); it != inbox_.end(); ++it) {
        if (const auto* x = std::get_if<T>(&*it)) {
          if (pred(*x)) {
            out = *x;
            inbox_.erase(it);
            return true;
          }
        }
      }
      return closed_.load();
    });
    return out;
  }

  /// The next delivery of `kind` (event / REQ / RESP) from the daemon.
  std::optional<dp::Delivery> NextDelivery(dp::ElementKind kind, std::uint32_t timeout_ms = 2000) {
    return NextMatching<dp::Delivery>([kind](const dp::Delivery& d) { return d.kind == kind; }, timeout_ms);
  }
  /// Waits `ms` and says whether no delivery of `kind` arrived meanwhile.
  bool NoDelivery(dp::ElementKind kind, std::uint32_t ms = 200) { return !NextDelivery(kind, ms).has_value(); }

  /// Waits for a LinkStatus of the given link with the expected readiness.
  bool WaitLinkStatus(const std::string& link, bool ready, std::uint32_t timeout_ms = 3000) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < end) {
      const auto left = static_cast<std::uint32_t>(
          std::chrono::duration_cast<std::chrono::milliseconds>(end - std::chrono::steady_clock::now()).count());
      const auto s = NextMatching<dp::LinkStatus>([&](const dp::LinkStatus& x) { return x.link == link; },
                                                  std::max<std::uint32_t>(left, 1));
      if (s && s->ready == ready) return true;
      if (!s) return false;
    }
    return false;
  }

  /// How many messages of the type are in the inbox.
  template <typename T>
  std::size_t Pending() {
    std::lock_guard<std::mutex> lock{mutex_};
    std::size_t n = 0;
    for (const auto& m : inbox_) n += std::holds_alternative<T>(m) ? 1 : 0;
    return n;
  }

  /// Waits `ms` and says whether no message of type T arrived meanwhile.
  template <typename T>
  bool NothingArrives(std::uint32_t ms = 200) {
    return !Next<T>(ms).has_value();
  }

 private:
  void ReadLoop() {
    while (!stop_.load()) {
      auto r = socket_.Receive(50);
      if (r.status == dp::RecvStatus::kTimeout) continue;
      if (r.status != dp::RecvStatus::kMessage) {
        closed_ = true;
        cv_.notify_all();
        return;
      }
      {
        std::lock_guard<std::mutex> lock{mutex_};
        inbox_.push_back(std::move(*r.message));
      }
      cv_.notify_all();
    }
  }

  dp::MessageSocket socket_;
  std::thread reader_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<dp::Message> inbox_;
  std::atomic<bool> stop_{false};
  std::atomic<bool> closed_{false};
};

}  // namespace testing
}  // namespace mavlink_daemon
}  // namespace srp

#endif  // PLATFORM_COMMON_MAVLINK_DAEMON_CODE_UT_DAEMON_TEST_SUPPORT_H_
