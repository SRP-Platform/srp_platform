# MAVLink in srp_platform: a radio transport for ara::com services

MAVLink is a third way to carry a service next to `ipc` and `udp`: an `"on": "mavlink"` entry in
the application model. The same service (the same skeleton/proxy code) can be offered locally
(IPC/UDP) **and** over a radio.

One platform application per CPU owns the radios: the **MavlinkDaemon**
(`platform/common/mavlink_daemon`, [README](../../platform/common/mavlink_daemon/README.md)). It
holds the links, the radio drivers, the queues and signing. The daemon sends no periodic frame of its own: a periodic
status message is an ordinary event of an application. Applications
with `on: mavlink` are thin clients of the daemon:

```text
 ExampleApp ─┐  registration, events, REQ/RESP                ┌─ driver serial_radio ── UART ── radio A
             ├── unix SOCK_SEQPACKET ── MavlinkDaemon ────────┤
 ExampleApp2 ┘  (SOME/IP BE <-> MAVLink LE codec in the app)  └─ driver linked in statically ── radio B
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
| `"mavlink": false` or no block | the element cannot go over a radio (listing it in `elements` of an application is an error) |

Having a block does not put the element on a radio: that is decided per application and radio in
`app.json` (section 2); the block only gives the message id. A key that is not one of `id`,
`data_structure`, `desc`, `mavlink` is an error, as is an incomplete block. A method without
`out` is fire-and-forget (`req_id` only); with `out` it needs `req_id` and `resp_id`, with no retry.
`max_rate_hz` is **not** a `service.json` key (it is an error with a hint): the rate limit is per
element and radio in `app.json`. Neither is `timeout_ms` (an error with a hint): the timeout of a
method belongs to the radio, `method_timeout_ms` of the link (default 1000, see section 3); a method
on several radios waits for the longest of them. The daemon keeps the
**latest value of each event** (coalescing, not a queue), so a fast series of `Update` calls does
not put every value on the radio; REQ and RESP are queued in order (32 per link). A `string`
needs `max_len` in the block.

## 2. The application and its instances

An application lists **instances only**, see
[`example_app.json`](../../example_app/models/ExampleApp/example_app.json) (provides two
instances) and [`example_app.json`](../../example_app2/models/ExampleApp2/example_app.json)
(requires one). `system_id`, `component_id`, links, drivers and signing belong to the
daemon config. **Nothing goes over a radio unless you list it**: an `on: mavlink` entry has a
whitelist `elements`, element -> radios (link names from the daemon config) -> options:

```json
{"name": "EngineService as Engine", "on": "mavlink", "instance": 2,
 "elements": {
   "Telemetry": {"radio_a": {"max_rate_hz": 10}, "radio_b": {"max_rate_hz": 0.2}},
   "Position":  {"radio_b": {"max_rate_hz": 1}},
   "SetMode":   {"radio_a": {}}}}
