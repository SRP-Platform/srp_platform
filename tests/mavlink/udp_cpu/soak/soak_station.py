"""Ground station of the soak test of `//tests/mavlink/udp_cpu` (the PC side of both UDP links).

Listens on both links of the CPU (radio_a 14550, radio_b 14551), counts every frame with the runtime
of the generated monitor (`//tools/mavlink:monitor_lib`), and checks the traffic against the model
over the three phases of the soak (baseline, load, cooldown):

  * events: per link, sender and event the frequency in windows of `--window` seconds against the
    model rate = min(source rate, `max_rate_hz`); radio_a must stay within `--tolerance` in
    `--min-window-ok` of the windows of the load phase, radio_b must not exceed its limits (a
    frame dropped by the limiter is reported, not a failure);
  * `seq` of the MAVLink header per sender and link: on radio_a (UDP loses nothing) a gap = a frame
    the daemon dropped;
  * methods: it answers every SetMode REQ of ExampleApp2 (RESP status 0, the same link) and sends
    its own SetMode REQ every `--client-period` seconds, measuring the latency of the RESP;
  * channel: CRC, signature, unknown message id, frames outside the model;
  * after the cooldown the frequencies equal the baseline.

    bazel run //tests/mavlink/udp_cpu:soak_station -- --duration 60 --summary summary.json

`soak_run` starts it with the phases of the soak. One line of `--jsonl` per second, the verdict in
`--summary` (exit 0 = PASS). Tied to the example dialect (SetMode of EngineService).
"""
# pylint: disable=import-error,too-many-instance-attributes,too-many-locals,too-many-arguments
# pylint: disable=too-many-branches,too-many-statements,missing-function-docstring
# pylint: disable=invalid-name,too-few-public-methods,too-many-branches,too-many-arguments
# pylint: disable=too-many-positional-arguments,missing-class-docstring
from __future__ import annotations

import argparse
import collections
import json
import os
import select
import signal
import socket
import sys
import time
from pathlib import Path

import mavlink_monitor as mm

LINKS = ("radio_a", "radio_b")
STATION = (2, 1)  # system_id, component_id of the station: the remote system of the example models
CPU = (1, 1)
CLIENT_LINK = "radio_a"
SET_MODE = "SRP_ENGINE_SERVICE_SET_MODE"
DEFAULT_SOURCE_HZ = "EngineService.Telemetry=100,EngineService.CurrentMode=10"
ERROR_COUNTERS = ("crc_errors", "unknown_msgid", "sig_bad", "decode_errors", "v1_frames",
                  "out_of_model", "garbage_bytes", "short_datagrams")
STX_V2 = 0xFD


# ---------------------------------------------------------------- helpers

def FindData(rel: str) -> str | None:
    """A data file by its path in the repository: the runfiles tree of `bazel run`/`bazel-bin`
    or the working directory."""
    starts = [Path(__file__), Path(sys.argv[0]), Path.cwd()]
    if os.environ.get("RUNFILES_DIR"):
        starts += [p for p in Path(os.environ["RUNFILES_DIR"]).iterdir() if p.is_dir()]
    for start in starts:
        for parent in [start, *start.parents]:
            if (parent / rel).is_file():
                return str(parent / rel)
    return None


def ParsePhases(text: str) -> list[tuple[str, float]]:
    """`baseline:600,load:10800,cooldown:600` -> [(name, seconds)]."""
    out = []
    for item in text.split(","):
        name, _, secs = item.partition(":")
        out.append((name.strip(), float(secs)))
    return out


def PhaseAt(phases: list[tuple[str, float]], t: float) -> str:
    """The phase at time `t` since the start; the last one lasts for ever."""
    end = 0.0
    for name, secs in phases:
        end += secs
        if t < end:
            return name
    return phases[-1][0]


def ParseRates(text: str) -> dict[str, float]:
    return {k.strip(): float(v) for k, _, v in (item.partition("=") for item in text.split(","))}


