"""Interfejs peera MAVLink dla scenariuszy E2E: proces C++ (cc_binary na prawdziwym runtime) sterowany przez JSON lines
(stdin/stdout), topologia testowa (ChannelSim) i GS na pymavlink. Protokol peera: tests/mavlink/README.md.
`MAVLINK_PEER` / `MAVLINK_PEER_V1` = binarki peera (v2 / v1), ustawiane przez BUILD.
"""
import json
import os
import select
import subprocess
import sys
import threading
import time
import tty

import mav_helpers as mh
from channel_sim import ChannelSim


def impls():
    return ["cpp"]  # jedyna implementacja peera: proces C++ na prawdziwym runtime


def primary_impl():
    return "cpp"


def make_peer(impl, config):
    assert impl == "cpp", impl
    v1 = all(l.get("version", 2) == 1 for l in config["links"])
    return ProcPeer([os.environ["MAVLINK_PEER_V1" if v1 else "MAVLINK_PEER"]], config)


class ProcPeer:
    """Wrapper peera uruchamianego jako proces; protokol: tests/mavlink/README.md (JSON lines)."""

    def __init__(self, argv, config):
        env = dict(os.environ, PYTHONPATH=os.pathsep.join(p for p in sys.path if p))
        self.cfg = config
        self.proc = subprocess.Popen(argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                                     bufsize=1, env=env)
        self._id, self._replies, self.events_rx, self.req_handled = 0, {}, [], []
        self._cv = threading.Condition()
        self._reader = threading.Thread(target=self._read, daemon=True)
        self._reader.start()

    def _read(self):
        for line in self.proc.stdout:
            try:
                m = json.loads(line)
            except ValueError:
                continue
            if m.get("ev") == "event":
                self.events_rx.append(m["event"])
            elif m.get("ev") == "handled":
                self.req_handled.append(tuple(m["handled"]))
            else:
                with self._cv:
                    self._replies[m["id"]] = m
                    self._cv.notify_all()

    def _rpc(self, cmd, timeout=30, **kw):
        self._id += 1
        i = self._id
        self.proc.stdin.write(json.dumps(dict(id=i, cmd=cmd, **kw)) + "\n")
        self.proc.stdin.flush()
        end = time.monotonic() + timeout
        with self._cv:
            while i not in self._replies:
                left = end - time.monotonic()
                if left <= 0 or self.proc.poll() is not None and i not in self._replies:
                    raise RuntimeError(f"peer nie odpowiada na {cmd}: rc={self.proc.poll()} "
                                       f"stderr={self.proc.stderr.read() if self.proc.poll() is not None else ''}")
                self._cv.wait(min(left, 0.2))
            r = self._replies.pop(i)
        if not r.get("ok"):
            raise RuntimeError(f"peer {cmd}: {r.get('error')}")
        return r.get("result")

    def start(self):
        self._rpc("start", config=self.cfg)
        return self

    def emit(self, instance, element, **values):
        self._rpc("emit", instance=instance, element=element, values=values)

    def call(self, instance, method, timeout=1.0, **args):
        return tuple(self._rpc("call", timeout=timeout + 30, instance=instance, method=method, call_timeout=timeout,
                               args=args))

    def register_transport(self, name, device):
        self._rpc("register_transport", name=name, device=device)

    @property
    def stats(self):
        return self._rpc("stats")

    def rss_kb(self):
        return self._rpc("rss")

    def stop(self):
        try:
            self._rpc("stop", timeout=10)
        except Exception:
            pass
        try:
            self.proc.stdin.close()
            self.proc.wait(timeout=5)
        except Exception:
            self.proc.kill()


def peer_rss_kb(peer):
    return peer.rss_kb()


# --- konfiguracja ---------------------------------------------------------------------------------------------
def link_cfg(name, device, version=2, key=None, link_id=0, accept_unsigned=False, hb=0, state_file=None):
    return dict(name=name, device=device, version=version, key_hex=key.hex() if key else None,
                link_id=link_id, accept_unsigned=accept_unsigned, heartbeat_hz=hb, state_file=state_file)


def inst_cfg(instance, links, exclude=(), max_rate_hz=None, dedup_ttl_ms=10000, **kw):
    return dict(instance=instance, links=list(links), exclude=list(exclude), max_rate_hz=dict(max_rate_hz or {}),
                dedup_ttl_ms=dedup_ttl_ms, **kw)


def peer_cfg(links, instances, sysid=1, compid=1, method_timeout_ms=100, **kw):
    """method_timeout_ms: timeout metod proxy (peer C++ bierze go z konfiguracji procesu zamiast z modelu = 1000 ms)."""
    return dict(sysid=sysid, compid=compid, links=links, instances=instances, method_timeout_ms=method_timeout_ms, **kw)


