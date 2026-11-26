"""The parser, the `seq` gaps and the frequency windows of the soak station on synthetic frames.

A fake clock drives `Station` through baseline, load and cooldown with the traffic of the model of
the example CPU (frames encoded with the generated dialect); no socket, no daemon.
"""
# pylint: disable=missing-function-docstring,redefined-outer-name,import-error,too-many-locals
# pylint: disable=invalid-name,too-few-public-methods,too-many-branches,too-many-arguments
# pylint: disable=too-many-positional-arguments,missing-class-docstring
import json
import os
import sys

import pytest

import soak_station as ss


def dialect_files(suffix):
    return [p for p in os.environ["EXAMPLE_DIALECT"].split() if p.endswith(suffix)]


MOD = ss.mm.LoadDialect(dialect_files("srp_example.py")[0])
with open(dialect_files("_monitor.json")[0], encoding="utf-8") as _f:
    META = json.load(_f)

# (link, element message class, instance, Hz) of the CPU in the model: min(source, limit)
TRAFFIC = [
    ("radio_a", "current_mode", 2, 10.0), ("radio_a", "current_mode", 3, 10.0),
    ("radio_a", "telemetry", 2, 2.0), ("radio_a", "telemetry", 3, 1.0),
    ("radio_b", "current_mode", 2, 1.0), ("radio_b", "telemetry", 2, 2.0),
]
PHASES = "baseline:20,load:40,cooldown:20"


class Sink:
    def __init__(self):
        self.buf = bytearray()

    def write(self, data):
        self.buf += data


class Cpu:
    """The sender of the CPU: one MAVLink object per link (the header `seq` counts per link)."""

    def __init__(self):
        self.sink = {ln: Sink() for ln in ss.LINKS}
        self.mav = {ln: MOD.MAVLink(self.sink[ln], srcSystem=1, srcComponent=1) for ln in ss.LINKS}
        self.rx = MOD.MAVLink(None)

    def frame(self, link, msg):
        self.sink[link].buf.clear()
        self.mav[link].send(msg)
        return bytes(self.sink[link].buf)

    def event(self, link, kind, instance, seq):
        if kind == "current_mode":
            msg = MOD.MAVLink_srp_engine_service_current_mode_message(seq, instance, 1)
        else:
            msg = MOD.MAVLink_srp_engine_service_telemetry_message(seq, instance, 1000, 20.0)
        return self.frame(link, msg)


def args(**over):
    ns = ss.Parse(["--phases", PHASES, "--window", "10", "--cooldown-skip", "10"])
    for k, v in over.items():
        setattr(ns, k, v)
    return ns


def run(drop=(), extra_hz=None, answer=True, step=0.01):
    """80 s of traffic; `drop` = (link, kind, instance, n-th frame) never delivered (the header
    `seq` is used); `extra_hz` = {(link, kind, instance): Hz} replaces the model rate."""
    cpu, sent = Cpu(), []
    st = ss.Station(args(), META, MOD, lambda link, data: sent.append((link, data)))
    rates = {(ln, k, i): hz for ln, k, i, hz in TRAFFIC}
    rates.update(extra_hz or {})
    due = {key: 0.0 for key in rates}
    count = {key: 0 for key in rates}
    req_due, req_seq, answers = 0.0, 0, []
    t = 0.0
    while t < 80.0:
        now = 1000.0 + t
        for key, hz in rates.items():
            while due[key] <= t:
                due[key] += 1.0 / hz
                count[key] += 1
                frame = cpu.event(key[0], key[1], key[2], count[key])
                if (*key, count[key]) not in drop:
                    st.OnDatagram(key[0], frame, now)
        while req_due <= t:  # ExampleApp2 asks for SetMode twice a second
            req_due += 0.5
            req_seq += 1
            st.OnDatagram("radio_a", cpu.frame("radio_a", MOD.MAVLink_srp_engine_service_set_mode_req_message(
                req_seq, 2, 2, 1, 3)), now)
        for link, data in list(sent):  # the REQ of the station: the CPU answers after 5 ms
            sent.remove((link, data))
            msg = cpu.rx.decode(bytearray(data))
            if msg.get_type() == "SRP_ENGINE_SERVICE_SET_MODE_REQ" and answer:
                answers.append((t + 0.005, link, msg))
        for item in [a for a in answers if a[0] <= t]:
            answers.remove(item)
            st.OnDatagram(item[1], cpu.frame(item[1], MOD.MAVLink_srp_engine_service_set_mode_resp_message(
                item[2].seq, item[2].instance, 2, 1, 0, item[2].mode)), now)
        st.Tick(now)
        t += step
    st.Finish(1080.0)
    return st, sent


