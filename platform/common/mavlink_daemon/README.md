# MavlinkDaemon

One platform application per CPU that **owns the MAVLink links** and serves many `on: mavlink`
applications at once (modelled on `someip_demon`). The daemon provides: links to radios, queues and
priorities, v2 signing, and the `crc_extra` table built from
application registrations. Applications are thin bindings — IPC clients of the daemon (they register service
instances and pass messages through).

```
 app A ──┐                         ┌── radio driver (linked in statically) ── air
 app B ──┼─ SEQPACKET socket ──  daemon ── links "lora" / "radio_b" / … (queues, signing)
 app C ──┘   (daemon_protocol.h)    └── radio driver …
```

## Configuration (`mavlink_daemon_config.json`, in srp `deployment`)

```json
{
  "system_id": 1,
  "component_id": 1,
  "socket_path": "/run/srp_mavlink_daemon.sock",
  "socket_mode": "0660",
  "restart_backoff": { "min_ms": 500, "max_ms": 30000 },
  "links": {
    "lora": {
      "driver": "ebyte_e220",
      "driver_config": "/etc/srp/mavlink/e220.json",
      "signing": { "enabled": true, "key_file": "/etc/srp/mavlink/lora.key", "link_id": 1,
                   "state_file": "/var/lib/srp/mavlink/lora.state", "accept_unsigned": false }
    },
    "radio_b": { "driver": "serial_radio", "driver_config": "/etc/srp/mavlink/radio_b_serial.json",
                 "air_rate_bps": 2400, "max_utilization": 0.8, "mtu": 100 }
  }
}
```

| field | meaning |
|-------|---------|
| `system_id`, `component_id` | identity of the CPU in MAVLink (1..255); **one `component_id` per CPU**, used on every link |
| `socket_path`, `socket_mode` | unix socket for applications (default `/run/srp_mavlink_daemon.sock`, mode `0660`) |
| `restart_backoff` | pause before `Start()` of a driver again after a fatal error or a failed start: `min_ms` (default 500) after the first failure, doubled after every further one up to `max_ms` (default 30000); back to `min_ms` after a start that lasted 10 s. The old `restart_delay_ms` is a config error |
| `stats_period_ms` | period of the `LinkStats` event of `MavlinkLinkService` (ipc only), 10..3600000, default 1000; `stats_period_ms / links` must be >= 50 (start error otherwise) |
| `links.<name>.driver` | id of a driver linked into the daemon binary (`serial_radio` is always there) |
| `links.<name>.driver_config` | path to the **driver's own file** — only the driver knows its format |
| `links.<name>.air_rate_bps` | optional: the air rate of the radio in bit/s (1..4294967295). **Without it there is no budget and no TX pacing** (as before) |
| `links.<name>.max_utilization` | optional, `(0, 1]`, default `0.8`: the share of `air_rate_bps` the daemon may use; TX pacing runs at `air_rate_bps x max_utilization` (only with `air_rate_bps`) |
| `links.<name>.mtu` | optional, 12..280 B: the largest frame the radio takes. The generator fails when a frame of an element on the link is larger; the daemon only stores it (the check against the driver comes with `MaxFrameSize()` of the driver API) |
| `links.<name>.method_timeout_ms` | optional, default `1000`: how long a REQ may wait on this link (in the queue and for the TX pacing) before it is dropped (`tx_expired`) **and** how long the caller waits for the RESP of a method that uses the link. A method on several radios waits for the **longest** of them; the daemon returns that effective timeout per method in `RegisterReply` (the application has no timeout of its own) |
| `links.<name>.signing` | v2 signing |

The config is **strict**: an unknown key (root, link or `signing`) is a start error naming the key and the link
(`heartbeat_Hz`, a typo, is not silently ignored). Keys of earlier versions (`version`, `heartbeat_hz`,
`dedup_ttl_ms`, ...) get a hint on what replaced them. The known keys are the constants `kRootKeys`,
`kLinkKeys`, `kSigningKeys` and the hint table `kRemovedKeys` in `daemon_config.cc`.