def wait_until(pred, timeout=3.0, step=0.005):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if pred():
            return True
        time.sleep(step)
    return False


# --- GS (pymavlink na koncu B lacza) -------------------------------------------------------------------------------
class GS:
    """Stacja naziemna: pymavlink parser + (opcjonalnie) weryfikator podpisu wg spec. Zbiera komunikaty i surowe bajty."""

    def __init__(self, path, version=2, key=None, link_id=0, sysid=255, compid=190, accept_unsigned=False, verify=None):
        self.path, self.version, self.key = path, version, key
        self.mod = mh.load_dialect(version)
        self.mav, self.sink = mh.make_mav(self.mod, sysid, compid, key, link_id)
        self.parser, _ = mh.make_mav(self.mod, sysid, compid, None)
        self.verifier = mh.StrictVerifier(key, mh.signing_timestamp_now(), accept_unsigned) if (key and verify is not False) else None
        self.messages, self.rejected, self.raw = [], [], bytearray()
        self._stop = threading.Event()
        self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        tty.setraw(self.fd)
        self.lock = threading.Lock()
        self.t = threading.Thread(target=self._rx, daemon=True)
        self.t.start()

    def _rx(self):
        while not self._stop.is_set():
            if not select.select([self.fd], [], [], 0.01)[0]:
                continue
            try:
                data = os.read(self.fd, 4096)
            except OSError:
                continue
            self.raw += data
            for m in self.parser.parse_buffer(data) or []:
                if m.get_type() == "BAD_DATA":
                    continue
                raw = bytes(m.get_msgbuf())
                verdict = self.verifier.check(raw) if self.verifier else "ok"
                rec = dict(type=m.get_type(), d=m.to_dict(), raw=raw, t=time.monotonic(), verdict=verdict,
                           sysid=m.get_srcSystem(), compid=m.get_srcComponent())
                (self.messages if verdict == "ok" else self.rejected).append(rec)

    def send(self, msg, key="default", link_id=None, timestamp=None):
        """Koduje (z podpisem GS, jesli ustawiony) i wysyla; zwraca wyslane bajty."""
        with self.lock:
            self.sink.buf.clear()
            if self.key:
                self.mav.signing.timestamp = timestamp if timestamp is not None else max(
                    mh.signing_timestamp_now(), self.mav.signing.timestamp)
            self.mav.send(msg)
            data = bytes(self.sink.buf)
        self.send_raw(data)
        return data

    def send_raw(self, data):
        view = memoryview(data)
        end = time.monotonic() + 1.0
        while view and time.monotonic() < end:
            try:
                view = view[os.write(self.fd, view):]
            except BlockingIOError:
                select.select([], [self.fd], [], 0.01)

    def of(self, type_name):
        return [m for m in self.messages if m["type"] == type_name]

    def stop(self):
        self._stop.set()
        self.t.join(timeout=1)
        try:
            os.close(self.fd)
        except OSError:
            pass


class Link:
    """Para peer<->GS przez ChannelSim: peer na A, GS na B."""

    def __init__(self, **sim_kw):
        self.sim = ChannelSim(**sim_kw).start()

    @property
    def peer_dev(self):
        return self.sim.path_a

    @property
    def gs_dev(self):
        return self.sim.path_b

    def stop(self):
        self.sim.stop()


def record(name, **numbers):
    """Zapisuje liczby ze scenariuszy (raport testu): linia JSON do $MAVLINK_REPORT albo $TEST_UNDECLARED_OUTPUTS_DIR."""
    path = os.environ.get("MAVLINK_REPORT") or os.path.join(os.environ.get("TEST_UNDECLARED_OUTPUTS_DIR", "."), "t2_report.jsonl")
    try:
        with open(path, "a", encoding="utf-8") as f:
            f.write(json.dumps(dict(test=name, impl=primary_impl(), **numbers)) + "\n")
    except OSError:
        pass
    print("NUMBERS", name, json.dumps(numbers))


class Scenario:
    """Zbiera zasoby scenariusza (lacza, peery, GS) i zamyka je w odwrotnej kolejnosci."""

    def __init__(self, impl):
        self.impl, self._closers = impl, []

    def link(self, **kw):
        l = Link(**kw)
        self._closers.append(l.stop)
        return l

    def peer(self, cfg, impl=None):
        p = make_peer(impl or self.impl, cfg).start()
        self._closers.append(p.stop)
        return p

    def gs(self, path, **kw):
        g = GS(path, **kw)
        self._closers.append(g.stop)
        return g

    def close(self):
        for c in reversed(self._closers):
            try:
                c()
            except Exception:
                pass
