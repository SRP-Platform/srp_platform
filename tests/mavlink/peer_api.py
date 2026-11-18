"""Interfejs peera MAVLink dla scenariuszy E2E: DWA procesy na "CPU" testu - MavlinkDaemon (`daemon_host`, ten sam
rdzen co produkcyjny daemon, bez EM) i aplikacja (peer C++, klient daemona) - sterowane przez JSON lines (stdin/stdout),
topologia testowa (ChannelSim) i GS na pymavlink. Protokol peera: tests/mavlink/README.md.
`MAVLINK_PEER` = binarka peera, `MAVLINK_DAEMON_HOST` = binarka hosta daemona (ustawiane przez BUILD).
"""
import json
import os
import select
import shutil
import signal
import subprocess
import sys
import tempfile
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
    # v1: osobna binarka z ID wiadomosci <= 255 (model peer_model_v1); wersje lacza ustawia config daemona
    v1 = all(l.get("version", 2) == 1 for l in config["links"])
    return ProcPeer([os.environ["MAVLINK_PEER_V1" if v1 else "MAVLINK_PEER"]], config)


class JsonProc:
    """Proces sterowany JSON lines: odpowiedzi po `id`, zdarzenia (`ev`) trafiaja do `on_event`."""

    def __init__(self, argv, on_event=None, err_path=None):
        env = dict(os.environ, PYTHONPATH=os.pathsep.join(p for p in sys.path if p))
        # stderr do pliku: nieodczytywany pipe zapelnilby sie (logi daemona/peera) i zablokowal proces
        self.err_path = err_path or os.devnull
        self._err = open(self.err_path, "ab")
        self.proc = subprocess.Popen(argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self._err, text=True,
                                     bufsize=1, env=env)
        self._id, self._replies, self.events = 0, {}, []
        self._on_event = on_event
        self._cv = threading.Condition()
        self._reader = threading.Thread(target=self._read, daemon=True)
        self._reader.start()

    def _read(self):
        for line in self.proc.stdout:
            try:
                m = json.loads(line)
            except ValueError:
                continue
            if "ev" in m:
                if self._on_event is not None and self._on_event(m):
                    continue
                with self._cv:
                    self.events.append(m)
                    self._cv.notify_all()
            else:
                with self._cv:
                    self._replies[m["id"]] = m
                    self._cv.notify_all()

    def stderr_tail(self, n=2000):
        try:
            with open(self.err_path, "rb") as f:
                return f.read()[-n:].decode("utf-8", "replace")
        except OSError:
            return ""

    def wait_event(self, name, timeout=15.0):
        end = time.monotonic() + timeout
        with self._cv:
            while not any(e.get("ev") == name for e in self.events):
                left = end - time.monotonic()
                if left <= 0 or self.proc.poll() is not None and not any(e.get("ev") == name for e in self.events):
                    err = [e for e in self.events if e.get("ev") == "error"]
                    raise RuntimeError(f"brak zdarzenia {name}: rc={self.proc.poll()} {err} stderr={self.stderr_tail()}")
                self._cv.wait(min(left, 0.2))

    def rpc(self, cmd, timeout=30, **kw):
        self._id += 1
        i = self._id
        self.proc.stdin.write(json.dumps(dict(id=i, cmd=cmd, **kw)) + "\n")
        self.proc.stdin.flush()
        end = time.monotonic() + timeout
        with self._cv:
            while i not in self._replies:
                left = end - time.monotonic()
                if left <= 0 or self.proc.poll() is not None and i not in self._replies:
                    raise RuntimeError(f"proces nie odpowiada na {cmd}: rc={self.proc.poll()} stderr={self.stderr_tail()}")
                self._cv.wait(min(left, 0.2))
            r = self._replies.pop(i)
        if not r.get("ok"):
            raise RuntimeError(f"{cmd}: {r.get('error')}")
        return r.get("result")

    def terminate(self, graceful_cmd="stop", wait=8.0):
        """Lagodne zatrzymanie (komenda + zamkniecie stdin), po czasie SIGKILL."""
        try:
            self.proc.stdin.write(json.dumps(dict(cmd=graceful_cmd)) + "\n")
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


