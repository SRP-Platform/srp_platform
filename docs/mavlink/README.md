# MAVLink in srp_platform: a radio transport for ara::com services

MAVLink is a third way to carry a service next to `ipc` and `udp`: an `"on": "mavlink"` entry in
the application model. The same service (the same skeleton/proxy code) can be offered locally
(IPC/UDP) **and** over a radio.

One platform application per CPU owns the radios: the **MavlinkDaemon**
(`platform/common/mavlink_daemon`, [README](../../platform/common/mavlink_daemon/README.md)). It
holds the links, the radio drivers, the queues, signing, HEARTBEAT and deduplication. Applications
with `on: mavlink` are thin clients of the daemon:

```text
 ExampleApp ─┐  registration, events, REQ/RESP                ┌─ driver serial_radio ── UART ── radio A
             ├── unix SOCK_SEQPACKET ── MavlinkDaemon ────────┤
 ExampleApp2 ┘  (SOME/IP BE <-> MAVLink LE codec in the app)  └─ driver from a .so plugin ── radio B
```

Several applications can therefore share one radio. The generator produces the MAVLink dialect
(XML, Wireshark Lua, pymavlink module) **for the whole CPU**, from all its applications with
`on: mavlink`, and the model of the instances of each application
([generator README](../../tools/model_generator/ara/mavlink/README.md)).

A working example: `example_app` and `example_app2` on one daemon of the CPU `test_cpu/cpu/ec`
(service `EngineService`, links `radio_a` and `radio_b`); its end-to-end test is
`bazel test //tests/mavlink:e2e_examples_test`. `example_app2` has no service definitions of its
own on purpose: it is the client of the `EngineService` that `example_app` offers, and one CPU
reads one definition per service.

## 1. The service (`service.json`)

Every event and method has an **explicit MAVLink id** in a `mavlink` block, see
[`engine_service.json`](../../example_app/models/ExampleApp/engine_service.json):

| entry | meaning |
|---|---|
| `"mavlink": {"msg_id": 42100}` (event), `{"req_id": ..., "resp_id": ...}` (method) | the element goes over the radio |
| `"mavlink": false` or no block | the element stays off the radio; the generator lists such elements in `<dialect>_links.md`, so an omission is visible |

A key that is not one of `id`, `data_structure`, `desc`, `mavlink` is an error, as is an
incomplete block. A method without `out` is fire-and-forget (`req_id` only); with `out` it needs
`req_id`, `resp_id` and takes `timeout_ms` (default 1000) with no retry. `max_rate_hz` on an event
is the service's limit: the daemon keeps the **latest value of each event** (coalescing, not a
queue), so a fast series of `Update` calls does not put every value on the radio; REQ and RESP
are queued in order (32 per link). A `string` needs `max_len` in the block.

## 2. The application and its instances

An application lists **instances only**, see
[`example_app.json`](../../example_app/models/ExampleApp/example_app.json) (provides two
instances) and [`example_app.json`](../../example_app2/models/ExampleApp2/example_app.json)
(requires one). `system_id`, `component_id`, links, drivers, signing and HEARTBEAT belong to the
daemon config. An entry picks links by **name from the daemon config** with `links`; several
links mean **redundancy**: every frame goes out on all of them and the receiver drops duplicates
(window `dedup_ttl_ms`, default 10000; a duplicate has the same `seq` and payload CRC32).
**A different scope per radio means separate instances** of the service with `exclude` and
`max_rate_hz`.

| field | meaning |
|---|---|
| `instance` | unique per (service, instance, role) on the CPU, that is within one daemon |
| `links` | at least one link name of the daemon config |
| `exclude` | elements disabled in this instance; an unknown name is an error |
| `max_rate_hz` | event rate override for this instance, `0` = unlimited; a method here is an error |
| `dedup_ttl_ms` | deduplication window |
| `target_system_id`, `target_component_id` | 0 = broadcast |
| `source_system_id`, `source_component_id` | `require` only, 0 = any |

Every message carries `seq` and `instance` (methods also `target_system`, `target_component`;
RESP also `status`: 0 ok, 1 method error, 2 excluded method or unknown instance), so **the
ground station must use the same `instance` numbers and the same exclusions**.

## 3. The daemon config

`mavlink_daemon_config.json` (in srp: `deployment`) holds what applications do not know: the
system and component id, the links with their radio drivers and signing. Its format and the
drivers are described in the [daemon README](../../platform/common/mavlink_daemon/README.md); an
example is [`mavlink_daemon_config.json`](../../test_cpu/cpu/ec/mavlink_daemon_config.json). The
platform knows only driver ids: the built-in `serial_radio` (raw MAVLink over a UART) and plugins
(`.so`) that a user adds for a particular modem. The generator reads only link names, versions and
signing mode from the config, to check them against the applications; the daemon validates the
rest when it starts.

## 4. What goes over which radio

The generator writes the element x link matrix of the whole CPU to `<dialect>_links.md`. One row
of the example (`//test_cpu/cpu/ec:dialect`) shows its shape:

