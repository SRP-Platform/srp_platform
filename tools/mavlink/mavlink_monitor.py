"""Ground monitor of a MAVLink 2 stream: events with their frequency, methods, the channel.

The runtime of the generated `<dialect>_monitor.py`: that script holds the metadata of the model
(elements, instances, `max_rate_hz` per link, `air_rate_bps`) and calls `main(meta, dialect_py)`.
Only the standard library and the pymavlink module of the dialect are used.

Input: `--serial PORT --baud N` (a UART or pty, like `serial2udp`) or `--udp [HOST:]PORT`
(listens; one datagram = one frame, like `example_udp_radio`). The monitor is passive: it counts
every frame it is given, in both directions when it is attached to a tap of the link.

Views: curses table (default on a terminal), `--plain` (text table every second), `--jsonl`
(one JSON object per second, for logs). The numbers:

  * events: frames, frequency (sliding window of 5 s and the average since the first frame),
    `max_rate_hz` of the model (a frame rate above the limit + 10 % is marked `!`), age of the
    last frame, the last decoded values;
  * methods: REQ / RESP, pairs matched by (element, instance, payload `seq`, requester), REQs
    without an answer after the timeout, latency, `status` of the RESPs;
  * channel: bytes per second and the share of `air_rate_bps`, CRC errors, unknown msgid, v1
    frames, repeated payload `seq`, frames outside the model, signatures (ok / bad / unsigned).
"""
from __future__ import annotations

import argparse
import collections
import hashlib
import importlib.util
import json
import os
import select
import socket
import sys
import time

import serial2udp

WINDOW_S = 5.0
OVER_LIMIT = 1.10  # an event faster than max_rate_hz * 1.10 is marked
DEFAULT_TIMEOUT_MS = 1000
STX_V1, STX_V2 = 0xFE, 0xFD
SIGNED_FLAG = 0x01
HEADER_V2, SIGNATURE_LEN = 10, 13
VALUE_WIDTH = 60


def Crc16(data: bytes, crc_extra: int) -> int:
    """MAVLink X.25 CRC over `data` and the CRC extra byte."""
    crc = 0xFFFF
    for byte in data + bytes([crc_extra]):
        tmp = byte ^ (crc & 0xFF)
        tmp = (tmp ^ (tmp << 4)) & 0xFF
        crc = ((crc >> 8) ^ (tmp << 8) ^ (tmp << 3) ^ (tmp >> 4)) & 0xFFFF
    return crc


def Signature(key: bytes, frame: bytes) -> bytes:
    """The 48-bit signature of a signed frame: the first 6 bytes of SHA-256(key + frame without it)."""
    return hashlib.sha256(key + frame[:-6]).digest()[:6]


class Frame:
    """A split frame: `kind` is v2 | v1 | garbage; `data` the bytes."""

    def __init__(self, kind: str, data: bytes) -> None:
        self.kind = kind
        self.data = data

    @property
    def msg_id(self) -> int:
        return self.data[7] | self.data[8] << 8 | self.data[9] << 16

    @property
    def signed(self) -> bool:
        return bool(self.data[2] & SIGNED_FLAG)


class Splitter:
    """Cuts a byte stream (or a datagram) into frames; never raises.

    A MAVLink 2 frame is cut by its length byte; the CRC is checked by the monitor (it needs the
    dialect). `Resync()` drops one byte after a bad CRC so that the next frame is found even when
    the length byte of the bad one was wrong. Bytes outside any frame are counted.
    """

    def __init__(self) -> None:
        self.buf = bytearray()
        self.garbage = 0

    @staticmethod
    def _length(buf) -> int | None:
        """Length of the frame at buf[0]: None = more bytes needed, 0 = no frame starts here."""
        if buf[0] == STX_V2:
            if len(buf) < 3:
                return None
            return HEADER_V2 + buf[1] + 2 + (SIGNATURE_LEN if buf[2] & SIGNED_FLAG else 0)
        if buf[0] == STX_V1:
            if len(buf) < 2:
                return None
            return 6 + buf[1] + 2
        return 0

    def Add(self, data: bytes) -> None:
        self.buf += data

    def Next(self) -> Frame | None:
        """The next complete frame of the buffer, None when more bytes are needed."""
        while self.buf:
            n = self._length(self.buf)
            if n is None or (n and len(self.buf) < n):
                return None
            if n == 0:
                self.garbage += 1
                del self.buf[0]
                continue
            frame = Frame("v2" if self.buf[0] == STX_V2 else "v1", bytes(self.buf[:n]))
            del self.buf[:n]
            return frame
        return None

    def Resync(self, frame: Frame) -> None:
        """After a bad CRC: look for the next frame inside the bytes of the bad one."""
        self.buf[:0] = frame.data[1:]
        self.garbage += 1

    def Datagram(self, data: bytes) -> Frame | None:
        """One datagram = one frame: bytes that do not belong to it are garbage."""
        self.buf.clear()
        self.Add(data)
        frame = self.Next()
        self.garbage += len(self.buf)
        self.buf.clear()
        return frame


