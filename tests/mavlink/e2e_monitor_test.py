"""The generated ground monitor (`<dialect>_monitor.py`) on the traffic of the example CPU.

The scenario is the one of the example applications (`engine_service.h`: Telemetry at 100 Hz,
CurrentMode at 10 Hz, limited by `max_rate_hz` of the model; `example_app2` calls SetMode twice a
second and a ground station answers). A tap copies every frame of a link, in both directions, to the
monitor, which runs for 10 s with `--jsonl`:

  * UDP: `example_udp_daemon` + `--udp PORT`, one datagram per frame, links radio_a and radio_b;
  * UART: `daemon_host` + `serial_radio` through a ChannelSim at 57600 baud, `--serial PTY
    --baud 57600`, link radio_a.

Both must give the same result: every event of the link present, the measured frequency within
15 % of min(source rate, max_rate_hz), the methods paired (every REQ answered with status 0) and no
error on the channel.
"""
# pylint: disable=missing-function-docstring,redefined-outer-name,import-error,too-many-locals
# pylint: disable=too-many-arguments,consider-using-with
import json
import os
import subprocess
import sys
import threading

import pytest

import e2e_examples_test as ee
import harness as hn
import mavlink_monitor
import udp_harness as uh

UDP_DAEMON_HOST = os.environ["UDP_DAEMON_HOST"]
SOURCE_HZ = {"EngineService.Telemetry": 100.0, "EngineService.CurrentMode": 10.0}
RUN_S = 10
TOLERANCE = 0.15
ERRORS = ("crc_errors", "unknown_msgid", "v1_frames", "duplicate_seq", "out_of_model",
          "garbage_bytes", "decode_errors", "short_datagrams", "sig_bad", "filtered")


def dialect_files(suffix):
    return [p for p in os.environ["EXAMPLE_DIALECT"].split() if p.endswith(suffix)]


MONITOR = os.environ["MONITOR"]  # the target //test_cpu/cpu/ec:dialect_monitor
with open(dialect_files("_monitor.json")[0], encoding="utf-8") as _f:
    META = json.load(_f)


@pytest.fixture
def scn():
    s = hn.Scenario()
    yield s
    s.close()


class Monitor:
    """The `dialect_monitor` target with `--jsonl` as a subprocess; `snapshots` are its lines."""

    def __init__(self, args):
        self.proc = subprocess.Popen([MONITOR, *args, "--jsonl", "--duration", str(RUN_S)],
                                     stdout=subprocess.PIPE, text=True)
        self.snapshots, self.first = [], threading.Event()
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.reader.start()
        assert self.first.wait(15.0), "the monitor printed nothing"

    def _read(self):
        for line in self.proc.stdout:
            self.snapshots.append(json.loads(line))
            self.first.set()

    def result(self):
        assert self.proc.wait(timeout=RUN_S + 20) == 0
        self.reader.join(5.0)
        return self.snapshots[-1]

    def stop(self):
        if self.proc.poll() is None:
            self.proc.kill()
            self.proc.wait()


def expected_hz(element, limit):
    source = SOURCE_HZ[element]
    return min(source, limit) if limit else source


def check(snap, link, what):
    """The AC on one link: events, methods, channel."""
    problems = []
    cpu = META["cpus"][0]
    wanted = [(el["element"], inst["instance"], inst["links"][link])
              for el in cpu["elements"] if el["kind"] == "event"
              for inst in el["instances"] if inst["direction"] == "out" and link in inst["links"]]
    assert wanted, f"the model has no event on {link}"
    for element, instance, limit in wanted:
        ev = next((e for e in snap["events"] if e["element"] == element
                   and e["instance"] == instance and e["src"] == "1:1"), None)
        exp = expected_hz(element, limit)
        if ev is None or not ev["frames"]:
            problems.append(f"{element}#{instance}: no frames")
            continue
        if abs(ev["hz_avg"] - exp) > TOLERANCE * exp:
            problems.append(f"{element}#{instance}: {ev['hz_avg']} Hz, expected {exp} +-15%")
        if abs(ev["hz_window"] - exp) > TOLERANCE * exp + 1.0 / mavlink_monitor.WINDOW_S:
            problems.append(f"{element}#{instance}: window {ev['hz_window']} Hz, expected {exp}")
        if ev["over_limit"] or not ev["in_model"] or ev["max_rate_hz"] != limit:
            problems.append(f"{element}#{instance}: {ev}")
    assert not [e for e in snap["events"] if not e["in_model"]], snap["events"]
    methods = [m for m in snap["methods"] if m["element"] == "EngineService.SetMode"]
    if len(methods) != 1:
        problems.append(f"SetMode: {methods}")
    else:
        m = methods[0]
        # a REQ may still be in flight when the monitor stops: that one is `pending`, not lost;
        # a RESP may answer a REQ sent before the tap started: that one is not `paired`
        if (m["resp"] < 2 * RUN_S - 4 or not 0 <= m["resp"] - m["paired"] <= 1
                or m["unanswered"] != 0
                or m["req"] - m["paired"] != m["pending"] + m["unanswered"] or m["pending"] > 1
                or m["status"] != {"0": m["resp"]} or m["requester"] != "1:1"):
            problems.append(f"SetMode: {m}")
    ch = snap["channel"]
    for key in ERRORS:
        if ch[key]:
            problems.append(f"channel {key}={ch[key]}")
    if not ch["frames"] or not ch["bytes_per_s"]:
        problems.append(f"channel {ch}")
    print(f"\n--- {what} {link}\n" + mavlink_monitor.RenderPlain(snap))
    assert not problems, f"{what} {link}: {problems}"


def run_taps(taps, monitors):
    """Starts passing frames once every monitor is up, waits for the monitors to finish."""
    for tap in taps:
        tap.active = True
    return [m.result() for m in monitors]


def test_monitor_over_udp_on_both_links(tmp_path, scn):
    stack = uh.UdpStack(tmp_path, scn, UDP_DAEMON_HOST, os.environ["EXAMPLE_DAEMON_CONFIG"],
                        ee.APP1, ee.APP2)
    stack.wait_registered()
    ports = uh.free_udp_ports(len(uh.LINKS))
    monitors = []
    for link, port in zip(uh.LINKS, ports):
        stack.gs[link].tap = scn.add(hn.UdpTap(port))
        monitors.append(scn.add(Monitor([
            "--udp", f"127.0.0.1:{port}", "--link", link,
            "--daemon-config", str(tmp_path / "mavlink_daemon_config.json")])))
    snaps = run_taps([stack.gs[n].tap for n in uh.LINKS], monitors)
    for link, snap in zip(uh.LINKS, snaps):
        check(snap, link, "udp")
    assert snaps[1]["channel"]["air_rate_bps"] == 2400
    assert 0 < snaps[1]["channel"]["air_share_pct"] < 80  # max_utilization of radio_b


def test_monitor_over_a_pty_as_a_uart(tmp_path, scn):
    stack = ee.new_stack(tmp_path, scn, baud=57600)
    tap = stack.gs["radio_a"].tap = scn.add(hn.PtyTap())
    monitor = scn.add(Monitor(["--serial", tap.path, "--baud", "57600", "--link", "radio_a",
                               "--daemon-config", stack.cfg_path]))
    check(run_taps([tap], [monitor])[0], "radio_a", "uart")


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v", "-s", "-p", "no:cacheprovider"]))
