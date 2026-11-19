# MAVLink generator (`"on": "mavlink"`)

MAVLink is a third way to carry a service next to `ipc` and `udp`. The radio links, signing and
HEARTBEAT belong to the MavlinkDaemon (`platform/common/mavlink_daemon`); an application only
lists the service instances it provides or requires and the daemon links they use.

## What is generated

| Input | Output |
|---|---|
| `app_config.json` of **every application of a CPU** (`on: mavlink` entries), the `service.json` files (`mavlink` blocks) and, optionally, the CPU's `mavlink_daemon_config.json` | `<dialect>.xml`, `<dialect>_links.md`, `<dialect>.lua` (Wireshark), `<dialect>.py` (pymavlink), one set per CPU |
| `app_config.json` of one application | the C++ model of its instances in `initialization.cc` (`ara/core/model/mavlink_model_item.h`) |

Bazel rule: `ara_mavlink_dialect(name, apps, daemon_config, data, dialect)` in `mavlink.bzl`
(`:example_dialect` uses `testdata/app/`). CLI:
`mavlink_gen <out_dir> --dialect NAME [--daemon-config cfg.json] model.json...`.

An application with `on: mavlink` entries needs `ara_runtime_lib(..., mavlink = True)`, which links
the daemon client.

## Model

* **Instance** (`on: mavlink` entry): `instance`, `links` (at least one name from the daemon
  config; several names mean redundancy), `target_system_id`, `target_component_id`,
  `source_system_id` / `source_component_id` (`require` only, 0 = any), `dedup_ttl_ms`
  (default 10000), `exclude` (elements disabled in this instance), `max_rate_hz` (event rate
  overrides, 0 = unlimited). Any other key is an error.
  An application block `mavlink` or `mavlink_links` is an error too. A CPU whose applications
  have no `on: mavlink` entry is an error (nothing to generate).
* **Element** (`service.json`): `"mavlink": {"msg_id"}` for an event, `{"req_id", "resp_id", ...}`
  for a method. No block or `"mavlink": false` keeps the element off MAVLink. The keys allowed on
  an element are `id`, `data_structure`, `desc` and `mavlink`; any other key is an error.
* **Messages**: `SRP_<SERVICE>_<EVENT>` and `SRP_<SERVICE>_<METHOD>_REQ` / `_RESP`. Events start
  with `seq`, `instance`; methods add `target_system`, `target_component`; a response adds
  `status`. Struct members are flattened to `a_b`; a string needs `max_len`.
* The daemon identifies a service by its name: all instances and both roles of a service share
  the `msg_id`s, and a duplicate is rejected per (service, instance, role).

## Checks

Generation fails, naming the application, service and element, on: a `msg_id` or message name
collision (including HEARTBEAT, id 0), a payload over 255 B, a string without `max_len`, a field
that collides with the header or is a reserved Python name, an instance registered twice for
the same (service, instance, role), a duplicate SOME/IP element id, and unknown keys.
With a daemon config it also fails on an unknown link name, an id above 255 on a MAVLink v1 link
and links of one instance with different `signing.enabled` / `accept_unsigned`.
The daemon validates its own config when it starts; the generator only reads link names,
versions and signing mode from it.

Warnings: a daemon link no service uses, an instance whose elements are all excluded, and
`max_rate_hz` that has no effect (excluded element or `require` entry; the value is not emitted).

## Tests

`bazel test //tools/model_generator/ara/mavlink/...`: `test_mavlink_model` (messages, entries,
CPU checks) and `test_mavlink_gen` (golden files in `testdata/`, C++ emission, matrix, CLI,
pymavlink round trip). Update the golden files deliberately when the output changes.