# ---------------------------------------------------------------- statistics

class Rate:
    """Frame times of the last WINDOW_S seconds and the count since the start."""

    def __init__(self) -> None:
        self.times: collections.deque = collections.deque()
        self.count = 0
        self.first: float | None = None
        self.last: float | None = None

    def Add(self, now: float) -> None:
        self.count += 1
        self.first = now if self.first is None else self.first
        self.last = now
        self.times.append(now)

    def Window(self, now: float) -> int:
        while self.times and self.times[0] < now - WINDOW_S:
            self.times.popleft()
        return len(self.times)

    def WindowHz(self, now: float) -> float:
        if self.first is None:
            return 0.0
        return self.Window(now) / min(WINDOW_S, max(now - self.first, 1e-6))

    def AvgHz(self) -> float:
        if self.first is None or self.count < 2:
            return 0.0
        return (self.count - 1) / max(self.last - self.first, 1e-6)


class EventStat:
    def __init__(self, element: str, instance: int, src: tuple[int, int], limit: float,
                 listed: bool) -> None:
        self.element, self.instance, self.src = element, instance, src
        self.limit = limit  # max_rate_hz of the model (0 = none)
        self.listed = listed  # the model lists this (element, instance)
        self.rate = Rate()
        self.last_seq: int | None = None
        self.values: dict = {}


class MethodStat:
    def __init__(self, element: str, instance: int, requester: tuple[int, int]) -> None:
        self.element, self.instance, self.requester = element, instance, requester
        self.req = self.resp = self.paired = self.unanswered = 0
        self.lat: list[float] = []
        self.status: collections.Counter = collections.Counter()
        self.pending: dict[int, float] = {}  # payload seq -> time of the REQ
        self.last_seq: int | None = None


