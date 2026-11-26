# `example_udp_radio` — a minimal radio driver

One MAVLink frame = one UDP datagram. The code is a teaching example of the `IRadioDriver`
contract (`ara/com/mavlink/radio_driver.h`); the comments in `udp_radio.cc` say which rule each
piece satisfies. Real drivers live in the integrator repository (srp); the step-by-step guide is
"Writing and linking a radio driver" in [`docs/mavlink/README.md`](../../../../../docs/mavlink/README.md).

| File | What |
|------|------|
| `udp_radio.h`, `udp_radio.cc` | the driver, registered as `example_udp_radio` (`alwayslink = True`) |
| `BUILD` | the library, the daemon binary `example_udp_daemon` (`mavlink_daemon_binary`), the contract test |
| `example_udp_daemon_config.json` | daemon config: link `udp_ground` on the driver |
| `udp_ground.json` | driver file (`driver_config`): `{"bind_port": 14560, "peer_host": "127.0.0.1", "peer_port": 14550}` |
| `udp_radio_contract_test.cc` | the shared contract suite on this driver |

An unknown key in the driver file (a typo such as `peer_hots`) makes `Start()` fail with a `kPersistent` error that names the key. A peer that cannot be reached (no route, host down, closed port) does not stop the link: `Send()` stays `kRetryable` and the driver logs one line per 10 s through `OnError` (not fatal).

Run: `bazel test //ara/com/mavlink/examples/udp_radio/...`. Try it by hand: build
`//ara/com/mavlink/examples/udp_radio:example_udp_daemon`, point `driver_config` at `udp_ground.json`
and listen on UDP 14550 with pymavlink (`mavutil.mavlink_connection("udpin:127.0.0.1:14550")`);
send to port 14560.

Design: one thread in `poll()` (socket + an eventfd for `Stop()` and for the wake-up after `kBusy`);
`Send()` is a non-blocking `sendto` (`EAGAIN` -> `kBusy`, `OnTxReady()` after `POLLOUT`); `Stop()`
joins the thread, so there are no callbacks afterwards; `MaxFrameSize()` = 280.
