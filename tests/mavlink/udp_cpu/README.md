# udp_cpu: CPU package for hardware tests of MAVLink over UDP

A ready CPU package (`//tests/mavlink/udp_cpu:pkg`) for an RPi Zero 2 W (or a BBB) and a PC as the
ground station. The MavlinkDaemon uses the example radio driver `example_udp_radio`
(`ara/com/mavlink/examples/udp_radio`; one MAVLink frame = one UDP datagram), so no radio hardware is
needed: the "air" is the cable. It is the hardware test plan of the MAVLink stack (events, methods,
restart of the daemon) with the same applications as `//test_cpu/cpu/ec:pkg`.

```
 PC (station)                          RPi Zero 2 W (CPU: system 1, component 1)
 192.168.7.1  <== USB gadget g_cdc ==> usb0 192.168.7.2
 UDP :14550  <---- events, RESP -----  radio_a  driver :14560  (udp_ground.json)
             ----- REQ ------------->
 UDP :14551  <---- events, RESP -----  radio_b  driver :14561  (udp_ground_b.json, 2400 bps, mtu 100)
```

## What is in the package

| Part | Content |
|------|---------|
| `mavlink_daemon` | `udp_daemon` = `mavlink_daemon_binary` with `example_udp_radio`; installed as `/srp/opt/mavlink_daemon` (the name of the EC package, so Execution Management starts it as usual) |
| `mavlink_daemon_config.json` | `system_id` 1, `component_id` 1, links `radio_a` and `radio_b` (driver `example_udp_radio`) |
| `udp_ground.json`, `udp_ground_b.json` | the driver files: `bind_port`, `peer_host` (the PC), `peer_port` |
| applications | `ExampleApp`, `ExampleApp2` (models with `on: mavlink`, as in `test_cpu/cpu/ec`) |
| platform | `em`, `logger`, `someip_demon`, `state_manager`, `diag_demon` |

Link names are `radio_a` and `radio_b` because the models of the example applications name those
links (the generator rejects another name); here both are UDP links. `radio_b` has the limits of the
EC config (`air_rate_bps` 2400, `max_utilization` 0.8, `mtu` 100), so the bandwidth budget can be
tested. The station dialect is `//test_cpu/cpu/ec:dialect` (the same models and ids);
`//tests/mavlink/udp_cpu:dialect` generates it from this package's config and fails the build when the
config does not fit the models.

## Build and flash

```sh
bazel build --config=rpi-platform //tests/mavlink/udp_cpu:pkg      # RPi Zero 2 W (tools/rpi)
bazel build --config=bbb-platform //tests/mavlink/udp_cpu:pkg      # BBB
bazel run   --config=rpi-platform //tests/mavlink/udp_cpu:flash    # ssh root@192.168.7.2, then reboot
```

`flash` goes to `192.168.7.2` (the RPi over USB). Other addresses:

| Setup | CPU | PC | What to change |
|-------|-----|----|----------------|
| USB gadget (default) | 192.168.7.2 | 192.168.7.1 | nothing |
| Ethernet/WiFi of the SRP subnet | 192.168.10.51 | 192.168.10.1/24 | `ip` in `flash` (BUILD), `ip`/`interface_name` in `platform_config.json`, `ip` in `logger_config.json`, `peer_host` in both `udp_ground*.json` |
| QEMU rpi (`tools/qemu`) | 192.168.10.101 | tap0 host side | the same four places, `ip` = 192.168.10.101 |

Change the PC address (`peer_host`) only in `udp_ground.json` / `udp_ground_b.json` and rebuild, or
edit `/srp/opt/mavlink_daemon/etc/udp_ground.json` on the CPU and restart the daemon. The flash script
removes `/srp/opt/*` first, so a file edited on the CPU is lost at the next flash.

The CPU needs the SRP image (`tools/rpi/README.md`) with `usb0` up as 192.168.7.2 (`g_cdc`); the
PC side: `sudo ip addr add 192.168.7.1/24 dev <usb interface>` if the PC does not do it itself.
`platform_config.json` of this package sets `interface_name` to `usb0`; the SOME/IP traffic of the
applications stays on the CPU, only MAVLink goes to the PC.

## Test on the PC (the plan for `tester-hw`)

Run one tool at a time on UDP 14550 (a second socket would split the datagrams).

1. Connectivity: `ping 192.168.7.2`; after flash and reboot the daemon runs
   (`ps | grep mavlink_daemon` on the CPU, the log of `logger`).