```

An element that is not in `elements` does not go over any radio (the generator does not even put
it in the dialect or in `<dialect>_links.md`). One instance serves all of its radios, so a
different scope per radio needs no separate instances. The links of the instance are the sum of
the links of its elements. An event goes out only on **its own** radios, each with its **own**
rate limit; a frame of an element arriving on another radio is dropped. Listing one element on
several radios means **redundancy** for that element: the frame goes out on each of them. The
rocket side does not deduplicate: a copy arriving on each radio is delivered to the application
(the ground station deduplicates by `seq`). A **method on more than one radio** is a generator
warning: a REQ sent over all of them is executed once per radio.

| field | meaning |
|---|---|
| `instance` | unique per (service, instance, role) on the CPU, that is within one daemon |
| `elements` | required, non-empty: `{"<Element>": {"<link>": {"max_rate_hz": N}}}`; the element needs a `mavlink` block in `service.json`; `max_rate_hz` only for events (`0` or absent = unlimited), a method takes `{}` |
| `target_system_id`, `target_component_id` | 0 = broadcast |
| `source_system_id`, `source_component_id` | `require` only, 0 = any |

The former keys `links`, `exclude` and `max_rate_hz` are an error with a hint. In a `require`
entry `max_rate_hz` has no effect (the limit applies to the sender) and is a warning.

Every message carries `seq` and `instance` (methods also `target_system`, `target_component`;
RESP also `status`: 0 ok, 1 method error, 2 unknown instance or a method the instance does not
serve), so **the ground station must use the same `instance` numbers**.

## 3. The daemon config

`mavlink_daemon_config.json` (in srp: `deployment`) holds what applications do not know: the
system and component id, the links with their radio drivers and signing. Its format and the
drivers are described in the [daemon README](../../platform/common/mavlink_daemon/README.md); an
example is [`mavlink_daemon_config.json`](../../test_cpu/cpu/ec/mavlink_daemon_config.json). The
platform knows only driver ids: `serial_radio` (raw MAVLink over a UART) and the drivers that a user
links statically into the daemon binary for a particular modem (`mavlink_daemon_binary(drivers = [...])`). The generator reads only link names and
signing mode from the config, to check them against the applications; the daemon validates the
rest when it starts. A link also has `method_timeout_ms` (default 1000): how long a REQ may wait
on that radio and how long a caller waits for the RESP; give a slow radio a larger value, e.g. 4000.

## 4. What goes over which radio

The generator writes the element x link matrix of the whole CPU to `<dialect>_links.md`. One row
of the example (`//test_cpu/cpu/ec:dialect`) shows its shape:

```text
| element | radio_a | radio_b |
| srp.example.EngineService.Telemetry (event) | ExampleApp.2 @ 2 Hz, ExampleApp.3 @ 1 Hz, ExampleApp2.2 (proxy) | ExampleApp.2 @ 2 Hz, ExampleApp2.2 (proxy) |
```

A cell is `application.instance` with the event rate limit of that radio, `(proxy)` marks a
`require` entry and `-` a link that does not carry the element. The rate limit applies to the
sender, so a `(proxy)` entry carries no rate. Only elements listed in `elements` of some instance
appear in the matrix.

### Bandwidth budget and `mtu`

A radio is slow: 2400 bit/s is 300 B/s. Give a link in the daemon config its `air_rate_bps` (and
optionally `max_utilization`, default 0.8, and `mtu` in bytes) and the generator adds a
**Bandwidth budget** section to `<dialect>_links.md`: per link a table of every (element, instance)
the CPU transmits, with the frame size, the `max_rate_hz`, the load in B/s and its share of
`air_rate_bps`.

* **Frame size** = header 10 + CRC 2 + signature 13 (on a signed link) + the full payload (the
  worst case). **Load** = frame x `max_rate_hz`, summed over the events the CPU provides.
  Methods have no frequency: their REQ / RESP frame sizes are listed, not summed.
  Events the CPU only receives (`require`) are not its load.
* **Warning** when the sum exceeds `max_utilization`; **error** above 100% of `air_rate_bps`
  (naming the link and the percent).
* The generator prints one line per CPU to stderr in every build, e.g.
  `INFO mavlink budget: radio_a 37.2% (max 80%), radio_b -` (`-` = link without `air_rate_bps`);
  `ara_mavlink_system` prints one per CPU (`INFO mavlink budget: CPU <name>: ...`).
* An event on a link with `air_rate_bps` needs `max_rate_hz > 0` in `elements`, otherwise its load
  cannot be computed: an error.
* A frame (of any element on the link: REQ, RESP or event) larger than the `mtu` of the link is an
  error naming the element, the link and the size.
* **Without `air_rate_bps` and `mtu` nothing changes**: no section, no budget.

At run time the daemon holds the transmission of a link with `air_rate_bps` to
`air_rate_bps x max_utilization`: a frame waits in the daemon until its share of the air time is
free, so an event updated faster than the radio can send is coalesced to its latest value (not
queued) and a REQ that waited longer than the `method_timeout_ms` of the link is dropped, also with a plain
`serial_radio`. See the [daemon README](../../platform/common/mavlink_daemon/README.md).

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

