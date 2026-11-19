# tests/mavlink: integration tests of the MAVLink transport

Unit tests live next to the code (`ara/com/mavlink/ut`, `ara/com/mavlink/client/ut`,
`platform/common/mavlink_daemon/code/ut`, `tools/model_generator/ara/mavlink/test_*.py`,
`tools/mavlink/*_test.py`). This directory holds the integration tests: a real **MavlinkDaemon**
(the `daemon_host` core), the example applications `example_app` and `example_app2`, and a
ground station on `pymavlink`, joined through simulated radio links (ptys).

## Running

```sh
bazel test //tests/mavlink/...
```

`tshark` must be installed (without it `tshark_lua_test` fails). `pymavlink` comes from bazel.
Run heavy builds under `flock -o` and a separate `--output_base` for ARM.

## Topology

One "CPU" of the test is three processes: **`daemon_host`** (the production daemon core without
Execution Management, controlled by JSON lines: `stats` returns the counters of the links and of
the daemon, `stop` saves the signing state and exits), `example_app` (provides EngineService
instances 2 and 3) and `example_app2` (requires instance 2). The applications run with the mains
in `no_em_main_app*.cc`, which skip Execution Management. The daemon opens the links
(`serial_radio`) on one end of a `ChannelSim` pty, the ground station sits on the other. The
applications reach the daemon through a unix socket (`SRP_MAVLINK_DAEMON_SOCKET`).

## Files

| File | Purpose |
|---|---|
| `channel_sim.py`, `channel_sim_test.py` | radio simulator on ptys: loss, bit flips, delay, baud limit, seed |
| `mav_helpers.py`, `mav_helpers_test.py` | signing helpers and `StrictVerifier`, a reference verifier written from the spec |
| `harness.py` | `JsonProc` (a JSON-lines process), `GS` (the ground station), `Link`, `Scenario`, `wait_until`, the CPU dialect module |
| `tshark_harness.py`, `tshark_lua_test.py` | pcap, `tshark -X lua_script:` with the generated Lua: v1, v2 and signed frames |
| `daemon_host.cc` | the daemon core for tests |
| `no_em_main_app1.cc`, `no_em_main_app2.cc` | test mains of the example applications |
| `e2e_examples_test.py` | the scenarios below |
| `daemon_client.py` | a Python client of the daemon IPC; the E220 end-to-end test in srp uses it |

## Requirement to test

| Requirement | Test |
|---|---|
| `on: mavlink` in the model | `tools/model_generator/ara/mavlink/test_mavlink_gen.py` (emission); `e2e_examples_test.py` (the example models) |
| two applications on one daemon and one radio | `e2e_examples_test::test_both_apps_register_in_one_daemon`, `test_events_reach_the_gs_on_the_links_of_their_instance` |
| dialect XML for the ground station | `test_mavlink_gen.py` (golden XML, pymavlink round trip) |
| Lua for Wireshark (per CPU) | `tshark_lua_test.py` (v1, v2, signed); `tools/mavlink/serial2udp_test.py` |
| MAVLink v1 and v2 | `tshark_lua_test.py`; `test_daemon_output_is_valid_mavlink_2_with_heartbeats`; `ara/com/mavlink/interop/ut/test_cpp_interop.py` (codec against pymavlink, v1, v2, signed) |
| signing v2 and the key | `test_signed_links_sign_refuse_unsigned_and_keep_timestamps_rising_across_a_restart`; `mav_helpers_test.py`; `tools/mavlink/keygen_test.py` |
| several links, redundancy, dedup | `test_require_app_gets_each_event_once_despite_two_links` |
| lossy channel | `test_lossy_links_deliver_redundant_events_without_duplicates` (10 and 30 percent) |
| `exclude` | `test_gs_calls_methods_excluded_ones_answer_status_2` |
| `max_rate_hz` | `test_instance_rate_limit_is_below_the_service_limit` |
| method with a timeout | `test_proxy_call_is_answered_times_out_and_recovers` |
| restart of the daemon and of an application | `test_apps_register_again_within_5_s_after_the_daemon_is_killed`; `ara/com/mavlink/client/ut/test_daemon_client.cc` |
| `msg_id` and instance conflicts | generator: `test_mavlink_model.py::CpuTest`; daemon: `test_conflicting_registrations_are_refused_with_a_reason` |
| garbage on the link | `test_random_garbage_for_10_s_does_not_stop_the_daemon_or_the_apps`; `FrameParser` unit tests |

`mav_helpers.StrictVerifier` follows the spec; pymavlink 2.4.x does not catch a replay of a frame
older than the last one.