```text
| element | radio_a | radio_b |
| srp.example.EngineService.Telemetry (event) | ExampleApp.2 @ 2 Hz, ExampleApp.3 @ 1 Hz, ExampleApp2.2 (proxy) | ExampleApp.2 @ 2 Hz, ExampleApp2.2 (proxy) |
```

A cell is `application.instance` with the event rate limit, `(proxy)` marks a `require` entry and
`-` a link that does not carry the element. The rate limit applies to the sender, so a `(proxy)`
entry carries no rate. Below the matrix the file lists the elements that are
off the radio and the exclusions per instance.

## 5. The application as a daemon client

* **Registration:** `StartOffer()` / `StartFind()` connects to the daemon socket
  (`SRP_MAVLINK_DAEMON_SOCKET`, default `/run/srp_mavlink_daemon.sock`) and registers the instance
  (role, `instance`, links, filters, the element table with `msg_id`, `crc_extra` and the header
  layout). A **conflict** (the same service, instance and role, or the same `msg_id` with another
  definition), an unknown link or a wrong protocol version rejects the registration: the instance
  stays unavailable and the application logs why.
* The SOME/IP (big endian) to MAVLink (little endian) **codec** stays in the application; the
  daemon receives whole MAVLink payloads.
* **Without the daemon or a ready link** a method call fails at once (`kCommunicationLinkError`)
  and an event `Update()` sends nothing.
* **Restart of the daemon:** the socket breaks, calls in flight fail, and the application
  reconnects and registers everything again within 5 s of the daemon's start. **Restart of an
  application:** the daemon sees the socket close and removes its registrations.

## 6. Signing (MAVLink 2)

Signing belongs to the **link in the daemon config** (`links.<name>.signing`).

* **Key:** 32 bytes as 64 hex characters in the file named by `signing.key_file`. Create it with
  `bazel run //tools/mavlink:keygen -- /etc/srp/mavlink/radio_a.key`; the ground station needs
  the same file. **A key never goes into the repository.**
* `link_id` differs per link, v1 and signing do not combine, and the links of one instance need
  the same `signing.enabled` and `accept_unsigned`.
* **`state_file`:** the timestamp (units of 10 us since 2015-01-01) and the last timestamp seen
  from the peer are saved at most every 10 s and on stop. After a restart the first timestamp is
  `max(now, persisted + 60 s, last_valid_peer_ts + 1, last_tx + 1)`; the +60 s floor applies
  once. A missing file only logs a warning and signing runs without persistence. The BeagleBone
  has no RTC: with a clock before 2020 and no state the sender starts from zero.
* **Limit:** after a restart a *new* stream (sysid, compid, link_id) is refused when
  `ts + 60 s < max(last_peer, last_tx)`. A ground station whose clock went backwards after the
  restart is refused (`rx_sig_err`) until its timestamps pass the threshold: synchronize its
  clock. A replay inside a known stream is always refused. The ground station should send a
  signed HEARTBEAT, which QGroundControl and MAVProxy do.
* `accept_unsigned: true` lets unsigned frames through; by default they are refused.

## 7. Artifacts for the ground station and Wireshark

`ara_mavlink_dialect(name, apps, daemon_config, data, dialect)` generates, per CPU,
`<dialect>.xml`, `<dialect>_links.md`, `<dialect>.lua` (Wireshark) and `<dialect>.py` (pymavlink).
`apps` are the models of **all** applications of the CPU with `on: mavlink`. Example:
`//test_cpu/cpu/ec:dialect`. Using the pymavlink module:

```python
import serial
import srp_example  # the generated <dialect>.py, on PYTHONPATH

port = serial.Serial("/dev/ttyUSB0", 57600)
mav = srp_example.MAVLink(port)
mav.robust_parsing = True
while True:
    for msg in mav.parse_buffer(port.read(256)) or []:
        if msg.get_type() == "SRP_ENGINE_SERVICE_TELEMETRY":
            print(msg.instance, msg.rpm, msg.temp)
```

`mavutil.mavlink_connection(..., dialect="srp_example")` loads `pymavlink.dialects.v20.srp_example`,
not a local file; copy the generated module there to use it, or use the code above.

For live Wireshark, `serial2udp` parses the UART stream with the generated module and sends **one
frame per datagram** to UDP 14550:

```sh
bazel run //tools/mavlink:serial2udp -- /dev/ttyUSB0 srp_example.py --baud 57600
wireshark -k -i lo -f "udp port 14550" -X lua_script:srp_example.lua
```

## 8. Limits and checks

The limits (255 B payload, ids up to 255 on a v1 link, collisions between applications, reserved
field names) and the checks the generator runs are listed in the
[generator README](../../tools/model_generator/ara/mavlink/README.md).

## 9. Tests

`bazel test //tests/mavlink/...` (daemon, applications and a ground station on pymavlink over
ptys; see [`tests/mavlink/README.md`](../../tests/mavlink/README.md)),
`bazel test //tools/mavlink/...` (`keygen`, `serial2udp`) and
`bazel test //tools/model_generator/ara/mavlink/...` (the generator).