The daemon also offers `MavlinkLinkService` on `ipc` (the `LinkStats` event per link every `stats_period_ms`
and `GetLinkStats(link)`); see [docs/mavlink/README.md](../../docs/mavlink/README.md#radio-link-statistics-the-mavlinklinkservice).
The links are published one at a time, one per `stats_period_ms / links` (the `ipc` event is a single slot that a back-to-back
second event would overwrite), so keep `stats_period_ms / links` well above the wake-up latency of the receivers (>= 50 ms).

Only MAVLink v2 is sent and accepted: a v1 frame (STX 0xFE) with a valid CRC is dropped and counted in
`rx_dropped`.

The platform knows no modem or medium — link names and drivers are added by the integrator (srp), see **Radio drivers**.

### Start errors (the daemon starts nothing; one message + an `Error` log)

- a **duplicate driver id** (two libraries of the binary register the same id; the message names the id),
- a driver used by a link is missing (the message names the link, the id and the known drivers),
- an unusable IPC socket, config errors (all at once, naming the link), among them:
  a **repeated `signing.link_id`**
  on signed links (a receiver merging redundant radios keeps one timestamp state per (link_id, sysid, compid),
  so copies from the second radio would be rejected as replay) and the **same non-empty `signing.state_file`**
  on two links (two signers would overwrite one file),
- a **live daemon at `socket_path`**: the daemon holds an exclusive `flock` on `<socket_path>.lock` for its
  whole life; a locked file = error "daemon already running" (a stale socket of a killed process is replaced,
  since the kernel drops the lock with its owner).

`Start()` after `Stop()` on the same object starts the daemon again.

## Radio drivers

API: `ara/com/mavlink/radio_driver.h` (`IRadioDriver`, `IRadioListener`; the contract is described in the header).
A driver has its own thread (e.g. `poll()` on AUX/UART) and `Send()` is non-blocking: `kBusy` = nothing accepted,
the daemon holds that frame until `OnTxReady()`; `OnError(fatal)` → the daemon calls `Stop()` and, after
the restart backoff, `Start()`.

**`serial_radio`** — raw bytes over UART, no AT commands or modem configuration; linked into every daemon
binary. Driver file:

```json
{ "device": "/dev/ttyS2", "baudrate": 57600 }
```

**Drivers are linked statically.** A driver is a `cc_library` with `alwayslink = True` whose `.cc` file registers a
factory from a static initializer:

```cpp
#include "ara/com/mavlink/radio_driver_registry.h"
SRP_MAVLINK_REGISTER_DRIVER("ebyte_e220", [](const std::string& config_path) {
  return std::shared_ptr<ara::com::mavlink::IRadioDriver>{std::make_shared<E220Driver>(config_path)};
});
```

`alwayslink` is required: without it the linker drops the unreferenced object file and the driver is missing
(the start error "unknown driver" lists the known ids). The daemon binary is built by a rule — in srp the user
writes only a BUILD entry, no `main()`:

```python
load("@srp_platform//platform/common/mavlink_daemon:defs.bzl", "mavlink_daemon_binary")
mavlink_daemon_binary(name = "mavlink_daemon", drivers = ["//core/radio/e220:e220_driver"])
```

The rule generates `main()` (the platform application under Execution Management) and links the daemon library,
`serial_radio` and the given drivers. `host = True` builds the same core without Execution Management,
controlled through JSON lines (for tests on a PC, see `tests/mavlink`). The platform's own `mavlink_daemon` is
this rule with `serial_radio` only. A driver id registered twice in one binary is a daemon start error naming the id.

**Contract tests** for your own driver: `ara/com/mavlink/testing/radio_driver_contract_test.h` (zero callbacks
after `Stop()`, non-blocking `Send`, `kBusy` → `OnTxReady` without a partial write, `Start` after `Stop`, callbacks
from the driver thread); usage examples: `ara/com/mavlink/ut/test_serial_radio.cc` (pty) and
`test_registered_driver_contract.cc` (a driver created through the registry, as the daemon does it).

## IPC protocol application ↔ daemon

`ara/com/mavlink/daemon_protocol.h` (+ `daemon_ipc.h`: a `SOCK_SEQPACKET` socket). Protocol version 3 has 7
message types (`RegisterInstance`, `RegisterReply`, `Unregister`, `ElementPayload`, `SendError`, `Delivery`,
`LinkStatus`); event / REQ / RESP are told apart by a `kind` byte. One connection = one application, with many
instances on it (`RegisterInstance` carries a table of elements: msg_id, crc_extra, length, offsets of the
seq/instance/target/status fields, links with a rate limit). The daemon rejects (with the reason in `RegisterReply`):
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

- Transmit order: REQ/RESP (FIFO; REQs older than the `method_timeout_ms` of the link are dropped) → events
  (coalesced to the latest value, at most one per `max_rate_hz` of the element **on that link**).
- **TX pacing** (only a link with `air_rate_bps`): a token bucket of one frame, refilled at
  `air_rate_bps x max_utilization / 8` bytes per second. A frame (REQ, RESP or event) leaves when the budget time
  of the previous one has passed; the time a frame takes is its full size: header 10 + CRC 2 + signature 13 on a
  signed link + the full payload (the worst case; the v2 truncation of trailing zeros is not counted). After an
  idle time the first frame goes at once and the next waits a whole frame time after it (a burst is one frame:
  never two frames back to back). Until then the
  frame **waits in the daemon**, not in a UART or modem buffer, so event coalescing (the latest value) and the
  expiry of a REQ work with a slow radio too; a REQ whose `method_timeout_ms` ends in the wait is dropped
  (`tx_expired`).
- `kBusy` holds exactly the frame that was built (and signed); nothing overtakes it, so signing timestamps grow
  in transmit order; no `OnTxReady` for 5 s = a Warn in the log (the frame stays).
- Fatal driver error: `Stop()`, link status "not ready" for applications, `Start()` again after the restart backoff (500 ms, doubling to 30 s on repeated failures);
  the frame held at the moment of the error is lost (the REQ would have expired anyway).
- RX: bytes are copied from `OnReceive` and parsed on a separate thread; a frame reaches an instance only over a
  link listed for its **element** (`ElementDesc::links`; an element listed for radio B is dropped when it arrives on
  radio A). Per-element TX is the same: an event, REQ or RESP goes out only on the links of its element, an event
  with that link's own rate limit; a REQ needs a ready link **of its method** (`kNoLink` otherwise).
- Signing: the signature is checked in the parser, but the replay window advances only after the daemon core
  accepted the frame (instance link, target/source), so a frame for a foreign instance cannot move it. The
  exception is a REQ for an unknown instance that the daemon answered with RESP
  `status=2`: it counts as accepted, so a replayed recording of it gets no further RESP (only the first one
  costs airtime; on a link without signing there is no replay protection at all). The
  transmit timestamp is `max(now, build date, persisted + 60 s, last_tx + 1, last_peer + 1)`; the build date is
  the floor on a system whose clock is not set (no RTC).

## Tests

```bash
bazel test //platform/common/mavlink_daemon/... //ara/com/mavlink/... //core/serial/...
```

- `code:test_radio_link` — a link on `MemoryRadio` (states, restart and its backoff on a test clock, queues, kBusy, signing),
- `code:test_mavlink_daemon_core` — the daemon + IPC clients (registration and conflicts, events/methods in both
  directions, redundancy (no dedup), cleanup after disconnecting, slow applications, start errors),
- `code:test_daemon_config` — the config parser.
