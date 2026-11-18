/**
 * Copyright (c) 2024
 *
 * @file radio_driver.h
 * @brief MAVLink radio driver API: IRadioDriver / IRadioListener, the driver factory and the
 * `.so` plugin interface (SRP_MAVLINK_DRIVER_PLUGIN macro).
 *
 * The platform knows no modem. A driver is a class with its own thread (waiting for an AUX/UART
 * interrupt through `poll()`) that the MavlinkDaemon opens for a link of the config
 * (`links.<name>.driver` = factory id, `driver_config` = path to the driver's file; only the
 * driver knows the format). The built-in driver `serial_radio` (serial_radio.h) is the
 * reference; a plugin with the same id replaces it.
 *
 * NOTE: changing this header is a breaking change (implementations live in the srp repo) — it
 * requires bumping `kRadioDriverApiVersion`.
 *
 * CONTRACT (checked by the `radio_driver_contract_test` suite):
 *
 *  Start(listener, stop_token)
 *    Configures the radio and starts the driver thread; may block (e.g. AT commands) and should
 *    react to the stop_token. Called on the daemon's link thread, not on the driver thread. An
 *    error makes the link retry after 30 s (kPersistent: logged as Error once; kRetryable: Warn).
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
 *                       again after 30 s. fatal=false: informational (logged, the link keeps
 *                       running).
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

/// Implemented by the daemon; called from the driver thread.
class IRadioListener {
 public:
  virtual ~IRadioListener() = default;
  virtual void OnReceive(std::span<const std::uint8_t> bytes) noexcept = 0;
  virtual void OnTxReady() noexcept = 0;
  virtual void OnError(ara::core::ErrorCode e, bool fatal) noexcept = 0;
};

class IRadioDriver {
 public:
  virtual ~IRadioDriver() = default;
  virtual ara::core::Result<void> Start(IRadioListener& listener,  // NOLINT(runtime/references)
                                        std::stop_token token) noexcept = 0;
  virtual void Stop() noexcept = 0;
  virtual ara::core::Result<void> Send(std::span<const std::uint8_t> frame) noexcept = 0;
  virtual std::string Name() const noexcept = 0;
};

/// Plugin API version (symbol `srp_mavlink_driver_api_version`); a different one than the
/// daemon's is a start error.
constexpr std::uint32_t kRadioDriverApiVersion = 1;

/// Creates a driver for a link; `config_path` = `driver_config` from the daemon config (may be
/// empty). nullptr = the driver cannot be created (the daemon: a start error naming the link).
using RadioDriverFactory =
    std::function<std::shared_ptr<IRadioDriver>(const std::string& config_path)>;

/// The registry the daemon hands to a plugin.
class IRadioDriverRegistry {
 public:
  virtual ~IRadioDriverRegistry() = default;
  /// Registers a factory under an id. Error: empty id/factory or an id taken by another plugin
  /// (a duplicate is a daemon start error even if the plugin ignores the result). A plugin with
  /// the id of the built-in driver (`serial_radio`) replaces it.
  virtual ara::core::Result<void> Register(const std::string& id,
                                           RadioDriverFactory factory) noexcept = 0;
};

using DriverPluginRegisterFn = void (*)(IRadioDriverRegistry&);

constexpr const char* kPluginVersionSymbol = "srp_mavlink_driver_api_version";
constexpr const char* kPluginRegisterSymbol = "srp_mavlink_driver_register";

}  // namespace mavlink
}  // namespace com
}  // namespace ara

/// Declaration of a `.so` plugin: `register_fn` is `void(ara::com::mavlink::IRadioDriverRegistry&)`
/// and calls `registry.Register("id", factory)` for 1+ drivers. Use once per library (usually
/// through the bazel rule `mavlink_driver_plugin`).
#define SRP_MAVLINK_DRIVER_PLUGIN(register_fn)                                              \
  extern "C" __attribute__((visibility("default"))) std::uint32_t                          \
  srp_mavlink_driver_api_version() {                                                       \
    return ::ara::com::mavlink::kRadioDriverApiVersion;                                    \
  }                                                                                        \
  extern "C" __attribute__((visibility("default"))) void srp_mavlink_driver_register(      \
      ::ara::com::mavlink::IRadioDriverRegistry* registry) {                               \
    register_fn(*registry);                                                                \
  }

#endif  // ARA_COM_MAVLINK_RADIO_DRIVER_H_