2. Monitor (events, rates, methods):

   ```sh
   bazel run //test_cpu/cpu/ec:dialect_monitor -- --udp 14550 --plain --duration 30
   ```

   Optional, Wireshark with the generated dissector:

   ```sh
   bazel build //test_cpu/cpu/ec:dialect
   wireshark -k -i <iface> -f "udp port 14550" -X lua_script:bazel-bin/test_cpu/cpu/ec/srp_example.lua
   ```

3. Requests from the station (SetMode, DebugDump) and the responses:

   ```sh
   bazel run //tests/mavlink:udp_cpu_station -- --set-mode 5 --debug-dump --duration 10
   # another CPU address: --target 192.168.10.51 (default 192.168.7.2:14560, listens on 14550)
   ```

   Exit code 0 and `PASS` = the events were seen and every SetMode got a response with status 0.
4. Second link: `bazel run //test_cpu/cpu/ec:dialect_monitor -- --udp 14551 --link radio_b --plain`.

### Expected values

| Check | Expected |
|-------|----------|
| `EngineService.CurrentMode` instance 2 and 3 on `radio_a` | about 10 Hz (source rate, no limit), each within 15 % |
| `EngineService.Telemetry` instance 2 on `radio_a` | 2 Hz (limit `max_rate_hz` 2) |
| `EngineService.Telemetry` instance 3 on `radio_a` | 1 Hz (limit 1) |
| `radio_b`: `CurrentMode` instance 2 | 1 Hz (limit 1), `Telemetry` instance 2: 2 Hz |
| SetMode REQ from `udp_cpu_station` | RESP, status 0, same `seq`; 5 of 5 |
| SetMode REQ of `ExampleApp2` (instance 2, twice a second) | monitor shows REQ/RESP pairs only when the station answers (the monitor does not answer) |
| DebugDump REQ | no response (void method); the daemon stays up |
| Channel errors in the monitor | `crc_errors`, `unknown_msgid`, `v1_frames`, `duplicate_seq`, `out_of_model`, `garbage_bytes` all 0 |
| `radio_b` air share | below 80 % (`max_utilization`) |
| Restart of the daemon (kill it on the CPU, Execution Management starts it again) | events return within the restart time of EM, `seq` of the new daemon starts from 0, so the monitor may show one `duplicate_seq` per stream (when the last `seq` before the restart was 0) and nothing else; the channel error counters do not grow otherwise |

The same values are checked on a host by `//tests/mavlink:udp_cpu_test` (the config of this package
with the loopback as the station) and `//tests/mavlink:e2e_udp_radio_test`.

## Soak test

A long test of the daemon under CPU load (`tester-hw`, RPi Zero 2 W `srp-rpi`): does it keep the
rates, lose no frame, answer the requests and keep its memory while `sha256sum /dev/zero` hogs take
every core. Three phases: **baseline** 10 min (no hogs), **load** 3 h (one hog per core, `nice 0`:
the same priority as the daemon), **cooldown** 10 min (the rates return to the baseline).

```sh
bazel build //tests/mavlink/udp_cpu:soak_run                     # the PC tools
bazel run   //tests/mavlink/udp_cpu:soak_run -- --duration 10800 # the whole test, in the terminal
setsid nohup bazel-bin/tests/mavlink/udp_cpu/soak_run --pcap > /dev/null 2>&1 &   # detached
```

Needs the package flashed (`:flash`), `ssh root@192.168.7.2` with a key, the PC at 192.168.7.1 and
free UDP 14550/14551. Options of `soak_run`: `--duration S` (load phase, default 10800), `--baseline S`,
`--cooldown S` (600 each), `--hogs N|auto`, `--host USER@HOST`, `--out DIR` (default
`~/mavlink/soak/<date>/`), `--pcap` (tshark, a ring of 10 files of 50 MB), `--iface`, `--dlt-cmd CMD`
(a command that prints the DLT log of the daemon, saved to `dlt.log` and scanned for `ERROR`),
`--log FILE` (one more log to scan), `--station-args "..."` / `--verdict-args "..."` (the thresholds
below, for example `--verdict-args "--max-temp-c 75"`). Exit code 0 = PASS.

`soak_run` copies `soak_cpu.sh` to `/tmp` on the CPU and starts it with `nohup` (it runs on, if the
PC loses the connection), runs `soak_station` on the PC, and after the end fetches
`/logs/soak_<date>.csv` and `/logs/srp_start.log`. `soak_run --local` does it all on this host (the
daemon with `example_udp_daemon_host` and the example applications, the loopback as the CPU, phases
10 + 40 + 10 s): a test of the tools, run by `bazel test //tests/mavlink/udp_cpu:soak_run_test`.