def Percentile(values: list[float], pct: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int(round(pct / 100.0 * (len(ordered) - 1))))]


def Label(element: str, instance: int) -> str:
    return f"{element}#{instance}"


def ExpectedRates(meta: dict, link: str, source_hz: dict[str, float]) -> dict[str, dict]:
    """label -> {expected, limit} of the events the CPU sends on `link`: min(source, limit)."""
    out: dict[str, dict] = {}
    for cpu in meta["cpus"]:
        for el in cpu["elements"]:
            if el["kind"] != "event":
                continue
            for inst in el["instances"]:
                if inst["direction"] != "out" or link not in inst["links"]:
                    continue
                limit = float(inst["links"][link])
                src = source_hz.get(el["element"], 0.0)
                out[Label(el["element"], inst["instance"])] = {
                    "expected": min(src, limit) if limit and src else (limit or src),
                    "limit": limit}
    return out


class SeqTracker:
    """Gaps of the 8-bit `seq` of the frame header per sender: a gap = frames that never arrived."""

    def __init__(self) -> None:
        self.last: dict[tuple[int, int], int] = {}
        self.lost = 0
        self.repeated = 0
        self.frames = 0

    def Feed(self, src: tuple[int, int], seq: int) -> int:
        self.frames += 1
        last = self.last.get(src)
        self.last[src] = seq
        if last is None:
            return 0
        gap = (seq - last - 1) & 0xFF
        if gap == 255:
            self.repeated += 1
            return 0
        self.lost += gap
        return gap


class Client:
    """The own SetMode requests: RESP within the timeout = ok; after it = late; never = timeout."""

    def __init__(self, timeout_s: float) -> None:
        self.timeout_s = timeout_s
        self.pending: dict[int, tuple[float, str]] = {}
        self.expired: dict[int, float] = {}
        self.sent = self.ok = self.late = self.timeouts = 0
        self.bad_status = 0
        self.lat_ms: dict[str, list[float]] = collections.defaultdict(list)
        self.next_seq = 1

    def Sent(self, seq: int, now: float, phase: str) -> None:
        self.sent += 1
        self.pending[seq] = (now, phase)

    def Resp(self, seq: int, status: int, now: float) -> None:
        if seq in self.pending:
            sent, phase = self.pending.pop(seq)
            self.ok += 1
            self.lat_ms[phase].append((now - sent) * 1000.0)
            self.lat_ms["all"].append((now - sent) * 1000.0)
        elif seq in self.expired:
            del self.expired[seq]
            self.late += 1
        else:
            return
        if status != 0:
            self.bad_status += 1

    def Expire(self, now: float) -> None:
        for seq, (sent, _phase) in list(self.pending.items()):
            if now - sent > self.timeout_s:
                del self.pending[seq]
                self.expired[seq] = sent
                self.timeouts += 1
        for seq, sent in list(self.expired.items()):
            if now - sent > 30 * self.timeout_s:
                del self.expired[seq]

    def Summary(self) -> dict:
        done = self.ok + self.timeouts
        lat = self.lat_ms["all"]
        return {"sent": self.sent, "ok": self.ok, "late": self.late, "timeouts": self.timeouts,
                "in_flight": len(self.pending), "bad_status": self.bad_status,
                "ok_ratio": round(self.ok / done, 5) if done else None,
                "latency_ms": {"p50": _R(Percentile(lat, 50)), "p99": _R(Percentile(lat, 99)),
                               "max": _R(max(lat) if lat else None)},
                "latency_ms_by_phase": {
                    ph: {"p50": _R(Percentile(v, 50)), "p99": _R(Percentile(v, 99))}
                    for ph, v in self.lat_ms.items() if ph != "all"}}


def _R(value: float | None) -> float | None:
    return None if value is None else round(value, 2)


# ---------------------------------------------------------------- the station