class ProcPeer:
    """Aplikacja (peer C++) + jej MavlinkDaemon; protokol peera: tests/mavlink/README.md (JSON lines).

    `config` (jak dotad): sysid/compid, links[] (name, device, version, key_hex, link_id, accept_unsigned, heartbeat_hz,
    state_file), instances[]. `links` trafiaja do configu DAEMONA (driver `serial_radio` na urzadzeniu pty), `instances` do
    aplikacji. Liczniki `stats` = liczniki daemona (lacza, duplikaty) + liczniki instancji aplikacji.
    """

    def __init__(self, argv, config, baudrate=921600, daemon_from=None):
        """daemon_from: inny ProcPeer, ktorego daemon ma obslugiwac tez te aplikacje (dwie aplikacje na jednym CPU)."""
        self.cfg, self.argv, self.baudrate = config, argv, baudrate
        self.owner = daemon_from
        self.work = daemon_from.work if daemon_from else tempfile.mkdtemp(prefix="mavpeer_")
        self.socket = daemon_from.socket if daemon_from else os.path.join(self.work, "daemon.sock")
        self.daemon = None
        self.app = None
        self.events_rx, self.req_handled = [], []

    def _daemon_proc(self):
        return (self.owner or self).daemon

    # --- daemon ---
    def _daemon_config(self):
        links = {}
        for l in self.cfg["links"]:
            if not l.get("device"):
                raise RuntimeError(f"lacze {l['name']} bez urzadzenia: daemon otwiera urzadzenie przez driver")
            drv = os.path.join(self.work, f"{l['name']}.drv.json")
            with open(drv, "w", encoding="utf-8") as f:
                json.dump({"device": l["device"], "baudrate": self.baudrate}, f)
            link = {"driver": "serial_radio", "driver_config": drv, "version": l.get("version", 2),
                    "heartbeat_hz": l.get("heartbeat_hz", 0)}
            if l.get("key_hex"):
                key_path = os.path.join(self.work, f"{l['name']}.key")
                with open(key_path, "w", encoding="utf-8") as f:
                    f.write(l["key_hex"])
                os.chmod(key_path, 0o600)
                link["signing"] = {"enabled": True, "key_file": key_path, "link_id": l.get("link_id", 0),
                                   "accept_unsigned": l.get("accept_unsigned", False)}
                if l.get("state_file"):
                    link["signing"]["state_file"] = l["state_file"]
            links[l["name"]] = link
        return {"system_id": self.cfg.get("sysid", 1), "component_id": self.cfg.get("compid", 1),
                "socket_path": self.socket, "links": links}

    def start_daemon(self):
        path = os.path.join(self.work, "mavlink_daemon_config.json")
        with open(path, "w", encoding="utf-8") as f:
            json.dump(self._daemon_config(), f)
        self.daemon = JsonProc([os.environ["MAVLINK_DAEMON_HOST"], path], err_path=os.path.join(self.work, "daemon.err"))
        self.daemon.wait_event("ready")
        return self

    def kill_daemon(self):
        """SIGKILL daemona (bez zapisu stanu): aplikacja ma sama polaczyc sie i zarejestrowac po jego powrocie."""
        if self._daemon_proc() is not None:
            self._daemon_proc().kill()

    def stop_daemon(self):
        if self.daemon is not None:
            self.daemon.terminate()

    def restart_daemon(self):
        self.kill_daemon()
        return self.start_daemon()

    @property
    def daemon_stats(self):
        return self._daemon_proc().rpc("stats")

    # --- aplikacja ---
    def _on_app_event(self, m):
        if m.get("ev") == "event":
            self.events_rx.append(m["event"])
            return True
        if m.get("ev") == "handled":
            self.req_handled.append(tuple(m["handled"]))
            return True
        return False

    def start_app(self, wait_ready=True):
        self.app = JsonProc(self.argv, on_event=self._on_app_event, err_path=os.path.join(self.work, f"app{id(self)}.err"))
        self.app.rpc("start", config=dict(self.cfg, socket=self.socket))
        if wait_ready and self.cfg.get("instances") and not self.app.rpc("ready", timeout_ms=10000, timeout=20):
            raise RuntimeError("instancje nie zarejestrowaly sie w daemonie (albo lacze niegotowe): "
                               f"app: {self.app.stderr_tail()} daemon: {self._daemon_proc().stderr_tail()}")
        return self

    def kill_app(self):
        """SIGKILL aplikacji (bez wyrejestrowania): daemon widzi tylko zamkniecie gniazda."""
        if self.app is not None:
            self.app.kill()

    def start(self, wait_ready=True):
        if self.owner is None:
            self.start_daemon()
        return self.start_app(wait_ready)

    def _rpc(self, cmd, timeout=30, **kw):
        return self.app.rpc(cmd, timeout=timeout, **kw)

    def emit(self, instance, element, **values):
        self._rpc("emit", instance=instance, element=element, values=values)

    def call(self, instance, method, timeout=1.0, **args):
        return tuple(self._rpc("call", timeout=timeout + 30, instance=instance, method=method, call_timeout=timeout,
                               args=args))

    def wait_registered(self, timeout=10.0):
        return bool(self._rpc("ready", timeout=timeout + 15, timeout_ms=int(timeout * 1000)))

    @property
    def stats(self):
        st = dict(self.daemon_stats)
        st.pop("links", None)
        st.update(self._rpc("stats"))
        for k in ("rx_rejected", "rx_unknown_instance", "rx_target", "tx_rate_held"):
            st.setdefault(k, 0)
        return st

    def rss_kb(self):
        return self._rpc("rss")

    def stop(self):
        if self.app is not None:
            try:
                self._rpc("stop", timeout=10)
            except Exception:
                pass
            self.app.terminate("stop", wait=5)
        if self.daemon is not None:
            self.daemon.terminate()
        if self.owner is None:
            shutil.rmtree(self.work, ignore_errors=True)


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

    def __init__(self, path, version=2, key=None, link_id=0, sysid=255, compid=190, accept_unsigned=False, verify=None,
                 mod=None):
        self.path, self.version, self.key = path, version, key
        self.mod = mod or mh.load_dialect(version)  # mod: modul pymavlink wygenerowany dla CPU (<dialekt>.py)
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