class Monitor:
    """Counts the frames of one link; `OnBytes` / `OnDatagram` take what the source delivers."""

    def __init__(self, meta: dict, dialect, link: str | None = None, key: bytes | None = None,
                 sysid: int | None = None, compid: int | None = None,
                 timeout_ms: float = DEFAULT_TIMEOUT_MS, air_rate_bps: int | None = None) -> None:
        self.meta, self.mod, self.link, self.key = meta, dialect, link, key
        self.filter = (sysid, compid)
        self.timeout_s = timeout_ms / 1000.0
        self.parser = dialect.MAVLink(None)
        self.splitter = Splitter()
        self.start = time.monotonic()
        self.cpus = {(c["system_id"], c["component_id"]): c for c in meta["cpus"]
                     if c.get("system_id") is not None}
        self.msgs: dict[int, dict] = {}
        for c in meta["cpus"]:
            for mid, m in c["messages"].items():
                self.msgs[int(mid)] = m
        self.air_rate_bps = air_rate_bps if air_rate_bps is not None else self._air_rate()
        self.events: dict[tuple, EventStat] = {}
        self.methods: dict[tuple, MethodStat] = {}
        self.ch = collections.Counter()
        self.bytes_window: collections.deque = collections.deque()
        self.first_t: float | None = None
        self._Precreate()

    # -- model lookups

    def _air_rate(self) -> int:
        rates = {c["links"][self.link]["air_rate_bps"] for c in self.meta["cpus"]
                 if self.link in c["links"]}
        return rates.pop() if len(rates) == 1 else 0

    def _Instances(self, cpu: dict | None, element: str):
        """(cpu, instance entry) pairs of the model for an element, restricted to the link."""
        for c in ([cpu] if cpu else self.meta["cpus"]):
            for el in c["elements"]:
                if el["element"] != element:
                    continue
                for inst in el["instances"]:
                    if self.link is None or self.link in inst["links"]:
                        yield c, inst

    def _Limit(self, cpu, element: str, instance: int) -> tuple[float, bool]:
        rates = [max(inst["links"].values(), default=0.0) if self.link is None
                 else inst["links"][self.link]
                 for _c, inst in self._Instances(cpu, element) if inst["instance"] == instance]
        return (max(rates), True) if rates else (0.0, False)

    def _Precreate(self) -> None:
        """Events the CPU transmits appear in the table before the first frame (a silent one is
        the interesting one)."""
        for (sysid, compid), cpu in self.cpus.items():
            for el in cpu["elements"]:
                if el["kind"] != "event":
                    continue
                for _c, inst in self._Instances(cpu, el["element"]):
                    if inst["direction"] == "out":
                        self._Event(el["element"], inst["instance"], (sysid, compid))

    def _Event(self, element: str, instance: int, src: tuple[int, int]) -> EventStat:
        key = (element, instance, src)
        st = self.events.get(key)
        if st is None:
            limit, listed = self._Limit(self.cpus.get(src), element, instance)
            st = self.events[key] = EventStat(element, instance, src, limit, listed)
        return st

    # -- input

    def OnBytes(self, data: bytes, now: float | None = None) -> None:
        self._Account(data, now)
        before = self.splitter.garbage
        self.splitter.Add(data)
        while (fr := self.splitter.Next()) is not None:
            if not self._Frame(fr, now, stream=True):
                self.splitter.Resync(fr)
        self.ch["garbage_bytes"] += self.splitter.garbage - before

    def OnDatagram(self, data: bytes, now: float | None = None) -> None:
        self._Account(data, now)
        before = self.splitter.garbage
        fr = self.splitter.Datagram(data)
        if fr is None:
            self.ch["short_datagrams"] += 1
        else:
            self._Frame(fr, now)
        self.ch["garbage_bytes"] += self.splitter.garbage - before

    def _Account(self, data: bytes, now: float | None) -> None:
        now = time.monotonic() if now is None else now
        self.first_t = now if self.first_t is None else self.first_t
        self.ch["bytes"] += len(data)
        self.bytes_window.append((now, len(data)))

    # -- one frame; False = not a frame of this link (the caller resynchronizes). A bad CRC is
    # always False; a v1 frame or an unknown msgid (no CRC check possible) only on a byte stream,
    # where the STX is as likely a noise byte as a real frame and its length byte would swallow
    # real frames behind it. A datagram is one frame, so there is nothing to resynchronize.

    def _Frame(self, fr: Frame, now: float | None, stream: bool = False) -> bool:
        now = time.monotonic() if now is None else now
        d = fr.data
        if fr.kind == "v1":
            self.ch["v1_frames"] += 1
            return not stream
        cls = self.mod.mavlink_map.get(fr.msg_id)
        if cls is None:
            self.ch["unknown_msgid"] += 1
            return not stream
        body_end = HEADER_V2 + d[1]
        crc = d[body_end] | d[body_end + 1] << 8
        if Crc16(d[1:body_end], cls.crc_extra) != crc:
            self.ch["crc_errors"] += 1
            return False
        src = (d[5], d[6])
        if self.filter[0] is not None and src[0] != self.filter[0] or \
                self.filter[1] is not None and src[1] != self.filter[1]:
            self.ch["filtered"] += 1
            return True
        if not self._Signature(fr):
            return True
        try:
            msg = self.parser.decode(self._Unsigned(fr, cls.crc_extra))
        except Exception:  # pylint: disable=broad-except  # pymavlink raises MAVError for anything odd
            self.ch["decode_errors"] += 1
            return True
        self.ch["frames"] += 1
        self._Message(msg, fr, src, now)
        return True

    def _Signature(self, fr: Frame) -> bool:
        """Counts ok / bad / unsigned; False = a bad signature, the frame is not counted further."""
        if not fr.signed:
            self.ch["unsigned"] += 1
            return True
        if self.key is None:
            self.ch["signed_unchecked"] += 1
            return True
        if Signature(self.key, fr.data) != fr.data[-6:]:
            self.ch["sig_bad"] += 1
            return False
        self.ch["sig_ok"] += 1
        return True

    @staticmethod
    def _Unsigned(fr: Frame, crc_extra: int) -> bytearray:
        """The frame without its signature (the monitor verified it already); the incompat flag
        is part of the CRC, so the CRC is recomputed."""
        if not fr.signed:
            return bytearray(fr.data)  # pymavlink appends to the buffer it decodes
        data = bytearray(fr.data[:-SIGNATURE_LEN - 2])
        data[2] &= ~SIGNED_FLAG
        data += Crc16(bytes(data[1:]), crc_extra).to_bytes(2, "little")
        return data

    def _Message(self, msg, fr: Frame, src: tuple[int, int], now: float) -> None:
        meta = self.msgs.get(fr.msg_id)
        if meta is None:
            self.ch["out_of_model"] += 1
            return
        instance = getattr(msg, "instance", 0)
        payload_seq = getattr(msg, "seq", 0)
        if meta["role"] == "event":
            st = self._Event(meta["element"], instance, src)
            if not st.listed:
                self.ch["out_of_model"] += 1
            if st.last_seq == payload_seq:
                self.ch["duplicate_seq"] += 1
            st.last_seq = payload_seq
            st.rate.Add(now)
            st.values = {k: v for k, v in msg.to_dict().items()
                         if k not in ("mavpackettype", "seq", "instance")}
            return
        self._Method(meta, msg, instance, payload_seq, src, now)

    def _Method(self, meta: dict, msg, instance: int, seq: int, src: tuple[int, int],
                now: float) -> None:
        if meta["role"] == "req":
            key = (meta["element"], instance, src)
            st = self.methods.setdefault(key, MethodStat(*key))
            if seq == st.last_seq:
                self.ch["duplicate_seq"] += 1
            st.last_seq = seq
            st.req += 1
            st.pending[seq] = now
            return
        requester = (getattr(msg, "target_system", 0), getattr(msg, "target_component", 0))
        key = (meta["element"], instance, requester)
        st = self.methods.setdefault(key, MethodStat(*key))
        st.resp += 1
        st.status[getattr(msg, "status", 0)] += 1
        sent = st.pending.pop(seq, None)
        if sent is None:
            self.ch["resp_without_req"] += 1
            return
        st.paired += 1
        st.lat.append((now - sent) * 1000.0)

    # -- snapshot

    def Snapshot(self, now: float | None = None) -> dict:
        now = time.monotonic() if now is None else now
        for st in self.methods.values():
            for seq, t in list(st.pending.items()):
                if now - t > self.timeout_s:
                    del st.pending[seq]
                    st.unanswered += 1
        while self.bytes_window and self.bytes_window[0][0] < now - WINDOW_S:
            self.bytes_window.popleft()
        events = []
        for st in sorted(self.events.values(), key=lambda s: (s.element, s.instance, s.src)):
            hz = st.rate.WindowHz(now)
            over = bool(st.limit) and now - (st.rate.first or now) >= WINDOW_S and \
                st.rate.Window(now) > st.limit * WINDOW_S * OVER_LIMIT + 1
            events.append({
                "element": st.element, "instance": st.instance, "src": f"{st.src[0]}:{st.src[1]}",
                "frames": st.rate.count, "hz_window": round(hz, 2),
                "hz_avg": round(st.rate.AvgHz(), 2), "max_rate_hz": st.limit,
                "over_limit": over, "in_model": st.listed,
                "age_s": None if st.rate.last is None else round(now - st.rate.last, 2),
                "values": _Short(st.values)})
        methods = []
        for st in sorted(self.methods.values(), key=lambda s: (s.element, s.instance,
                                                               s.requester)):
            lat = st.lat
            methods.append({
                "element": st.element, "instance": st.instance,
                "requester": f"{st.requester[0]}:{st.requester[1]}",
                "req": st.req, "resp": st.resp, "paired": st.paired, "unanswered": st.unanswered,
                "pending": len(st.pending),
                "latency_ms": {"min": round(min(lat), 1), "avg": round(sum(lat) / len(lat), 1),
                               "max": round(max(lat), 1)} if lat else None,
                "status": {str(k): v for k, v in sorted(st.status.items())}})
        span = max(min(WINDOW_S, now - (self.first_t or now)), 1e-6)
        bps = sum(n for _t, n in self.bytes_window) / span if self.first_t else 0.0
        ch = {k: self.ch[k] for k in (
            "bytes", "frames", "crc_errors", "unknown_msgid", "v1_frames", "duplicate_seq",
            "out_of_model", "garbage_bytes", "decode_errors", "filtered", "unsigned",
            "sig_ok", "sig_bad", "signed_unchecked", "resp_without_req", "short_datagrams")}
        ch.update(bytes_per_s=round(bps, 1), air_rate_bps=self.air_rate_bps,
                  air_share_pct=(round(bps * 8 / self.air_rate_bps * 100, 1)
                                 if self.air_rate_bps else None))
        return {"t": round(now - self.start, 2), "link": self.link, "events": events,
                "methods": methods, "channel": ch}