class Station:
    """The counting, answering and checking of the soak; the sockets are in `Run()`.

    `OnDatagram(link, data, now)` takes a frame, `Tick(now)` must be called about every 50 ms and
    returns the JSONL line once a second, `Summary()` the verdict. `send(link, data)` transmits.
    """

    def __init__(self, args, meta: dict, mod, send) -> None:
        self.args, self.meta, self.mod, self.send = args, meta, mod, send
        self.phases = ParsePhases(args.phases)
        self.total_s = sum(s for _n, s in self.phases)
        self.source_hz = ParseRates(args.source_hz)
        self.timeouts = {ln: _Timeout(args.daemon_config, ln) for ln in LINKS}
        self.mons = {ln: mm.Monitor(meta, mod, ln, timeout_ms=self.timeouts[ln] * 1000.0)
                     for ln in LINKS}
        self.expected = {ln: ExpectedRates(meta, ln, self.source_hz) for ln in LINKS}
        self.seq = {ln: SeqTracker() for ln in LINKS}
        self.client = Client(self.timeouts[CLIENT_LINK])
        self.rx = mod.MAVLink(None)
        self.tx = mod.MAVLink(_Sink(), srcSystem=STATION[0], srcComponent=STATION[1])
        self.msg_ids = {m["name"]: int(mid) for mid, m in
                        next(iter(meta["cpus"]))["messages"].items()}
        self.windows: dict[tuple[str, str], list[tuple[str, float]]] = collections.defaultdict(list)
        self.air: dict[str, list[tuple[str, float]]] = collections.defaultdict(list)
        self.last_counts: dict[tuple[str, str], int] = {}
        self.last_bytes = {ln: 0 for ln in LINKS}
        self.req_seen = self.req_answered = 0
        self.t0: float | None = None
        self.next_tick = self.next_window = self.next_client = 0.0
        self.window_start = 0.0
        self.last_t = 0.0

    # -- input

    def OnDatagram(self, link: str, data: bytes, now: float) -> None:
        if self.t0 is None:
            self.Start(now)
        self.mons[link].OnDatagram(data, now)
        if len(data) < 10 or data[0] != STX_V2:
            return
        src = (data[5], data[6])
        self.seq[link].Feed(src, data[4])
        msg_id = data[7] | data[8] << 8 | data[9] << 16
        if msg_id == self.msg_ids[SET_MODE + "_REQ"]:
            self._Answer(link, data)
        elif msg_id == self.msg_ids[SET_MODE + "_RESP"] and link == CLIENT_LINK:
            msg = self._Decode(data)
            if msg is not None and (msg.target_system, msg.target_component) == STATION:
                self.client.Resp(msg.seq, msg.status, now)

    def _Decode(self, data: bytes):
        try:
            return self.rx.decode(bytearray(data))
        except Exception:  # pylint: disable=broad-except  # pymavlink raises MAVError for anything odd
            return None

    def _Answer(self, link: str, data: bytes) -> None:
        req = self._Decode(data)
        if req is None or (req.target_system, req.target_component) != STATION:
            return
        self.req_seen += 1
        self._Send(link, self.mod.MAVLink_srp_engine_service_set_mode_resp_message(
            seq=req.seq, instance=req.instance, target_system=data[5], target_component=data[6],
            status=0, mode=req.mode))
        self.req_answered += 1

    def _Send(self, link: str, msg) -> None:
        self.tx.file.buf.clear()
        self.tx.send(msg)
        self.send(link, bytes(self.tx.file.buf))

    # -- time

    def Start(self, now: float) -> None:
        self.t0 = now
        self.next_tick = now + 1.0
        self.window_start = now
        self.next_window = self._WindowEnd(now)
        self.next_client = now + 1.0

    def _WindowEnd(self, start: float) -> float:
        """The end of the window that starts at `start`: `--window` seconds or the next phase
        boundary, so that a window belongs to one phase."""
        edge = 0.0
        for _name, secs in self.phases:
            edge += secs
            if self.t0 + edge > start + 1e-6:
                return min(start + self.args.window, self.t0 + edge)
        return start + self.args.window

    def Phase(self, now: float) -> str:
        return PhaseAt(self.phases, now - (self.t0 if self.t0 is not None else now))

    def Tick(self, now: float) -> dict | None:
        if self.t0 is None:
            self.Start(now)
        self.last_t = now
        self.client.Expire(now)
        if self.args.client_period > 0 and now >= self.next_client:
            self.next_client += self.args.client_period
            seq = self.client.next_seq
            self.client.next_seq = seq % 0xFFFF + 1
            self.client.Sent(seq, now, self.Phase(now))
            self._Send(CLIENT_LINK, self.mod.MAVLink_srp_engine_service_set_mode_req_message(
                seq=seq, instance=2, target_system=CPU[0], target_component=CPU[1], mode=3))
        if now >= self.next_window:
            self._Window(now)
        if now < self.next_tick:
            return None
        self.next_tick += 1.0
        return self._Line(now)

    def Finish(self, now: float) -> None:
        """The end of the run: closes the last window when it holds at least half of one."""
        self.last_t = now
        self.client.Expire(now)
        if self.t0 is not None and now - self.window_start >= self.args.window / 2:
            self._Window(now)

    def _Window(self, now: float) -> None:
        """Closes a window: the frequency of every event of it, in the phase of its middle; a
        window shorter than half of `--window` (the end of a short phase) is dropped."""
        span = now - self.window_start
        phase = self.Phase((self.window_start + now) / 2)
        keep = span >= self.args.window / 2
        for link, mon in self.mons.items():
            for (element, instance, _src), st in mon.events.items():
                key = (link, Label(element, instance))
                count = st.rate.count
                if keep:
                    self.windows[key].append((phase, (count - self.last_counts.get(key, 0)) / span))
                self.last_counts[key] = count
            nbytes = mon.ch["bytes"]
            if keep and mon.air_rate_bps:
                self.air[link].append((phase, (nbytes - self.last_bytes[link]) * 8 / span
                                       / mon.air_rate_bps * 100.0))
            self.last_bytes[link] = nbytes
        self.window_start = now
        self.next_window = self._WindowEnd(now)

    def _Line(self, now: float) -> dict:
        line: dict = {"t": round(now - self.t0, 1), "phase": self.Phase(now), "links": {}}
        for link, mon in self.mons.items():
            snap = mon.Snapshot(now)
            ch = snap["channel"]
            line["links"][link] = {
                "events": {Label(e["element"], e["instance"]): {"frames": e["frames"],
                                                                "hz": e["hz_window"]}
                           for e in snap["events"]},
                "seq_lost": self.seq[link].lost, "bytes_per_s": ch["bytes_per_s"],
                "air_share_pct": ch["air_share_pct"],
                "errors": {k: ch[k] for k in ERROR_COUNTERS if ch[k]}}
        line["client"] = {k: v for k, v in self.client.Summary().items()
                          if k in ("sent", "ok", "late", "timeouts")}
        line["responder"] = {"req": self.req_seen, "resp": self.req_answered}
        return line

    # -- the verdict

    def _Windows(self, link: str, label: str, phase: str | None) -> list[float]:
        return [hz for ph, hz in self.windows[(link, label)] if phase is None or ph == phase]

    def _Load(self) -> str | None:
        return "load" if any(n == "load" for n, _s in self.phases) else None

    def Summary(self, finished: bool = True) -> dict:
        a = self.args
        checks: list[dict] = []

        def Check(name: str, ok: bool, value, limit) -> None:
            checks.append({"name": name, "ok": bool(ok), "value": value, "limit": limit})

        elapsed = (self.last_t - self.t0) if self.t0 is not None else 0.0
        Check("ran the planned time", finished and elapsed >= a.min_run * self.total_s,
              round(elapsed, 1), f">= {a.min_run * self.total_s:.0f} s")
        events: dict[str, dict] = {}
        one = 1.0 / a.window  # a window holds whole frames: one frame of slack
        load = self._Load()
        for link in LINKS:
            for label, exp in sorted(self.expected[link].items()):
                wins = self._Windows(link, label, load)
                allw = self._Windows(link, label, None)
                key = f"{link}/{label}"
                if link == "radio_a" or not exp["limit"]:
                    ok = [w for w in wins if abs(w - exp["expected"]) <= a.tolerance * exp["expected"] + one]
                else:  # a limited event on radio_b: not above its limit
                    ok = [w for w in wins if w <= exp["limit"] * (1 + a.tolerance) + one]
                frac = len(ok) / len(wins) if wins else 0.0
                events[key] = {"expected_hz": exp["expected"], "limit_hz": exp["limit"],
                               "windows": len(wins), "windows_ok": len(ok),
                               "mean_hz": _R(sum(wins) / len(wins)) if wins else None,
                               "min_hz": _R(min(wins)) if wins else None,
                               "max_hz": _R(max(wins)) if wins else None,
                               "windows_all": len(allw)}
                if link == "radio_a":
                    Check(f"{key} within {a.tolerance:.0%} of {exp['expected']:g} Hz in "
                          f"{a.min_window_ok:.0%} of the windows", frac >= a.min_window_ok,
                          round(frac, 4), f">= {a.min_window_ok}")
                elif exp["limit"]:
                    Check(f"{key} not above its limit {exp['limit']:g} Hz",
                          frac >= a.min_window_ok, round(frac, 4), f">= {a.min_window_ok}")
        for link in LINKS:
            wins = [v for ph, v in self.air[link] if load is None or ph == load]
            if link == "radio_b" and wins:
                frac = sum(1 for v in wins if v <= a.max_air_share) / len(wins)
                Check(f"{link} air share <= {a.max_air_share:g} %", frac >= a.min_window_ok,
                      round(frac, 4), f">= {a.min_window_ok}")
        Check("radio_a: no gap in the seq of the header", self.seq["radio_a"].lost <= a.max_seq_gaps,
              self.seq["radio_a"].lost, f"<= {a.max_seq_gaps}")
        cs = self.client.Summary()
        ratio = cs["ok_ratio"]
        Check(f"SetMode requests answered in time (timeout {self.timeouts[CLIENT_LINK]:g} s)",
              ratio is not None and ratio >= a.min_resp_ratio and cs["bad_status"] == 0,
              ratio, f">= {a.min_resp_ratio}")
        Check("the ExampleApp2 requests were answered", 0 < self.req_seen == self.req_answered,
              f"{self.req_answered}/{self.req_seen}", "> 0 and all")
        errors = {ln: {k: self.mons[ln].ch[k] for k in ERROR_COUNTERS if self.mons[ln].ch[k]}
                  for ln in LINKS}
        Check("no CRC, signature, unknown id or out-of-model frame",
              not any(errors.values()), errors, "none")
        base, cool = "baseline", "cooldown"
        if any(n == base for n, _s in self.phases) and any(n == cool for n, _s in self.phases):
            for label in sorted(self.expected["radio_a"]):
                b = self._Windows("radio_a", label, base)
                c = self._Windows("radio_a", label, cool)[max(1, int(a.cooldown_skip / a.window)):]
                if b and c:
                    mb, mc = sum(b) / len(b), sum(c) / len(c)
                    Check(f"radio_a/{label}: after the cooldown like the baseline",
                          abs(mc - mb) <= a.tolerance * mb, f"{mc:.2f} vs {mb:.2f} Hz",
                          f"{a.tolerance:.0%}")
                else:
                    Check(f"radio_a/{label}: after the cooldown like the baseline", False,
                          "no windows", "baseline and cooldown")
        return {
            "phases": self.phases, "elapsed_s": round(elapsed, 1), "events": events,
            "seq": {ln: {"lost": s.lost, "repeated": s.repeated, "frames": s.frames}
                    for ln, s in self.seq.items()},
            "client": cs, "responder": {"req": self.req_seen, "resp": self.req_answered},
            "channel": {ln: self.mons[ln].Snapshot(self.last_t)["channel"] for ln in LINKS},
            "air_share_pct": {ln: {"mean": _R(sum(v for _p, v in w) / len(w)) if w else None,
                                   "max": _R(max((v for _p, v in w), default=None))}
                              for ln, w in self.air.items()},
            "checks": checks, "verdict": "PASS" if all(c["ok"] for c in checks) else "FAIL"}