### Radio link statistics: the `MavlinkLinkService`

The daemon offers the platform service `srp.platform.mavlink.MavlinkLinkService`
([`platform_definition/mavlink_link_service`](../../platform_definition/mavlink_link_service)) **on `ipc`
only** (never on `udp`, and it is not a MAVLink service): the radio state of every link, for an
application that wants to adapt to the radio (rate, mode). It carries

* the event `LinkStats`, one per link every `stats_period_ms` of the daemon config (default 1000); the links are published
  one after another, `stats_period_ms / links` apart, because the `ipc` event keeps only the latest value:
  `link_index` (position of the link in the daemon config FILE, not alphabetical), `link_name` (up to 16 characters, packed in two `uint64`,
  `PackLinkName` / `UnpackLinkName` in `link_name.h`), `ready`, `age_ms` (age of the last radio
  measurement; it keeps growing when the driver stopped reporting, the last values stay), `valid` (mask: bit 0 RSSI, bit 1 noise floor, bit 2 SNR), `rssi_dbm`,
  `noise_floor_dbm`, `snr_db`, the daemon's counters `rx_ok`, `rx_crc_err`, `rx_sig_err`,
  `tx_frames`, `tx_busy`, `tx_dropped`, `tx_expired` and `budget_pct` (0 until the radio budget exists);
* the method `GetLinkStats(link)`: the same structure for one link; an unknown name is an error.

The radio measurements come from the driver (`IRadioListener::OnLinkStats`); a driver that reports
nothing is correct and the service then shows `valid` = 0 with the daemon's counters.

An application uses it like any platform service, with a `require` on `ipc`:

```json
"require": [{"name": "srp.platform.mavlink.MavlinkLinkService as LinkRPort", "on": "ipc", "instance": 1}]
```

with `"@(srp_platform)/platform_definition/mavlink_link_service/mavlink_link_service.json"` in the
`include` of the application model and `srp/platform/mavlink/MavlinkLinkService/MavlinkLinkServiceHandler.h`
(`MavlinkLinkServiceProxy`) in the code; a complete client is
[`tests/mavlink/link_stats_app`](../../tests/mavlink/link_stats_app). `ipc` is local to a CPU (the
daemon and its clients share the CPU), so an application sees only the links of the daemon of its own CPU.

## 6. Signing (MAVLink 2)

Signing belongs to the **link in the daemon config** (`links.<name>.signing`).

* **Key:** 32 bytes as 64 hex characters in the file named by `signing.key_file`. Create it with
  `bazel run //tools/mavlink:keygen -- /etc/srp/mavlink/radio_a.key`; the ground station needs
  the same file. **A key never goes into the repository.** The daemon logs a warning when the file is
  readable by group or others (wider than `0600`); fix it with `chmod 600`.
* `link_id` differs per link, and the links of one instance need
  the same `signing.enabled` and `accept_unsigned`.
* **`state_file`:** the timestamp (units of 10 us since 2015-01-01) and the last timestamp seen
  from the peer are saved at most every 10 s and on stop. After a restart the first timestamp is
  `max(now, persisted + 60 s, last_valid_peer_ts + 1, last_tx + 1)`; the +60 s floor applies
  once. A missing file only logs a warning and signing runs without persistence. The BeagleBone
  has no RTC: with a clock before 2020 and no state the sender starts from zero.
* **Limit:** after a restart a *new* stream (sysid, compid, link_id) is refused when
  `ts + 60 s < max(last_peer, last_tx)`. A ground station whose clock went backwards after the
  restart is refused (`rx_sig_err`) until its timestamps pass the threshold: synchronize its
  clock. A replay inside a known stream is always refused. A frame whose msg_id the daemon does
  not know (also id 0) is dropped before it can establish a stream, so the ground station should
  send one of the application requests or events early.
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

### Ground monitor

The same build generates `<dialect>_monitor.py` (and `<dialect>_monitor.json`, the model of the
CPU: events with their rate limits, methods with their REQ/RESP pairs, channel parameters).
`ara_mavlink_dialect` and `ara_mavlink_system` also create the target `<name>_monitor`, so the
monitor is started like `//tools/uds:main`, with `bazel run` (arguments after `--`):

