# MavlinkDaemon

One platform application per CPU that **owns the MAVLink links** and serves many `on: mavlink`
applications at once (modelled on `someip_demon`). The daemon provides: links to radios, queues and
priorities, v2 signing, HEARTBEAT, dedup of copies from several links, and the `crc_extra` table built from
application registrations. Applications are thin bindings — IPC clients of the daemon (they register service
instances and pass messages through).

```
 app A ──┐                         ┌── radio driver (.so plugin or serial_radio) ── air
 app B ──┼─ SEQPACKET socket ──  daemon ── links "lora" / "radio_b" / … (queues, signing, HEARTBEAT, dedup)
 app C ──┘   (daemon_protocol.h)    └── radio driver …
```

## Configuration (`mavlink_daemon_config.json`, in srp `deployment`)

```json
{
  "system_id": 1,
  "component_id": 1,
  "socket_path": "/run/srp_mavlink_daemon.sock",
  "socket_mode": "0660",
  "driver_dir": "/usr/lib/srp/mavlink_drivers",
  "restart_delay_ms": 30000,
  "links": {
    "lora": {
      "driver": "ebyte_e220",
      "driver_config": "/etc/srp/mavlink/e220.json",
      "version": 2,
      "heartbeat_hz": 1,
      "signing": { "enabled": true, "key_file": "/etc/srp/mavlink/lora.key", "link_id": 1,
                   "state_file": "/var/lib/srp/mavlink/lora.state", "accept_unsigned": false }
    },
    "radio_b": { "driver": "serial_radio", "driver_config": "/etc/srp/mavlink/radio_b_serial.json" }
  }
}
```

| field | meaning |
|-------|---------|
| `system_id`, `component_id` | identity of the CPU in MAVLink (1..255); **one `component_id` per CPU**, used on every link and in HEARTBEAT |
| `socket_path`, `socket_mode` | unix socket for applications (default `/run/srp_mavlink_daemon.sock`, mode `0660`) |
| `driver_dir` | directory with `*.so` plugins; empty/missing = only the built-in `serial_radio` |
| `restart_delay_ms` | pause before `Start()` of a driver again after a fatal error or a failed start (default 30 s) |
| `links.<name>.driver` | driver id from the factory (built in or from a plugin) |
| `links.<name>.driver_config` | path to the **driver's own file** — only the driver knows its format |
| `version` / `heartbeat_hz` / `signing` | transmit version (1\|2, both are accepted on receive), HEARTBEAT (0 = off), v2 signing |

The platform knows no modem or medium — link names and plugins are added by the integrator (srp).

### Start errors (the daemon starts nothing; one message + an `Error` log)

- a plugin with a wrong API version, without plugin symbols, or one that cannot be loaded (`dlopen`),
- a **duplicate driver id** between plugins (a plugin with id `serial_radio` *replaces* the built-in one — logged),
- a driver used by a link is missing (the message names the link, the id and the known drivers),
- a missing `driver_dir`, an unusable IPC socket, config errors (all at once, naming the link), among them:
  **`signing.enabled` on a link with `version: 1`** (signing needs MAVLink v2), a **repeated `signing.link_id`**
  on signed links (a receiver merging redundant radios keeps one timestamp state per (link_id, sysid, compid),
  so copies from the second radio would be rejected as replay) and the **same non-empty `signing.state_file`**
  on two links (two signers would overwrite one file),
- a **live daemon at `socket_path`**: the daemon holds an exclusive `flock` on `<socket_path>.lock` for its
  whole life; a locked file = error "daemon already running" (a stale socket of a killed process is replaced,
  since the kernel drops the lock with its owner).

`Start()` after `Stop()` on the same object starts the daemon again (plugins already loaded are not loaded twice).

## Radio drivers

API: `ara/com/mavlink/radio_driver.h` (`IRadioDriver`, `IRadioListener`; the contract is described in the header).
A driver has its own thread (e.g. `poll()` on AUX/UART) and `Send()` is non-blocking: `kBusy` = nothing accepted,
the daemon holds that frame until `OnTxReady()`; `OnError(fatal)` → the daemon calls `Stop()` and, after
`restart_delay`, `Start()`.

**Built-in `serial_radio`** — raw bytes over UART, no AT commands or modem configuration. Driver file:

```json
{ "device": "/dev/ttyS2", "baudrate": 57600 }
```