class _Sink:
    def __init__(self) -> None:
        self.buf = bytearray()

    def write(self, data: bytes) -> None:
        self.buf += data


def _Timeout(path: str | None, link: str) -> float:
    """`method_timeout_ms` of the link in the daemon config, in seconds."""
    ms = mm.DEFAULT_TIMEOUT_MS
    if path:
        with open(path, encoding="utf-8") as f:
            ms = json.load(f).get("links", {}).get(link, {}).get("method_timeout_ms", ms)
    return ms / 1000.0


def Report(summary: dict) -> str:
    out = [f"soak station: {summary['elapsed_s']:.0f} s, phases "
           f"{', '.join(f'{n} {s:g} s' for n, s in summary['phases'])}", ""]
    for key, ev in summary["events"].items():
        out.append(f"  {key:52s} {ev['mean_hz'] if ev['mean_hz'] is not None else '-':>7} Hz "
                   f"(model {ev['expected_hz']:g}, limit {ev['limit_hz']:g}) windows ok "
                   f"{ev['windows_ok']}/{ev['windows']}")
    c = summary["client"]
    out += ["", f"  SetMode: sent {c['sent']}, ok {c['ok']}, late {c['late']}, timeouts "
                f"{c['timeouts']}, latency p50/p99/max {c['latency_ms']['p50']}/"
                f"{c['latency_ms']['p99']}/{c['latency_ms']['max']} ms",
            f"  ExampleApp2 REQ answered: {summary['responder']['resp']}/{summary['responder']['req']}",
            "  seq lost: " + ", ".join(f"{ln} {v['lost']}" for ln, v in summary["seq"].items()), ""]
    for ch in summary["checks"]:
        out.append(f"  {'ok  ' if ch['ok'] else 'FAIL'} {ch['name']}: {ch['value']} (limit {ch['limit']})")
    out.append(summary["verdict"])
    return "\n".join(out)