```sh
bazel run //test_cpu/cpu/ec:dialect_monitor -- --serial /dev/ttyUSB0 --baud 57600
bazel run //test_cpu/cpu/ec:dialect_monitor -- --udp 14550 --key key.hex --link radio_a
bazel run //test_cpu/cpu/ec:system_monitor -- --udp 14550    # the station: all CPUs of the rocket
```

`:dialect_monitor` is one CPU, `:system_monitor` the dialect of all CPUs (see "Several CPUs").
`bazel run` starts in the runfiles directory: give `--key` and `--daemon-config` an absolute path
(or run with `--run_under="cd $PWD &&"`). Alternatively build the dialect and start the script by hand (it needs
`pymavlink` and finds the runtime `tools/mavlink/mavlink_monitor.py` in the srp_platform tree or
in `$SRP_MAVLINK_TOOLS`):

```sh
bazel build //test_cpu/cpu/ec:dialect
python3 bazel-bin/test_cpu/cpu/ec/srp_example_monitor.py --udp 14550
```

The monitor reads a UART or a UDP port and shows, per message: events with the measured and the
configured frequency, methods with REQ/RESP counts, and channel errors (CRC, signature, unknown
message id, duplicate sequence numbers).

The default view is a curses table; `--plain` prints a text table every second and `--jsonl`
one JSON object per second. `--key` verifies signatures, `--link` selects the radio link of the
model (limits, `air_rate_bps`), `--daemon-config` takes `method_timeout_ms` (the REQ timeout) from the daemon config,
`--sysid`/`--compid` filter frames and `--duration S` stops after S seconds.

### Several CPUs (one rocket, a radio on more than one computer)

The radio is physically on one CPU, but the stack works with a radio daemon on several CPUs of
one rocket. The convention:

* **one `system_id` per rocket, a separate `component_id` per CPU** in `mavlink_daemon_config.json`
  of each CPU (the daemon puts both in every frame it sends). The station tells the CPUs apart by
  the `compid` of a frame.
