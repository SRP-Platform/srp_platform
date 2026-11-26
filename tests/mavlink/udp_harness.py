"""Harness of the UDP end-to-end tests: a daemon with the statically linked `example_udp_radio`.

`UdpGS` is a ground station on a UDP socket (one datagram = one MAVLink frame, decoded only by the
pymavlink module that the generator produced for the CPU dialect), `UdpStack` starts the daemon
(`example_udp_daemon_host`) with one UDP link per link of the CPU config and the two example
applications, `free_udp_ports` picks the ports.
"""
# pylint: disable=missing-function-docstring,redefined-outer-name,import-error,too-many-locals
# pylint: disable=too-many-arguments,too-many-branches,too-many-statements,consider-using-with
import json
import os
import signal
import socket
import subprocess
import threading
import time

import harness as hn
import mav_helpers as mh

LINKS = ("radio_a", "radio_b")
GS_SYSTEM = 2  # the remote system named by target/source ids in the example models


def free_udp_ports(n):
    """n distinct free UDP ports (bound and released; good enough for a test)."""
    socks = [socket.socket(socket.AF_INET, socket.SOCK_DGRAM) for _ in range(n)]
    try:
        for s in socks:
            s.bind(("127.0.0.1", 0))
        return [s.getsockname()[1] for s in socks]
    finally:
        for s in socks:
            s.close()


class UdpGS:
    """Ground station at the far end of a UDP link.

    Every received datagram is kept in `datagrams` (arrival time, bytes). A datagram is decoded
    with the dialect module `mod` only; `messages` holds the frames pymavlink accepted (and the
    signature verifier, when a key is given), `rejected` those the verifier refused and `bad` the
    datagrams that did not decode to exactly one valid frame.
    """

    def __init__(self, gs_port, driver_port, mod, key=None, link_id=0, sysid=GS_SYSTEM, compid=1):
        self.mod, self.key, self.driver_port = mod, key, driver_port
        self.mav, self.sink = mh.make_mav(mod, sysid, compid, key, link_id)
        self.parser, _ = mh.make_mav(mod, sysid, compid, None)
        self.verifier = mh.StrictVerifier(key, mh.signing_timestamp_now()) if key else None
        self.datagrams, self.messages, self.rejected, self.bad = [], [], [], []
        self.lock = threading.Lock()
        self.tap = None  # a harness.Tap: a copy of both directions for the ground monitor
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("127.0.0.1", gs_port))
        self.sock.settimeout(0.05)
        self._stop = threading.Event()
        self.thread = threading.Thread(target=self._rx, daemon=True)
        self.thread.start()

    def _rx(self):
        while not self._stop.is_set():
            try:
                data, _ = self.sock.recvfrom(4096)
            except OSError:
                continue
            now = time.monotonic()
            if self.tap:
                self.tap.feed("rx", data)
            frames = self.parser.parse_buffer(data) or []
            self.datagrams.append((now, data))
            if len(frames) != 1 or frames[0].get_type() == "BAD_DATA":
                self.bad.append((now, data))
                continue
            m = frames[0]
            verdict = self.verifier.check(data) if self.verifier else "ok"
            rec = dict(type=m.get_type(), d=m.to_dict(), raw=data, t=now, verdict=verdict,
                       sysid=m.get_srcSystem(), compid=m.get_srcComponent(), hseq=data[4])
            (self.messages if verdict == "ok" else self.rejected).append(rec)

    def send(self, msg):
        """Encodes (signed when the GS has a key) and sends one frame to the driver's port."""
        with self.lock:
            self.sink.buf.clear()
            if self.key:
                self.mav.signing.timestamp = max(mh.signing_timestamp_now(), self.mav.signing.timestamp)
            self.mav.send(msg)
            data = bytes(self.sink.buf)
        if self.tap:
            self.tap.feed("tx", data)
        self.sock.sendto(data, ("127.0.0.1", self.driver_port))
        return data

    def of(self, type_name):
        return [m for m in list(self.messages) if m["type"] == type_name]

    def stop(self):
        self._stop.set()
        self.thread.join(timeout=1)
        self.sock.close()