# ---------------------------------------------------------------- sockets

def Parse(argv) -> argparse.Namespace:
    ap = argparse.ArgumentParser(description=__doc__.split("\n", maxsplit=1)[0])
    ap.add_argument("--dialect-py", help="generated pymavlink module (default: of the runfiles)")
    ap.add_argument("--meta", help="<dialect>_monitor.json (default: of the runfiles)")
    ap.add_argument("--daemon-config", help="mavlink_daemon_config.json: method_timeout_ms")
    ap.add_argument("--target", default="192.168.7.2", help="address of the CPU")
    ap.add_argument("--peer-port-a", type=int, default=14560, help="bind_port of radio_a (CPU)")
    ap.add_argument("--peer-port-b", type=int, default=14561, help="bind_port of radio_b (CPU)")
    ap.add_argument("--listen-host", default="0.0.0.0")
    ap.add_argument("--listen-port-a", type=int, default=14550, help="peer_port of radio_a (PC)")
    ap.add_argument("--listen-port-b", type=int, default=14551, help="peer_port of radio_b (PC)")
    ap.add_argument("--duration", type=float, help="seconds (default: the sum of the phases)")
    ap.add_argument("--phases", help="name:seconds,... (default: load for --duration)")
    ap.add_argument("--jsonl", help="one JSON object per second")
    ap.add_argument("--summary", help="the verdict as JSON; the exit code is 0 for PASS")
    ap.add_argument("--window", type=float, default=10.0, help="seconds of a frequency window")
    ap.add_argument("--client-period", type=float, default=1.0,
                    help="seconds between the own SetMode requests (0 = none)")
    ap.add_argument("--source-hz", default=DEFAULT_SOURCE_HZ,
                    help="source rate of the events of the example applications")
    ap.add_argument("--tolerance", type=float, default=0.15, help="radio_a: +-share of the model Hz")
    ap.add_argument("--min-window-ok", type=float, default=0.99, help="share of the windows within it")
    ap.add_argument("--min-resp-ratio", type=float, default=0.995, help="SetMode answered in time")
    ap.add_argument("--max-seq-gaps", type=int, default=0, help="radio_a: lost frames (header seq)")
    ap.add_argument("--max-air-share", type=float, default=80.0, help="radio_b: %% of air_rate_bps")
    ap.add_argument("--cooldown-skip", type=float, default=30.0, help="settling time of the cooldown")
    ap.add_argument("--min-run", type=float, default=0.98, help="share of the planned time")
    args = ap.parse_args(argv)
    if not args.phases:
        args.phases = f"load:{args.duration if args.duration else 60}"
    return args