* **a request from the station addresses a CPU with `target_system` / `target_component`** (the
  ids of that CPU's daemon config). A daemon serves only a request addressed to its own
  `component_id` (or 0 = any CPU: then every CPU that has the method executes it, once per link, as for any REQ over several links).
* **`MavlinkLinkService` is local to the CPU** (it is offered on `ipc`): an application sees the
  radios of its own CPU only. The statistics of another CPU's radio reach it through a relay
  application on that CPU or an event of its own.
* **`msg_id` is global for the system.** The same service on two CPUs (the same definition) uses
  the same `msg_id` and is one message in the station dialect; two different services must not
  share a `msg_id`.

`ara_mavlink_system(name, cpus, daemon_configs, dialect)` merges the dialects of the CPUs into **one**
dialect for the station (`<dialect>.xml`, `_links.md`, `.lua`, `.py`; `_links.md` has the addressing
table and the matrix of every CPU). `cpus` are the `ara_mavlink_dialect` targets, `daemon_configs`
the daemon configs in the same order. The generation fails when two CPUs use one `msg_id` for
different messages (the error names both CPUs and messages), when one message name has two ids and
when two daemon configs have the same (`system_id`, `component_id`); different `system_id`s are a
warning. Example: `//test_cpu/cpu/ec:system` (two CPUs, `component_id` 1 and 2); the end-to-end test with
two daemons on one machine: `//tests/mavlink:e2e_two_cpus_test`.

## 8. Limits and checks

The limits (255 B payload, collisions between applications, reserved
field names) and the checks the generator runs are listed in the
[generator README](../../tools/model_generator/ara/mavlink/README.md).

## 9. Tests

`bazel test //tests/mavlink/...` (daemon, applications and a ground station on pymavlink over
ptys; see [`tests/mavlink/README.md`](../../tests/mavlink/README.md)),
`bazel test //tools/mavlink/...` (`keygen`, `serial2udp`) and
`bazel test //tools/model_generator/ara/mavlink/...` (the generator).

## 10. Writing and linking a radio driver

The platform knows no modem. A radio driver is a class that the daemon opens for a link; the working
example is [`ara/com/mavlink/examples/udp_radio`](../../ara/com/mavlink/examples/udp_radio/README.md)
(`example_udp_radio`: one MAVLink frame = one UDP datagram).

1. **Where it lives.** A real driver (a modem such as the E220) lives in the integrator repository
   (srp), not in `srp_platform`; the platform ships only `serial_radio` and this example. In srp you
   can also use `core/radio/common` (`RadioDriver` + `IRadioModem`) and implement only the modem.
2. **The class.** Implement `ara::com::mavlink::IRadioDriver` (`ara/com/mavlink/radio_driver.h`; the
   header states the contract): `Start(listener, stop_token)`, `Stop()` (join; zero callbacks after
   it returns), a non-blocking `Send(frame)` (whole frame or `kBusy`; after `kBusy` call `OnTxReady()`
   once when room appears), `Name()`. The driver thread waits on `poll()` and calls
   `OnReceive(bytes)` / `OnTxReady()` / `OnError(e, fatal)`.
3. **Registration.** In a `.cc` file of the driver library, with `alwayslink = True`:

   ```cpp
   SRP_MAVLINK_REGISTER_DRIVER("example_udp_radio", [](const std::string& config_path) {
     return std::shared_ptr<ara::com::mavlink::IRadioDriver>{std::make_shared<UdpRadio>(config_path)};
   });
   ```

   ```python
   cc_library(name = "example_udp_radio", srcs = ["udp_radio.cc"], hdrs = ["udp_radio.h"],
              deps = ["@srp_platform//ara/com/mavlink:radio_driver", ...], alwayslink = True)
   ```

4. **Where to link it.** Add the library to `drivers` of `mavlink_daemon_binary(...)` in the BUILD
   file that builds the daemon of your deployment (srp: `deployment/platform/mavlink_daemon/BUILD`;
   here: `ara/com/mavlink/examples/udp_radio/BUILD`):

   ```python
   load("@srp_platform//platform/common/mavlink_daemon:defs.bzl", "mavlink_daemon_binary")
   mavlink_daemon_binary(name = "example_udp_daemon", drivers = [":example_udp_radio"])
   ```

   No `main()` is written; `serial_radio` is always linked in. Two drivers with one id make the
   daemon refuse to start (the error names the id).
5. **Daemon config.** `links.<name>.driver` is the registered id and `driver_config` the path of the
   file that only your driver reads:

   ```json
   { "links": { "udp_ground": { "driver": "example_udp_radio",
                                "driver_config": "/etc/srp/mavlink/udp_ground.json" } } }
   ```

   with `udp_ground.json` = `{"bind_port": 14560, "peer_host": "127.0.0.1", "peer_port": 14550}`
   (a ready pair: `example_udp_daemon_config.json`, `udp_ground.json` next to the BUILD file).
6. **Tests.** Run the shared suite `ara/com/mavlink/testing/radio_driver_contract_test.h` on your
   driver: the harness supplies the driver and "the other side of the air" (see
   `udp_radio_contract_test.cc`). End-to-end: `//tests/mavlink:e2e_udp_radio_test` (the daemon
   `example_udp_daemon` and pymavlink over UDP) and `//tests/mavlink:udp_conformance_test`.
7. **Frame size and statistics.** Override `MaxFrameSize()` when the radio has a frame limit: the
   largest whole MAVLink frame `Send()` accepts (the example returns 280; the default 0 = no limit;
   `Send()` of a larger frame is an error). Set `mtu` of the link in the daemon config to the same
   value (or lower): the generator then rejects an element whose frame would not fit (see
   "Bandwidth budget and `mtu`"). A radio that measures the signal can call
   `listener.OnLinkStats({valid, rssi_dbm, ...})` from its thread; the values reach
   `MavlinkLinkService` (a driver that reports nothing publishes `valid` = 0).