def test_phases_and_rates_are_parsed():
    phases = ss.ParsePhases("baseline:600,load:10800,cooldown:600")
    assert phases == [("baseline", 600.0), ("load", 10800.0), ("cooldown", 600.0)]
    assert [ss.PhaseAt(phases, t) for t in (0, 599.9, 600, 11399, 11400, 99999)] == [
        "baseline", "baseline", "load", "load", "cooldown", "cooldown"]
    assert ss.ParseRates(ss.DEFAULT_SOURCE_HZ) == {"EngineService.Telemetry": 100.0,
                                                  "EngineService.CurrentMode": 10.0}


def test_the_model_rate_is_the_lower_of_the_source_and_the_limit():
    src = ss.ParseRates(ss.DEFAULT_SOURCE_HZ)
    a = {k: v["expected"] for k, v in ss.ExpectedRates(META, "radio_a", src).items()}
    b = {k: v["expected"] for k, v in ss.ExpectedRates(META, "radio_b", src).items()}
    assert a == {"EngineService.CurrentMode#2": 10.0, "EngineService.CurrentMode#3": 10.0,
                 "EngineService.Telemetry#2": 2.0, "EngineService.Telemetry#3": 1.0}
    assert b == {"EngineService.CurrentMode#2": 1.0, "EngineService.Telemetry#2": 2.0}


def test_seq_gaps_count_lost_frames_and_the_wrap_is_not_a_gap():
    tr = ss.SeqTracker()
    assert [tr.Feed((1, 1), s) for s in (254, 255, 0, 1)] == [0, 0, 0, 0]
    assert tr.Feed((1, 1), 5) == 3 and tr.lost == 3
    assert tr.Feed((9, 1), 40) == 0  # another sender: its own counter
    tr.Feed((9, 1), 40)
    assert tr.repeated == 1 and tr.lost == 3


def test_a_clean_run_passes_with_every_window_in_tolerance():
    st, _ = run()
    summary = st.Summary()
    failed = [c for c in summary["checks"] if not c["ok"]]
    assert summary["verdict"] == "PASS", failed
    ev = summary["events"]["radio_a/EngineService.Telemetry#2"]
    assert ev["windows_ok"] == ev["windows"] >= 3 and abs(ev["mean_hz"] - 2.0) < 0.2
    assert summary["seq"]["radio_a"]["lost"] == 0
    client = summary["client"]
    assert client["ok"] >= 75 and client["timeouts"] == 0
    assert 4.0 < client["latency_ms"]["p50"] < 30.0  # the fake clock steps 10 ms
    assert summary["responder"]["req"] == summary["responder"]["resp"] > 100


def test_a_frame_lost_in_the_daemon_is_a_gap_in_the_seq_and_fails_radio_a():
    st, _ = run(drop={("radio_a", "telemetry", 2, 40)})
    summary = st.Summary()
    assert summary["seq"]["radio_a"]["lost"] == 1
    names = [c["name"] for c in summary["checks"] if not c["ok"]]
    assert names == ["radio_a: no gap in the seq of the header"], names


def test_an_event_above_the_model_rate_fails_its_windows():
    st, _ = run(extra_hz={("radio_a", "telemetry", 3): 1.6})
    summary = st.Summary()
    names = [c["name"] for c in summary["checks"] if not c["ok"]]
    assert any("radio_a/EngineService.Telemetry#3" in n for n in names), names
    assert summary["events"]["radio_a/EngineService.Telemetry#3"]["windows_ok"] == 0


def test_radio_b_above_its_limit_fails_and_below_it_only_reports():
    over, _ = run(extra_hz={("radio_b", "current_mode", 2): 1.5})
    assert any("radio_b/EngineService.CurrentMode#2 not above" in c["name"]
               for c in over.Summary()["checks"] if not c["ok"])
    under, _ = run(extra_hz={("radio_b", "current_mode", 2): 0.5})  # the limiter dropped frames
    summary = under.Summary()
    assert summary["verdict"] == "PASS" and summary["events"][
        "radio_b/EngineService.CurrentMode#2"]["mean_hz"] < 0.6


def test_unanswered_requests_fail_the_ratio():
    st, _ = run(answer=False)
    summary = st.Summary()
    assert summary["client"]["ok"] == 0 and summary["client"]["timeouts"] > 70
    assert any("answered in time" in c["name"] for c in summary["checks"] if not c["ok"])


def test_a_crc_error_is_reported():
    cpu = Cpu()
    st = ss.Station(args(), META, MOD, lambda *_: None)
    frame = bytearray(cpu.event("radio_a", "telemetry", 2, 1))
    frame[-1] ^= 0xFF
    st.OnDatagram("radio_a", bytes(frame), 1000.0)
    assert st.mons["radio_a"].ch["crc_errors"] == 1
    st.Tick(1000.5)
    assert not [c for c in st.Summary()["checks"] if c["name"].startswith("no CRC")][0]["ok"]


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v", "-p", "no:cacheprovider"]))