def _Short(values: dict) -> str:
    text = " ".join(f"{k}={v}" for k, v in values.items())
    return text if len(text) <= VALUE_WIDTH else text[:VALUE_WIDTH - 1] + "~"


# ---------------------------------------------------------------- views

def RenderPlain(snap: dict) -> str:
    """The snapshot as text tables (also the body of the curses view)."""
    out = [f"t={snap['t']:.0f}s link={snap['link'] or '*'}", "",
           f"{'EVENT':36} {'inst':>4} {'src':>5} {'frames':>7} {'Hz/5s':>7} {'Hz avg':>7} "
           f"{'limit':>6} {'age':>6}  values"]
    for e in snap["events"]:
        limit = f"{e['max_rate_hz']:g}" if e["max_rate_hz"] else "-"
        mark = "!" if e["over_limit"] else ("?" if not e["in_model"] else " ")
        age = "-" if e["age_s"] is None else f"{e['age_s']:.1f}s"
        out.append(f"{e['element']:36.36} {e['instance']:>4} {e['src']:>5} {e['frames']:>7} "
                   f"{e['hz_window']:>7.2f} {e['hz_avg']:>7.2f} {limit:>6}{mark}{age:>6}  "
                   f"{e['values']}")
    out += ["", f"{'METHOD':36} {'inst':>4} {'from':>5} {'REQ':>5} {'RESP':>5} {'pair':>5} "
                f"{'lost':>5} {'lat min/avg/max ms':>22}  status"]
    for m in snap["methods"]:
        lat = m["latency_ms"]
        text = f"{lat['min']:g}/{lat['avg']:g}/{lat['max']:g}" if lat else "-"
        status = " ".join(f"{k}:{v}" for k, v in m["status"].items()) or "-"
        out.append(f"{m['element']:36.36} {m['instance']:>4} {m['requester']:>5} {m['req']:>5} "
                   f"{m['resp']:>5} {m['paired']:>5} {m['unanswered']:>5} {text:>22}  {status}")
    c = snap["channel"]
    share = f"{c['air_share_pct']}% of {c['air_rate_bps']} bps" if c["air_rate_bps"] else "no air_rate_bps"
    out += ["", f"CHANNEL {c['bytes_per_s']:.0f} B/s ({share}), {c['bytes']} B, {c['frames']} frames",
            f"  crc_errors={c['crc_errors']} unknown_msgid={c['unknown_msgid']} "
            f"v1_frames={c['v1_frames']} duplicate_seq={c['duplicate_seq']} "
            f"out_of_model={c['out_of_model']} garbage_bytes={c['garbage_bytes']}",
            f"  signatures: ok={c['sig_ok']} bad={c['sig_bad']} unsigned={c['unsigned']}"
            + (f" unchecked={c['signed_unchecked']}" if c["signed_unchecked"] else "")]
    return "\n".join(out)