**`.so` plugin** (in the srp repo):

```python
load("@srp_platform//ara/com/mavlink:mavlink_driver_plugin.bzl", "mavlink_driver_plugin")
mavlink_driver_plugin(name = "ebyte_e220", srcs = ["e220_plugin.cc"], deps = [":e220_driver"])  # libebyte_e220.so
```

```cpp
#include "ara/com/mavlink/radio_driver.h"
void RegisterDrivers(ara::com::mavlink::IRadioDriverRegistry& registry) {
  registry.Register("ebyte_e220", [](const std::string& config_path) { return std::make_shared<E220Driver>(config_path); });
}
SRP_MAVLINK_DRIVER_PLUGIN(RegisterDrivers)   // API version + entry point (extern "C")
```

One `.so` may register several drivers. The library is never unloaded (no `dlclose`).
**Contract tests** for your own driver: `ara/com/mavlink/testing/radio_driver_contract_test.h` (zero callbacks
after `Stop()`, non-blocking `Send`, `kBusy` → `OnTxReady` without a partial write, `Start` after `Stop`, callbacks
from the driver thread); usage examples: `ara/com/mavlink/ut/test_serial_radio.cc` (pty) and
`test_radio_plugin_contract.cc` (a plugin loaded through `dlopen`).

## IPC protocol application ↔ daemon

`ara/com/mavlink/daemon_protocol.h` (+ `daemon_ipc.h`: a `SOCK_SEQPACKET` socket). Protocol version 2 has 7
message types (`RegisterInstance`, `RegisterReply`, `Unregister`, `ElementPayload`, `SendError`, `Delivery`,
`LinkStatus`); event / REQ / RESP are told apart by a `kind` byte. One connection = one application, with many
instances on it (`RegisterInstance` carries a table of elements: msg_id, crc_extra, length, offsets of the
seq/instance/target/status fields, timeout, rate limit). The daemon rejects (with the reason in `RegisterReply`):
a duplicate `(service, instance, role)`, a msg_id taken by another definition/service, an unknown link, a bad
description. Applications exchange **full MAVLink payloads** (the SOME/IP ↔ MAVLink codec stays in the
application). Disconnecting an application (also a crash) cleans up its registrations and queued events; after a
daemon restart the application reconnects and registers all instances again. A method without a ready link fails
at once (`SendError{kNoLink}`); a full REQ FIFO (32 per link) = `kQueueFull`.

Everything the daemon sends to an application (replies, `LinkStatus`, deliveries) goes through a **per-session
queue drained by a writer thread of that session** (limit 256 messages, 500 ms wait for room in the socket
buffer). A slow application therefore delays only its own messages — never the RX thread of a link or the
supervisor thread. An application that stops reading (full queue or send timeout) is disconnected.

## Link behaviour

- Transmit order: HEARTBEAT → REQ/RESP (FIFO; REQs older than the method timeout are dropped) → events
  (coalesced to the latest value, `max_rate_hz`).
- `kBusy` holds exactly the frame that was built (and signed); nothing overtakes it, so signing timestamps grow
  in transmit order; no `OnTxReady` for 5 s = a Warn in the log (the frame stays).
- Fatal driver error: `Stop()`, link status "not ready" for applications, `Start()` again after `restart_delay`;
  the frame held at the moment of the error is lost (the REQ would have expired anyway).
- RX: bytes are copied from `OnReceive` and parsed on a separate thread; a frame reaches an instance only over a
  link listed in its `links`. A frame for an excluded element is rejected and logged as a Warn once per instance.
- Signing: the signature is checked in the parser, but the replay window advances only after the daemon core
  accepted the frame (instance link, target/source, dedup), so a frame for a foreign instance cannot move it. The
  transmit timestamp is `max(now, build date, persisted + 60 s, last_tx + 1, last_peer + 1)`; the build date is
  the floor on a system whose clock is not set (no RTC).

## Tests

```bash
bazel test //platform/common/mavlink_daemon/... //ara/com/mavlink/... //core/serial/...
```

- `code:test_radio_link` — a link on `MemoryRadio` (states, restart, HEARTBEAT, queues, kBusy, signing),
- `code:test_mavlink_daemon_core` — the daemon + IPC clients (registration and conflicts, events/methods in both
  directions, redundancy and dedup, cleanup after disconnecting, slow applications, start errors),
- `code:test_daemon_config` — the config parser.