class Responder(threading.Thread):
    """Answers the SetMode requests of the proxy application (instance 2); `answer = False` = silent."""

    def __init__(self, stack):
        super().__init__(daemon=True)
        self.stack, self.answer, self.stop_ev, self.done = stack, True, threading.Event(), set()
        self.start()

    def run(self):
        while not self.stop_ev.is_set():
            for link, g in self.stack.gs.items():
                for m in g.of("SRP_ENGINE_SERVICE_SET_MODE_REQ"):
                    key = (link, m["d"]["seq"], m["d"]["instance"])
                    if key in self.done or m["d"]["instance"] != 2:
                        continue
                    self.done.add(key)
                    if self.answer:
                        g.send(self.stack.mod.MAVLink_srp_engine_service_set_mode_resp_message(
                            seq=m["d"]["seq"], instance=2, target_system=m["sysid"],
                            target_component=m["compid"], status=0, mode=m["d"]["mode"]))
            time.sleep(0.02)

    def stop(self):
        self.stop_ev.set()


class UdpStack:
    """The simulated CPU: daemon + `example_app` + `example_app2` on UDP links, one GS per link."""

    def __init__(self, tmp, scn, daemon_host, daemon_cfg, app1, app2, key=None, answer=True):
        self.tmp, self.key = tmp, key
        self.mod = hn.load_dialect_module()
        ports = free_udp_ports(2 * len(LINKS))
        self.ports = {n: (ports[2 * i], ports[2 * i + 1]) for i, n in enumerate(LINKS)}  # (gs, driver)
        self.gs = {n: scn.add(UdpGS(self.ports[n][0], self.ports[n][1], self.mod, key=key, link_id=7))
                   for n in LINKS}
        self.socket = str(tmp / "daemon.sock")
        self.cfg = self._config(daemon_cfg)
        self.daemon = hn.JsonProc([daemon_host, str(tmp / "mavlink_daemon_config.json")],
                                  err_path=str(tmp / "daemon.err"))
        self.apps = {}
        scn.add(self, "shutdown")
        self.daemon.wait_event("ready")
        self.apps["app1"] = self._start_app(app1, "app1")
        self.apps["app2"] = self._start_app(app2, "app2")
        self.responder = scn.add(Responder(self))
        self.responder.answer = answer

    def _config(self, daemon_cfg):
        cfg = json.load(open(daemon_cfg, encoding="utf-8"))
        cfg["socket_path"] = self.socket
        for i, n in enumerate(LINKS):
            drv = str(self.tmp / f"{n}.json")
            json.dump({"bind_port": self.ports[n][1], "peer_host": "127.0.0.1",
                       "peer_port": self.ports[n][0]}, open(drv, "w"))
            cfg["links"][n]["driver"] = "example_udp_radio"
            cfg["links"][n]["driver_config"] = drv
            if self.key:
                key_file = str(self.tmp / f"{n}.key")
                with open(key_file, "w") as f:
                    f.write(self.key.hex())
                os.chmod(key_file, 0o600)
                cfg["links"][n]["signing"] = {
                    "enabled": True, "key_file": key_file, "link_id": 1 + i,
                    "accept_unsigned": False, "state_file": str(self.tmp / f"{n}.state")}
        json.dump(cfg, open(self.tmp / "mavlink_daemon_config.json", "w"))
        return cfg

    def _start_app(self, binary, name):
        out = open(self.tmp / f"{name}.out", "wb")
        env = dict(os.environ, SRP_MAVLINK_DAEMON_SOCKET=self.socket)
        return subprocess.Popen([binary], stdout=out, stderr=subprocess.STDOUT, env=env,
                                start_new_session=True)

    def out(self, app):
        with open(self.tmp / f"{app}.out", "rb") as f:
            return f.read().decode("utf-8", "replace")

    def lines(self, app, prefix):
        return [l for l in self.out(app).splitlines() if l.startswith(prefix)]

    def instances(self, link, type_name="SRP_ENGINE_SERVICE_CURRENT_MODE"):
        return {m["d"]["instance"] for m in self.gs[link].of(type_name)}

    def wait_registered(self, timeout=15.0):
        """Until both applications are bound: events of instances 2 and 3 on radio_a, 2 on radio_b,
        and the calls of app2 reach the GS."""
        ok = hn.wait_until(lambda: {2, 3} <= self.instances("radio_a") and 2 in self.instances("radio_b")
                           and self.gs["radio_a"].of("SRP_ENGINE_SERVICE_SET_MODE_REQ"), timeout, step=0.05)
        if not ok:
            raise AssertionError(f"not registered: {self.instances('radio_a')} {self.instances('radio_b')}\n"
                                 f"--- app1\n{self.out('app1')[-1500:]}\n--- app2\n{self.out('app2')[-1500:]}\n"
                                 f"--- daemon\n{self.daemon.stderr_tail(1500)}")

    def shutdown(self):
        for p in self.apps.values():
            if p.poll() is None:
                os.killpg(p.pid, signal.SIGKILL)
                p.wait()
        self.daemon.terminate()