# ---------------------------------------------------------------- sources

class Source:
    """A file descriptor with the way the monitor reads it."""

    def __init__(self, fd, datagrams: bool, closer) -> None:
        self.fd, self.datagrams, self.closer = fd, datagrams, closer

    def Read(self) -> bytes:
        if self.datagrams:
            return self.fd.recv(65535)
        return os.read(self.fd, 4096)

    def Fileno(self) -> int:
        return self.fd if isinstance(self.fd, int) else self.fd.fileno()

    def Close(self) -> None:
        self.closer()


def OpenUdp(spec: str) -> Source:
    host, _, port = spec.rpartition(":")
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((host or "0.0.0.0", int(port)))
    return Source(sock, True, sock.close)


def OpenSerial(path: str, baud: int) -> Source:
    fd = serial2udp.open_port(path, baud)
    return Source(fd, False, lambda: os.close(fd))


def LoadDialect(path: str):
    spec = importlib.util.spec_from_file_location("monitor_dialect", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def _Timeout(path: str | None, link: str | None) -> float:
    """`method_timeout_ms` of the daemon config: of the link, or the longest of all links."""
    if not path:
        return DEFAULT_TIMEOUT_MS
    with open(path, encoding="utf-8") as f:
        links = json.load(f).get("links", {})
    values = [lk.get("method_timeout_ms", DEFAULT_TIMEOUT_MS) for n, lk in links.items()
              if link in (None, n)]
    return max(values, default=DEFAULT_TIMEOUT_MS)


def _Loop(mon: Monitor, src: Source, show, duration: float | None, poll=None) -> None:
    """Reads the source until `duration` seconds pass, `poll()` returns True or Ctrl-C; shows a
    snapshot every second and once at the end."""
    started = nxt = time.monotonic()
    try:
        while True:
            now = time.monotonic()
            if duration is not None and now - started >= duration:
                break
            if poll is not None and poll():
                break
            if now >= nxt:
                show(mon.Snapshot(now))
                nxt += 1.0
            if select.select([src.Fileno()], [], [], max(0.0, min(0.1, nxt - now)))[0]:
                data = src.Read()
                if not data and not src.datagrams:
                    break  # the port was closed
                (mon.OnDatagram if src.datagrams else mon.OnBytes)(data)
    except KeyboardInterrupt:
        pass
    show(mon.Snapshot())


def _Curses(mon: Monitor, src: Source, duration: float | None) -> None:
    import curses  # pylint: disable=import-outside-toplevel

    def run(scr):
        curses.curs_set(0)
        scr.nodelay(True)

        def show(snap):
            scr.erase()
            h, w = scr.getmaxyx()
            lines = RenderPlain(snap).splitlines() + ["", "q = quit"]
            for i, line in enumerate(lines[:h - 1]):
                scr.addnstr(i, 0, line, w - 1)
            scr.refresh()
        _Loop(mon, src, show, duration, poll=lambda: scr.getch() in (ord("q"), ord("Q")))
    curses.wrapper(run)


def Parse(argv) -> argparse.Namespace:
    ap = argparse.ArgumentParser(
        description="Ground monitor of a MAVLink dialect: event frequency, methods, channel.")
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--serial", metavar="PORT", help="UART or pty, for example /dev/ttyUSB0")
    src.add_argument("--udp", metavar="[HOST:]PORT", help="listen on a UDP port (one frame per datagram)")
    ap.add_argument("--baud", type=int, default=57600, choices=sorted(serial2udp.BAUDS))
    ap.add_argument("--link", help="the radio link of the model this input is (limits, air_rate_bps)")
    ap.add_argument("--key", metavar="FILE", help="signing key (64 hex characters): verify signatures")
    ap.add_argument("--sysid", type=int, help="count only frames of this system_id")
    ap.add_argument("--compid", type=int, help="count only frames of this component_id")
    ap.add_argument("--daemon-config", metavar="FILE",
                    help="mavlink_daemon_config.json: method_timeout_ms (default 1000)")
    ap.add_argument("--dialect-py", metavar="FILE", help="the pymavlink module of the dialect")
    ap.add_argument("--duration", type=float, metavar="S", help="stop after S seconds")
    view = ap.add_mutually_exclusive_group()
    view.add_argument("--plain", action="store_true", help="a text table every second")
    view.add_argument("--jsonl", action="store_true", help="one JSON object per second")
    return ap.parse_args(argv)


def main(meta: dict, dialect_py: str, argv=None) -> int:
    args = Parse(argv)
    links = {ln for c in meta["cpus"] for ln in c["links"]}
    if args.link and args.link not in links:
        print(f"monitor: unknown link '{args.link}' (model links: {', '.join(sorted(links))})",
              file=sys.stderr)
        return 2
    key = None
    if args.key:
        with open(args.key, encoding="utf-8") as f:
            key = bytes.fromhex(f.read().strip())
    mon = Monitor(meta, LoadDialect(args.dialect_py or dialect_py), args.link, key, args.sysid,
                  args.compid, _Timeout(args.daemon_config, args.link))
    src = OpenUdp(args.udp) if args.udp else OpenSerial(args.serial, args.baud)
    try:
        if args.jsonl:
            _Loop(mon, src, lambda s: print(json.dumps(s, separators=(",", ":")), flush=True),
                  args.duration)
        elif args.plain or not sys.stdout.isatty():
            _Loop(mon, src, lambda s: print(RenderPlain(s) + "\n", flush=True), args.duration)
        else:
            _Curses(mon, src, args.duration)
    finally:
        src.Close()
    return 0
