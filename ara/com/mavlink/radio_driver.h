/**
 * Copyright (c) 2024
 *
 * @file radio_driver.h
 * @brief MAVLink radio driver API: IRadioDriver / IRadioListener and the driver factory
 * (registration: radio_driver_registry.h).
 *
 * The platform knows no modem. A driver is a class with its own thread (waiting for an AUX/UART
 * interrupt through `poll()`) that the MavlinkDaemon opens for a link of the config
 * (`links.<name>.driver` = factory id, `driver_config` = path to the driver's file; only the
 * driver knows the format). Drivers are linked statically into the daemon binary and register
 * themselves with SRP_MAVLINK_REGISTER_DRIVER; `serial_radio` (serial_radio.h) is the
 * reference driver.
 *
 * NOTE: changing this header is a breaking change (implementations live in the srp repo, which
 * builds the daemon binary against this platform revision).
 *
 * CONTRACT (checked by the `radio_driver_contract_test` suite):
 *
 *  Start(listener, stop_token)
 *    Configures the radio and starts the driver thread; may block (e.g. AT commands) and should
 *    react to the stop_token. Called on the daemon's link thread, not on the driver thread. An
 *    error makes the link retry after the restart backoff (500 ms, doubling up to 30 s;
 *    kPersistent: logged as Error once; kRetryable: Warn).
 *    Start() after Stop() on the same object MUST work. Send() may be called after a successful
 *    Start().
 *
 *  Stop()
 *    Stops the driver thread; AFTER IT RETURNS THERE ARE ZERO CALLBACKS (no OnReceive/OnTxReady/
 *    OnError is in progress or will follow). Idempotent, thread-safe with respect to Send().
 *    Stop() must NOT be called from a listener callback (deadlock on joining the driver thread).
 *
 *  Send(frame) — NON-BLOCKING
 *    Accepts the whole frame or nothing (no partial write visible to the daemon): success = the
 *    frame was accepted for transmission; kBusy = nothing accepted, the daemon holds the frame
 *    until OnTxReady(); any other error = device lost (kPersistent/kClosed) or a transient error
 *    (kRetryable, frame dropped). After kBusy the driver MUST call OnTxReady() (once) when room
 *    is available again — also when kBusy was returned just before room appeared. Send() may be
 *    called from any daemon thread (one at a time) and MUST NOT call listener callbacks
 *    synchronously (OnTxReady only from the driver thread).
 *
 *  Listener callbacks (from the driver thread, never from Send()/Start())
 *    OnReceive(bytes)   any chunk of the stream (also half a frame); the daemon copies it and
 *                       wakes its RX thread, so the callback must be short; the bytes are valid
 *                       only during the call;
 *    OnTxReady()        room became available after kBusy;
 *    OnError(e, fatal)  fatal=true: the radio is unusable -> the daemon calls Stop() and Start()
 *                       again after the restart backoff. fatal=false: informational (logged, the link keeps
 *                       running).
 *    OnLinkStats(s)     OPTIONAL push of radio measurements (RSSI, noise floor, SNR) whenever the
 *                       driver has fresh data; `s.valid` says which fields it measured. Only from
 *                       the driver thread (never from Start()/Send()) and NEVER after Stop() has
 *                       returned; short and non-blocking like the other callbacks. A driver that
 *                       reports nothing is still correct (the daemon then publishes `valid` = 0).
 *
 *  MaxFrameSize()
 *    OPTIONAL: the largest frame (bytes, whole MAVLink frame) that Send() accepts; 0 (default) =
 *    no limit. Constant for the life of the object; callable before Start().
 *
 *  General requirements
 *    virtual destructor; noexcept methods; no exceptions across the interface boundary;
 *    Name() = a stable name for logs.
 */
#ifndef ARA_COM_MAVLINK_RADIO_DRIVER_H_
#define ARA_COM_MAVLINK_RADIO_DRIVER_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>  // NOLINT
#include <stop_token>  // NOLINT
#include <string>

#include "ara/com/mavlink/transport_error.h"
#include "ara/core/error_code.h"
#include "ara/core/result.h"

namespace ara {
namespace com {
namespace mavlink {

/// Radio measurements pushed by a driver (`IRadioListener::OnLinkStats`). A field is meaningful
/// only when its bit is set in `valid`.
struct RadioLinkStats {
  static constexpr std::uint8_t kRssi = 1U << 0;
  static constexpr std::uint8_t kNoiseFloor = 1U << 1;
  static constexpr std::uint8_t kSnr = 1U << 2;

  std::uint8_t valid{0};              ///< mask of kRssi | kNoiseFloor | kSnr
  std::int16_t rssi_dbm{0};           ///< received signal strength [dBm]
  std::int16_t noise_floor_dbm{0};    ///< noise floor [dBm]
  std::int16_t snr_db{0};             ///< signal to noise ratio [dB]
};

/// Implemented by the daemon; called from the driver thread.
class IRadioListener {
 public:
  virtual ~IRadioListener() = default;
  virtual void OnReceive(std::span<const std::uint8_t> bytes) noexcept = 0;
  virtual void OnTxReady() noexcept = 0;
  virtual void OnError(ara::core::ErrorCode e, bool fatal) noexcept = 0;
  /// Optional (default: ignored); see the contract above.
  virtual void OnLinkStats(const RadioLinkStats& stats) noexcept { static_cast<void>(stats); }
};

class IRadioDriver {
 public:
  virtual ~IRadioDriver() = default;
  virtual ara::core::Result<void> Start(IRadioListener& listener,  // NOLINT(runtime/references)
                                        std::stop_token token) noexcept = 0;
  virtual void Stop() noexcept = 0;
  virtual ara::core::Result<void> Send(std::span<const std::uint8_t> frame) noexcept = 0;
  virtual std::string Name() const noexcept = 0;
  /// Largest frame Send() accepts; 0 = no limit (see the contract above).
  virtual std::size_t MaxFrameSize() const noexcept { return 0; }
};

/// Creates a driver for a link; `config_path` = `driver_config` from the daemon config (may be
/// empty). nullptr = the driver cannot be created (the daemon: a start error naming the link).
using RadioDriverFactory =
    std::function<std::shared_ptr<IRadioDriver>(const std::string& config_path)>;

}  // namespace mavlink
}  // namespace com
}  // namespace ara

#endif  // ARA_COM_MAVLINK_RADIO_DRIVER_H_
