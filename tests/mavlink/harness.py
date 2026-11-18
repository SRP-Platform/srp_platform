"""Harness of the end-to-end tests.

`JsonProc` drives a process that speaks JSON lines (the daemon host), `GS` is the ground station
at the far end of a radio link (pymavlink with an optional signature verifier), `Link` is a pair
of ptys with channel impairments, `Scenario` closes everything a test opened.
"""
import importlib.util
import json
import os
import select
import signal
import subprocess
import threading
import time
import tty

import mav_helpers as mh
from channel_sim import ChannelSim


class JsonProc:
    """A process driven by JSON lines: replies carry the request `id`, events (`ev`) are collected."""

    def __init__(self, argv, err_path=None):
        # stderr goes to a file: an unread pipe would fill up and block the process
        self.err_path = err_path or os.devnull
        self._err = open(self.err_path, "ab")
        self.proc = subprocess.Popen(argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=self._err, text=True, bufsize=1)
        self._id, self._replies, self.events = 0, {}, []
        self._cv = threading.Condition()
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        for line in self.proc.stdout:
            try:
                m = json.loads(line)
            except ValueError:
                continue
            with self._cv:
                if "ev" in m:
                    self.events.append(m)
                else:
                    self._replies[m["id"]] = m
                self._cv.notify_all()

    def stderr_tail(self, n=2000):
        try:
            with open(self.err_path, "rb") as f:
                return f.read()[-n:].decode("utf-8", "replace")
        except OSError:
            return ""

    def _wait(self, pred, timeout, what):
        end = time.monotonic() + timeout
        with self._cv:
            while not pred():
                left = end - time.monotonic()
                if left <= 0 or (self.proc.poll() is not None and not pred()):
                    raise RuntimeError(f"{what}: rc={self.proc.poll()} stderr={self.stderr_tail()}")
                self._cv.wait(min(left, 0.2))

    def wait_event(self, name, timeout=15.0):
        self._wait(lambda: any(e.get("ev") == name for e in self.events), timeout,
                   f"no event {name}")

    def rpc(self, cmd, timeout=30, **kw):
        self._id += 1
        i = self._id
        self.proc.stdin.write(json.dumps(dict(id=i, cmd=cmd, **kw)) + "\n")
        self.proc.stdin.flush()
        self._wait(lambda: i in self._replies, timeout, f"no reply to {cmd}")
        with self._cv:
            r = self._replies.pop(i)
        if not r.get("ok"):
            raise RuntimeError(f"{cmd}: {r.get('error')}")
        return r.get("result")

    def terminate(self, wait=8.0):
        """Graceful stop (the `stop` command, then SIGKILL after `wait` seconds)."""
        try:
            self.proc.stdin.write(json.dumps(dict(cmd="stop")) + "\n")
            self.proc.stdin.flush()
            self.proc.stdin.close()
            self.proc.wait(timeout=wait)
        except Exception:
            self.kill()

    def kill(self):
        if self.proc.poll() is None:
            self.proc.send_signal(signal.SIGKILL)
        try:
            self.proc.wait(timeout=5)
        except Exception:
            pass


def dialect_file(ext):
    """A file of the CPU dialect (.py, .lua, .xml) from the $EXAMPLE_DIALECT list of paths."""
    for path in os.environ["EXAMPLE_DIALECT"].split():
        if path.endswith(ext):
            return path
    raise AssertionError(f"the dialect rule has no {ext} output")


def load_dialect_module():
    """The pymavlink module generated for the CPU dialect."""
    spec = importlib.util.spec_from_file_location("cpu_dialect", dialect_file(".py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def wait_until(pred, timeout=3.0, step=0.005):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if pred():
            return True
        time.sleep(step)
    return False


class GS:
    """Ground station: a pymavlink parser on a pty, with an optional signature verifier.

    `mod` is the pymavlink module generated for the CPU dialect. `messages` holds the accepted
    frames, `rejected` those the verifier refused, `bad` the bytes pymavlink could not parse.
    """

    def __init__(self, path, mod, key=None, link_id=0, sysid=255, compid=190):
        self.mod, self.key = mod, key
        self.mav, self.sink = mh.make_mav(mod, sysid, compid, key, link_id)
        self.parser, _ = mh.make_mav(mod, sysid, compid, None)
        self.verifier = mh.StrictVerifier(key, mh.signing_timestamp_now()) if key else None
        self.messages, self.rejected, self.bad, self.raw = [], [], 0, bytearray()
        self._stop = threading.Event()
        self.lock = threading.Lock()
        self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        tty.setraw(self.fd)
        self.thread = threading.Thread(target=self._rx, daemon=True)
        self.thread.start()

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
                    self.bad += 1
                    continue
                raw = bytes(m.get_msgbuf())
                verdict = self.verifier.check(raw) if self.verifier else "ok"
                rec = dict(type=m.get_type(), d=m.to_dict(), raw=raw, t=time.monotonic(),
                           verdict=verdict, sysid=m.get_srcSystem(), compid=m.get_srcComponent())
                (self.messages if verdict == "ok" else self.rejected).append(rec)

    def send(self, msg, timestamp=None):
        """Encodes (signed when the GS has a key) and writes the frame; returns its bytes."""
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
        self.thread.join(timeout=1)
        try:
            os.close(self.fd)
        except OSError:
            pass


class Link:
    """A radio link: the daemon opens `daemon_dev`, the GS opens `gs_dev`."""

    def __init__(self, **sim_kw):
        self.sim = ChannelSim(**sim_kw).start()
        self.daemon_dev, self.gs_dev = self.sim.path_a, self.sim.path_b

    def stop(self):
        self.sim.stop()


class Scenario:
    """Collects what a test starts and closes it in reverse order."""

    def __init__(self):
        self._closers = []

    def add(self, obj, closer="stop"):
        self._closers.append(getattr(obj, closer))
        return obj

    def close(self):
        for c in reversed(self._closers):
            try:
                c()
            except Exception:
                pass