### Files in `--out`

| File | Content |
|------|---------|
| `verdict.json`, last lines of `soak_run.log` | the checks (`ok`, value, limit), the CPU numbers, PASS/FAIL, with the station summary inside |
| `station.json` | the summary of the station: per event the windows, `seq` gaps, the requests, the channel counters |
| `station.jsonl` | one line per second (below) |
| `soak_cpu.csv` | the samples of the CPU every 30 s (below) |
| `srp_start.log`, `dlt.log`, `soak.pcap*`, `station.out`, `tshark.log` | logs of the CPU and the PC |

### Columns of `soak_cpu.csv`

`t_s` seconds since the start of the sampler; `epoch` clock of the CPU; `hogs` hog processes
running; `pid` PID of the daemon (0 = not running); `restarts` how many times the PID changed;
`rss_kb` VmRSS of the daemon; `daemon_cpu` CPU of the daemon in % of one core (100 = a whole core)
over the interval; `cpu_total` busy time of all cores in % (from the deltas of `/proc/stat`, without
idle and iowait); `load1`; `temp_mc` temperature in millidegrees (`thermal_zone0`); `freq_khz`
`scaling_cur_freq` of cpu0. The file ends with the line `SOAK_CPU_DONE`.

### Fields of `station.jsonl`

`t` seconds, `phase`; per link (`radio_a`, `radio_b`) `events` (`<Element>#<instance>`: `frames`
since the start, `hz` of the last 5 s), `seq_lost` frames missing in the `seq` of the header,
`bytes_per_s`, `air_share_pct` (of `air_rate_bps`), `errors` (non-zero channel counters: CRC,
signature, unknown id, v1, out of the model, garbage); `client` the own SetMode requests (`sent`,
`ok`, `late`, `timeouts`); `responder` REQs of ExampleApp2 received and answered (RESP status 0, the
same link).

### Thresholds (the defaults are the acceptance criteria of the hardware test)

| Check | Default | Flag |
|-------|---------|------|
| PID of the daemon constant, 0 restarts | 0 | `--max-restarts` (verdict) |
| RSS growth (end vs the median of the baseline) | <= 5 MB and <= 10 % | `--max-rss-growth-kb`, `--max-rss-growth-pct` (verdict) |
| radio_a: every event within 15 % (and one frame per window) of min(source rate, `max_rate_hz`) in windows of 10 s, in 99 % of the windows of the load phase | 0.15, 0.99 | `--tolerance`, `--min-window-ok`, `--window` (station) |
| radio_a: gaps in the header `seq` (UDP loses nothing: a gap = a frame the daemon dropped) | 0 | `--max-seq-gaps` |
| radio_b: no event above its `max_rate_hz` (a frame dropped by the limiter is only reported: `mean_hz`, `min_hz` in `station.json`); the air share | < 80 % | `--max-air-share` |
| SetMode REQ of the station answered within `method_timeout_ms` (default 1 s) | 99.5 % | `--min-resp-ratio` |
| REQs of ExampleApp2 answered by the station | all | |
| CRC, signature, unknown id, v1, out of the model, garbage | 0 | |
| `ERROR` lines in `srp_start.log` and in the DLT log (`--dlt-cmd`; without it the check is SKIP) | 0 | `--log` |
| after the cooldown the rates equal the baseline (without the first 30 s) | 0.15 | `--cooldown-skip` |
| temperature | < 80 C | `--max-temp-c` |
| load confirmed: mean of `cpu_total` in the load phase (without its first minutes) | >= 95 % | `--min-cpu-total` |

`daemon_cpu`, the latency of the responses (p50, p99, max; in `station.json` also per phase) and the
frequency drop of `freq_khz` (`throttling_note` in the verdict: the clock fell below 90 % of its
maximum, a note, not a failure) are reported, not checked.

### Reading the verdict

`soak_run` prints every check as `ok`/`FAIL name: value (limit ...)` and a last line `PASS` or `FAIL`.
A failed check says where to look: a frequency check names the event and the share of the windows in
tolerance (`windows_ok`/`windows` in `station.json`; `station.jsonl` shows when it broke, and
`soak_cpu.csv` what the CPU did at that moment), a `seq` gap is a frame lost in the daemon, a PID
change is a crash and restart of the daemon (`srp_start.log`, the DLT log). The station is tied to
the example dialect: the SetMode of `EngineService` (the station answers the REQs of ExampleApp2 and
sends its own; DebugDump has no response, so it is not used for the latency).
