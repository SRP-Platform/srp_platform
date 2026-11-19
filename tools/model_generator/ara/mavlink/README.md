# MAVLink generator (`"on": "mavlink"`)

MAVLink is a third way to carry a service next to `ipc` and `udp`. The radio links and signing
belong to the MavlinkDaemon (`platform/common/mavlink_daemon`); an application only
lists the service instances it provides or requires and the daemon links they use.

## What is generated

| Input | Output |
|---|---|
| `app_config.json` of **every application of a CPU** (`on: mavlink` entries), the `service.json` files (`mavlink` blocks) and, optionally, the CPU's `mavlink_daemon_config.json` | `<dialect>.xml`, `<dialect>_links.md`, `<dialect>.lua` (Wireshark), `<dialect>.py` (pymavlink), `<dialect>_monitor.py` (ground monitor, runtime `tools/mavlink/mavlink_monitor.py`) and `<dialect>_monitor.json` (its metadata), one set per CPU |
| `app_config.json` of one application | the C++ model of its instances in `initialization.cc` (`ara/core/model/mavlink_model_item.h`) |

Bazel rule: `ara_mavlink_dialect(name, apps, daemon_config, data, dialect)` in `mavlink.bzl`
(`:example_dialect` uses `testdata/app/`). CLI:
`mavlink_gen <out_dir> --dialect NAME [--daemon-config cfg.json] model.json...`.

An application with `on: mavlink` entries needs `ara_runtime_lib(..., mavlink = True)`, which links
the daemon client.

## Several CPUs

`ara_mavlink_system(name, cpus, daemon_configs, dialect)` (`mavlink.bzl`, tool `mavlink_system_gen`)
merges the dialects of the CPUs of one rocket into one dialect for the station: `cpus` are
`ara_mavlink_dialect` targets, `daemon_configs` the daemon configs in the same order. Outputs
`<dialect>.xml`, `_links.md` (addressing table and the links document of every CPU), `.lua`, `.py`, `_monitor.py` and `_monitor.json` (the monitor tells the CPUs apart by `system_id:component_id`).
Errors (naming the CPUs): the same `msg_id` with different definitions, the same message name with
different ids, the same (`system_id`, `component_id`) in two daemon configs, a config without them;
the same service on several CPUs is one message. Different `system_id`s: a warning (convention: one
`system_id` per rocket, a `component_id` per CPU; see `docs/mavlink/README.md`). CLI:
`mavlink_system_gen <out_dir> --dialect NAME --cpu NAME DIALECT.xml LINKS.md DAEMON_CONFIG ...`.
Test: `test_mavlink_system` (`:example_system` from `testdata/app` and `testdata/cpu2`).

## Model

* **Instance** (`on: mavlink` entry): `instance`, `elements` (the whitelist
  `{"<Element>": {"<link>": {"max_rate_hz": N}}}`: required, non-empty, nothing goes over a radio
  unless listed; the links of the instance are the sum of the links of its elements; `max_rate_hz`
  only for events, 0 or absent = unlimited), `target_system_id`, `target_component_id`,
  `source_system_id` / `source_component_id` (`require` only, 0 = any). Any other key is an
  error; the former `links`, `exclude` and `max_rate_hz` are errors with a hint.
  An application block `mavlink` or `mavlink_links` is an error too. A CPU whose applications
  have no `on: mavlink` entry is an error (nothing to generate).
* **Element** (`service.json`): `"mavlink": {"msg_id"}` for an event, `{"req_id", "resp_id", ...}`
  for a method. No block or `"mavlink": false` keeps the element off MAVLink (listing it in `elements` is an
  error); `max_rate_hz` is not allowed in the block (it is per radio, in `elements`), and neither is
  `timeout_ms` (an error naming the method and `method_timeout_ms`: the timeout of a method is a property
  of the radio, set on the link in the daemon config). The keys allowed on
  an element are `id`, `data_structure`, `desc` and `mavlink`; any other key is an error.
* **Messages**: `SRP_<SERVICE>_<EVENT>` and `SRP_<SERVICE>_<METHOD>_REQ` / `_RESP`. Events start
  with `seq`, `instance`; methods add `target_system`, `target_component`; a response adds
  `status`. Struct members are flattened to `a_b`; a string needs `max_len`.
* The daemon identifies a service by its name: all instances and both roles of a service share
  the `msg_id`s, and a duplicate is rejected per (service, instance, role).

## Checks

Generation fails, naming the application, service and element, on: a `msg_id` or message name
collision (including id 0, which `common.xml` uses for HEARTBEAT; a periodic heartbeat is an
ordinary event with its own `msg_id`), a payload over 255 B, a string without `max_len`, a field
that collides with the header or is a reserved Python name, an instance registered twice for
the same (service, instance, role), a duplicate SOME/IP element id, and unknown keys; also an
unknown element in `elements`, an element without a `mavlink` block and an empty `elements`.
With a daemon config it also fails on an unknown link name (naming the element),
links of one instance with different `signing.enabled` / `accept_unsigned`, and on the bandwidth
of a link: a load above 100% of `air_rate_bps`, an event without `max_rate_hz` on a link with
`air_rate_bps`, a frame larger than the `mtu` of the link (naming element, link and size).
Per link the generator also writes the table of frame sizes and load into `<dialect>_links.md`;
a load above `max_utilization` is a warning.
Every run prints one stderr line per CPU with the same percentages, e.g.
`INFO mavlink budget: radio_a 37.2% (max 80%), radio_b -` (`-` = no `air_rate_bps`); the system
generator prints one such line per CPU (`INFO mavlink budget: CPU <name>: ...`).
The daemon validates its own config when it starts; the generator only reads link names,
signing mode, `air_rate_bps`, `max_utilization` and `mtu` from it.

Warnings: a daemon link no service uses, a **method on more than one radio** (a REQ sent over all
of them is executed once per radio) and `max_rate_hz` that has no effect (a `require` entry; the
value is not emitted).

## Tests

`bazel test //tools/model_generator/ara/mavlink/...`: `test_mavlink_model` (messages, entries,
CPU checks) and `test_mavlink_system` (the system dialect), `test_mavlink_gen` (golden files in `testdata/`, C++ emission, matrix, CLI,
pymavlink round trip). Update the golden files deliberately when the output changes.