def Run(args, meta: dict, mod) -> int:
    socks = {}
    for link, port in (("radio_a", args.listen_port_a), ("radio_b", args.listen_port_b)):
        socks[link] = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        socks[link].bind((args.listen_host, port))
    peers = {"radio_a": (args.target, args.peer_port_a), "radio_b": (args.target, args.peer_port_b)}
    st = Station(args, meta, mod, lambda link, data: socks[link].sendto(data, peers[link]))
    stop = []
    for sig in (signal.SIGTERM, signal.SIGINT):
        signal.signal(sig, lambda *_: stop.append(1))
    jsonl = open(args.jsonl, "w", encoding="utf-8") if args.jsonl else None  # pylint: disable=consider-using-with
    by_fd = {s.fileno(): ln for ln, s in socks.items()}
    st.Start(time.monotonic())
    total = args.duration if args.duration else st.total_s
    try:
        while not stop and time.monotonic() - st.t0 < total:
            for fd in select.select(list(by_fd), [], [], 0.05)[0]:
                data, _ = socks[by_fd[fd]].recvfrom(65535)
                st.OnDatagram(by_fd[fd], data, time.monotonic())
            line = st.Tick(time.monotonic())
            if line and jsonl:
                jsonl.write(json.dumps(line, separators=(",", ":")) + "\n")
                jsonl.flush()
    finally:
        if jsonl:
            jsonl.close()
        for s in socks.values():
            s.close()
    st.Finish(time.monotonic())
    summary = st.Summary(finished=not stop)
    print(Report(summary), flush=True)
    if args.summary:
        with open(args.summary, "w", encoding="utf-8") as f:
            json.dump(summary, f, indent=1)
    return 0 if summary["verdict"] == "PASS" else 1


def main(argv=None) -> int:
    args = Parse(argv)
    dialect = args.dialect_py or FindData("tests/mavlink/udp_cpu/srp_example.py")
    meta_path = args.meta or FindData("tests/mavlink/udp_cpu/srp_example_monitor.json")
    args.daemon_config = args.daemon_config or FindData("tests/mavlink/udp_cpu/mavlink_daemon_config.json")
    if not dialect or not meta_path:
        print("soak_station: no dialect (--dialect-py, --meta)", file=sys.stderr)
        return 2
    with open(meta_path, encoding="utf-8") as f:
        meta = json.load(f)
    return Run(args, meta, mm.LoadDialect(dialect))


if __name__ == "__main__":
    sys.exit(main())
