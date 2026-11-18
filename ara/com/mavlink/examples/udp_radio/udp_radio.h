/**
 * Copyright (c) 2024
 *
 * @file udp_radio.h
 * @brief Example radio driver `example_udp_radio`: one MAVLink frame = one UDP datagram.
 *
 * A teaching example of the IRadioDriver contract (ara/com/mavlink/radio_driver.h) with no modem
 * and no dependency outside the platform. Real drivers live in the integrator repository (srp),
 * not here; see "Writing and linking a radio driver" in docs/mavlink/README.md.
 *
 * Driver config file (`driver_config` of the link), JSON, IPv4 only:
 * @code
 *   { "bind_port": 14560, "peer_host": "127.0.0.1", "peer_port": 14550 }
 * @endcode
 * Datagrams that arrive on `bind_port` are passed to the daemon; every frame from the daemon is
 * sent to `peer_host:peer_port`. A MAVLink frame is at most 280 bytes, so it always fits into
 * one datagram (MaxFrameSize() = 280).
 *
 * An unknown key in the file is a Start() error (kPersistent). A peer that is not reachable (no route,
 * host down, port closed) does not stop the link (Send() stays kRetryable); the driver reports it to the
 * daemon log with OnError(non fatal), at most once per 10 s.
 */
#ifndef ARA_COM_MAVLINK_EXAMPLES_UDP_RADIO_UDP_RADIO_H_
#define ARA_COM_MAVLINK_EXAMPLES_UDP_RADIO_UDP_RADIO_H_

#include <netinet/in.h>

#include <chrono>  // NOLINT
#include <cstddef>
#include <cstdint>
#include <mutex>  // NOLINT
#include <span>  // NOLINT
#include <stop_token>  // NOLINT
#include <string>
#include <thread>  // NOLINT

#include "ara/com/mavlink/radio_driver.h"

namespace ara {
namespace com {
namespace mavlink {
namespace examples {

/// Id of the driver in the registry (`links.<name>.driver`).
constexpr const char* kUdpRadioId = "example_udp_radio";

class UdpRadio final : public IRadioDriver {
 public:
  /// The largest MAVLink 2 frame (header + payload + checksum + signature).
  static constexpr std::size_t kMaxFrame = 280;

  /// Explicit parameters (tests).
  UdpRadio(std::uint16_t bind_port, std::string peer_host, std::uint16_t peer_port);
  /// From the JSON file of the link; a file error only surfaces from Start() (kPersistent).
  explicit UdpRadio(const std::string& config_path);
  ~UdpRadio() override;
  UdpRadio(const UdpRadio&) = delete;
  UdpRadio& operator=(const UdpRadio&) = delete;

  ara::core::Result<void> Start(IRadioListener& listener, std::stop_token token) noexcept override;
  void Stop() noexcept override;
  ara::core::Result<void> Send(std::span<const std::uint8_t> frame) noexcept override;
  std::string Name() const noexcept override { return name_; }
  std::size_t MaxFrameSize() const noexcept override { return kMaxFrame; }

 private:
  void Loop(std::stop_token caller, IRadioListener* listener);
  void Wake() noexcept;
  void SetPeer();
  void NoteUnreachable(int err);
  void DrainErrorQueue();

  std::string name_;
  std::string config_error_;  // not empty = the config is bad (Start() fails)
  std::uint16_t bind_port_{0};
  std::string peer_host_;
  std::uint16_t peer_port_{0};
  sockaddr_in peer_{};

  std::mutex lifecycle_mutex_;  // Start()/Stop() (never held by the driver thread)
  std::mutex mutex_;            // socket state shared with Send()
  int sock_{-1};
  int wake_fd_{-1};            // eventfd: wakes poll() for Stop() and after kBusy
  bool running_{false};
  std::string pending_report_;  // an unreachable-peer line for the driver thread (OnError, not fatal)
  bool reported_{false};
  std::chrono::steady_clock::time_point last_report_{};
  bool tx_ready_owed_{false};  // Send() returned kBusy: OnTxReady() is owed
  std::stop_source stop_;
  std::thread thread_;
};

}  // namespace examples
}  // namespace mavlink
}  // namespace com
}  // namespace ara

#endif  // ARA_COM_MAVLINK_EXAMPLES_UDP_RADIO_UDP_RADIO_H_
