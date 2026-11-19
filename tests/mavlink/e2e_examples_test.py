"""End-to-end tests on the example applications.

One CPU is simulated: a MavlinkDaemon (`daemon_host`, the production core without Execution
Management), `example_app` (provides EngineService instances 2 and 3) and `example_app2` (requires
instance 2), all on one daemon and two radio links (radio_a, radio_b = ptys with a channel
simulator). At the far end of each link a pymavlink ground station (system 2) uses the dialect the
generator produced for the CPU (//test_cpu/cpu/ec:dialect).

Instance 2 uses both links (redundancy), instance 3 only radio_a, without SetMode/DebugDump and
with Telemetry limited to 1 Hz.
"""
import json
import os
import random
import re
import signal
import subprocess
import sys
import threading
import time

import pytest

import daemon_client as dc
import harness as hn
import mav_helpers as mh

APP1 = os.environ["EXAMPLE_APP1"]
APP2 = os.environ["EXAMPLE_APP2"]
DAEMON_HOST = os.environ["MAVLINK_DAEMON_HOST"]
DAEMON_CFG = os.environ["EXAMPLE_DAEMON_CONFIG"]
GS_SYSTEM = 2  # the remote system named by target/source ids in the example models
K_PEER_UNREACHABLE = 5  # ara::com::ComErrc::kPeerIsUnreachable (method timeout)
LINKS = ("radio_a", "radio_b")


class Stack:
    """The simulated CPU and its ground stations; `Scenario` closes everything."""

    def __init__(self, tmp, scn, loss_pct=0.0, key=None):
        self.tmp, self.scn, self.key = tmp, scn, key
        self.mod = hn.load_dialect_module()
        self.link = {n: scn.add(hn.Link(baud=921600, loss_pct=loss_pct, seed=i + 1))
                     for i, n in enumerate(LINKS)}
        self.gs = {n: scn.add(hn.GS(self.link[n].gs_dev, self.mod, key=key, link_id=7,
                                    sysid=GS_SYSTEM, compid=1)) for n in LINKS}
        self.socket = str(tmp / "daemon.sock")
        self.cfg_path = self._write_config()
        self.runs = 0
        self.daemon = self.app1 = self.app2 = None
        self.start_daemon()
        self.start_apps()
        scn.add(self, "shutdown")

    def _write_config(self):
        cfg = json.load(open(DAEMON_CFG, encoding="utf-8"))
        cfg["socket_path"] = self.socket
        for n in LINKS:
            drv = str(self.tmp / f"{n}.json")
            json.dump({"device": self.link[n].daemon_dev, "baudrate": 921600}, open(drv, "w"))
            cfg["links"][n]["driver_config"] = drv
            if self.key:
                key_file = str(self.tmp / f"{n}.key")
                with open(key_file, "w") as f:
                    f.write(self.key.hex())
                os.chmod(key_file, 0o600)
                cfg["links"][n]["signing"] = {
                    "enabled": True, "key_file": key_file, "link_id": 1 + LINKS.index(n),
                    "accept_unsigned": False, "state_file": str(self.tmp / f"{n}.state")}
        path = str(self.tmp / "mavlink_daemon_config.json")
        json.dump(cfg, open(path, "w"))
        return path

    def start_daemon(self):
        self.daemon = hn.JsonProc([DAEMON_HOST, self.cfg_path], err_path=str(self.tmp / "daemon.err"))
        self.daemon.wait_event("ready")

    def _start_app(self, binary, name):
        out = open(self.tmp / f"{name}.{self.runs}.out", "wb")
        env = dict(os.environ, SRP_MAVLINK_DAEMON_SOCKET=self.socket)
        proc = subprocess.Popen([binary], stdout=out, stderr=subprocess.STDOUT, env=env,
                                start_new_session=True)
        return proc

    def start_apps(self):
        self.runs += 1
        self.app1 = self._start_app(APP1, "app1")
        self.app2 = self._start_app(APP2, "app2")

    def stop_apps(self):
        for p in (self.app1, self.app2):
            if p is not None and p.poll() is None:
                os.killpg(p.pid, signal.SIGKILL)
                p.wait()

    def kill_daemon(self):
        """SIGKILL: the daemon saves nothing; the apps must reconnect and register by themselves."""
        self.daemon.kill()
        for sim in self.link.values():
            sim.sim.release_exclusive()

    def restart_all(self):
        """Graceful stop of the daemon (saves the signing state) and a restart of every process."""
        self.stop_apps()
        self.daemon.terminate()
        for sim in self.link.values():
            sim.sim.release_exclusive()
        self.start_daemon()
        self.start_apps()

    def shutdown(self):
        self.stop_apps()
        self.daemon.terminate()

    def out(self, app):
        """The standard output of the current run of app1 or app2."""
        with open(self.tmp / f"{app}.{self.runs}.out", "rb") as f:
            return f.read().decode("utf-8", "replace")

    def lines(self, app, prefix):
        return [l for l in self.out(app).splitlines() if l.startswith(prefix)]

    def stats(self):
        return self.daemon.rpc("stats")

    def wait_registered(self, timeout=15.0, fresh=False):
        """Waits until both applications are bound: the GS sees events of instances 2 and 3 and the
        calls of app2 succeed. `fresh` counts only what arrives after this call."""
        base = {n: len(g.messages) if fresh else 0 for n, g in self.gs.items()}
        calls = len(self.lines("app2", "CALL SetMode ok")) if fresh else 0

        def instances(link):
            return {m["d"]["instance"] for m in self.gs[link].messages[base[link]:]
                    if m["type"] == "SRP_ENGINE_SERVICE_CURRENT_MODE"}

        ok = hn.wait_until(lambda: {2, 3} <= instances("radio_a") and 2 in instances("radio_b")
                           and len(self.lines("app2", "CALL SetMode ok")) > calls, timeout, step=0.1)
        if not ok:
            pytest.fail(f"not registered: {instances('radio_a')} {instances('radio_b')}\n--- app1\n"
                        f"{self.out('app1')[-1500:]}\n--- app2\n{self.out('app2')[-1500:]}\n--- daemon\n"
                        f"{self.daemon.stderr_tail(1500)}")

    def modes(self):
        """The modes app2 received as CurrentMode events, in arrival order."""
        return [int(m.group(1)) for l in self.lines("app2", "EVT CurrentMode")
                if (m := re.match(r"EVT CurrentMode mode=(\d+)", l))]

    def send_event(self, n):
        """An event for app2's proxy (instance 2), as the same frame on both links."""
        msg = self.mod.MAVLink_srp_engine_service_current_mode_message(seq=n, instance=2, mode=n)
        data = self.gs["radio_a"].send(msg)
        self.gs["radio_b"].send_raw(data)
        return data


class Responder(threading.Thread):
    """Answers app2's SetMode requests on instance 2; `answer = False` keeps the GS silent."""

    def __init__(self, stack):
        super().__init__(daemon=True)
        self.stack, self.answer, self.stop_ev, self.done = stack, True, threading.Event(), set()
        self.start()

    def run(self):
        while not self.stop_ev.is_set():
            for g in self.stack.gs.values():
                for m in list(g.of("SRP_ENGINE_SERVICE_SET_MODE_REQ")):
                    key = (m["d"]["seq"], m["d"]["instance"])
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


@pytest.fixture
def scn():
    s = hn.Scenario()
    yield s
    s.close()


def new_stack(tmp_path, scn, **kw):
    """A running stack with a GS responder, after both applications have registered."""
    st = Stack(tmp_path, scn, **kw)
    st.responder = scn.add(Responder(st))
    st.wait_registered()
    return st


@pytest.fixture
def stack(tmp_path, scn):
    return new_stack(tmp_path, scn)


def gs_req(stack, link, name, instance, seq, **kw):
    cls = getattr(stack.mod, f"MAVLink_srp_engine_service_{name}_req_message")
    return stack.gs[link].send(cls(seq=seq, instance=instance, target_system=1, target_component=1, **kw))


def test_both_apps_register_in_one_daemon(stack):
    # the fixture waited until the GS saw instances 2 and 3 of app1 and app2's proxy answered
    assert stack.stats()["register_rejected"] == 0


def test_daemon_output_is_valid_mavlink_2_with_heartbeats(stack):
    assert hn.wait_until(lambda: all(g.of("HEARTBEAT") for g in stack.gs.values()), 5.0)
    for g in stack.gs.values():
        assert g.bad == 0
        assert all(m["raw"][0] == 0xFD for m in g.messages)
        hb = g.of("HEARTBEAT")[0]
        assert (hb["sysid"], hb["compid"]) == (1, 1)


def test_events_reach_the_gs_on_the_links_of_their_instance(stack):
    gs = stack.gs

    def instances(link):
        return {m["d"]["instance"] for m in gs[link].of("SRP_ENGINE_SERVICE_CURRENT_MODE")}

    assert hn.wait_until(lambda: instances("radio_a") == {2, 3} and instances("radio_b") == {2}, 6.0), (
        instances("radio_a"), instances("radio_b"))


def test_instance_rate_limit_is_below_the_service_limit(stack):
    gs = stack.gs["radio_a"]

    def count(instance):
        return sum(1 for m in gs.of("SRP_ENGINE_SERVICE_TELEMETRY") if m["d"]["instance"] == instance)

    time.sleep(1.0)
    n2, n3 = count(2), count(3)
    time.sleep(8.0)
    n2, n3 = count(2) - n2, count(3) - n3
    assert 4 <= n3 <= 12, n3  # 1 Hz for 8 s
    assert n2 >= 1.5 * n3, (n2, n3)  # the service limit is 2 Hz


def test_gs_calls_methods_excluded_ones_answer_status_2(stack):
    gs = stack.gs
    data = gs_req(stack, "radio_a", "set_mode", 2, 101, mode=7)
    gs["radio_b"].send_raw(data)  # the same request on both links is executed once
    gs_req(stack, "radio_a", "debug_dump", 2, 102)
    gs_req(stack, "radio_a", "set_mode", 3, 103, mode=8)  # excluded in instance 3
    gs_req(stack, "radio_a", "debug_dump", 3, 104)  # excluded in instance 3
    assert hn.wait_until(lambda: any(m["d"]["seq"] == 103 for m in gs["radio_a"].of(
        "SRP_ENGINE_SERVICE_SET_MODE_RESP")), 5.0)
    ok = [m for m in gs["radio_a"].of("SRP_ENGINE_SERVICE_SET_MODE_RESP") if m["d"]["seq"] == 101]
    assert ok and ok[0]["d"]["status"] == 0 and ok[0]["d"]["mode"] == 7
    refused = [m for m in gs["radio_a"].of("SRP_ENGINE_SERVICE_SET_MODE_RESP") if m["d"]["seq"] == 103][0]
    assert refused["d"]["status"] == 2 and refused["d"]["target_system"] == GS_SYSTEM
    assert hn.wait_until(lambda: stack.lines("app1", "REQ DebugDump instance=2"), 3.0)
    time.sleep(1.0)
    assert len(stack.lines("app1", "REQ SetMode instance=2 mode=7")) == 1
    assert not stack.lines("app1", "REQ SetMode instance=3") and not stack.lines("app1", "REQ DebugDump instance=3")


def test_require_app_gets_each_event_once_despite_two_links(stack):
    assert hn.wait_until(lambda: stack.lines("app2", "CALL SetMode ok"), 10.0)  # the proxy is bound
    for n in range(1, 41):
        stack.send_event(n)
        time.sleep(0.02)
    assert hn.wait_until(lambda: len(stack.modes()) >= 40, 5.0), stack.modes()
    assert stack.modes() == list(range(1, 41))
    assert stack.stats()["rx_duplicate"] >= 40


def test_proxy_call_is_answered_times_out_and_recovers(stack):
    responder = stack.responder
    assert hn.wait_until(lambda: stack.lines("app2", "CALL SetMode ok"), 10.0)
    responder.answer = False
    seen = len(stack.lines("app2", "CALL SetMode"))
    assert hn.wait_until(lambda: any(f"err={K_PEER_UNREACHABLE}" in l for l in
                                     stack.lines("app2", "CALL SetMode")[seen:]), 6.0)
    responder.answer = True
    seen = len(stack.lines("app2", "CALL SetMode"))
    assert hn.wait_until(lambda: any(" ok " in l for l in stack.lines("app2", "CALL SetMode")[seen:]), 8.0)


def test_apps_register_again_within_5_s_after_the_daemon_is_killed(stack):
    stack.kill_daemon()
    stack.start_daemon()
    stack.wait_registered(timeout=5.0, fresh=True)
    stack.send_event(200)
    assert hn.wait_until(lambda: 200 in stack.modes(), 5.0)


@pytest.mark.parametrize("loss_pct, delivered", [(10, 94), (30, 80)])
def test_lossy_links_deliver_redundant_events_without_duplicates(tmp_path, scn, loss_pct, delivered):
    st = new_stack(tmp_path, scn, loss_pct=loss_pct)
    assert hn.wait_until(lambda: st.lines("app2", "CALL SetMode ok"), 20.0)
    for n in range(1, 101):
        st.send_event(n)
        time.sleep(0.02)
    hn.wait_until(lambda: len(st.modes()) >= 100, 4.0)
    modes = st.modes()
    assert len(modes) >= delivered, len(modes)  # a frame is lost only when both links drop it
    assert modes == sorted(set(modes)) and set(modes) <= set(range(1, 101))


def tx_timestamps(gs, start=0):
    """Signing timestamps (10 us units) of the daemon's signed frames received after index `start`."""
    return [int.from_bytes(m["raw"][-12:-6], "little") for m in gs.messages[start:] if m["sysid"] == 1]


def test_signed_links_sign_refuse_unsigned_and_keep_timestamps_rising_across_a_restart(tmp_path, scn):
    st = new_stack(tmp_path, scn, key=mh.make_key())
    assert hn.wait_until(lambda: st.lines("app2", "CALL SetMode ok"), 15.0)
    st.send_event(1)
    assert hn.wait_until(lambda: 1 in st.modes(), 5.0)
    assert hn.wait_until(lambda: tx_timestamps(st.gs["radio_a"]), 5.0)
    assert all(m["raw"][2] & 1 for g in st.gs.values() for m in g.messages)  # every frame is signed
    assert not any(g.rejected for g in st.gs.values())  # and the GS verifier accepted them all
    # an unsigned frame on a signed link is refused
    before = st.stats()["links"]["radio_a"]["rx_sig_err"]
    unsigned_mav, sink = mh.make_mav(st.mod, GS_SYSTEM, 1)
    unsigned_mav.send(st.mod.MAVLink_srp_engine_service_current_mode_message(seq=50, instance=2, mode=50))
    st.gs["radio_a"].send_raw(bytes(sink.buf))
    assert hn.wait_until(lambda: st.stats()["links"]["radio_a"]["rx_sig_err"] > before, 3.0)
    assert 50 not in st.modes()
    # the daemon's timestamps keep rising across a restart, even though the state is the only memory
    last_before = max(max(tx_timestamps(g) or [0]) for g in st.gs.values())
    mark = {n: len(g.messages) for n, g in st.gs.items()}
    st.restart_all()
    st.wait_registered(fresh=True)
    for n in LINKS:
        state = (tmp_path / f"{n}.state").read_text().split()
        assert int(state[0]) >= last_before, (n, state, last_before)  # the saved TX timestamp
    assert hn.wait_until(lambda: all(tx_timestamps(g, mark[n]) for n, g in st.gs.items()), 8.0)
    first_after = min(min(tx_timestamps(g, mark[n])) for n, g in st.gs.items())
    assert first_after > last_before + 5_500_000, (first_after, last_before)  # persisted value + 60 s
    assert not any(g.rejected for g in st.gs.values())
    # the ground station restarts too: a new signer with a fresh timestamp is accepted again
    st.gs["radio_a"].stop()
    st.gs["radio_a"] = st.scn.add(hn.GS(st.link["radio_a"].gs_dev, st.mod, key=st.key, link_id=7,
                                        sysid=GS_SYSTEM, compid=1))
    assert hn.wait_until(lambda: st.lines("app2", "CALL SetMode ok"), 10.0)
    st.gs["radio_a"].send(st.mod.MAVLink_srp_engine_service_current_mode_message(seq=60, instance=2, mode=60))
    assert hn.wait_until(lambda: 60 in st.modes(), 5.0)


def test_conflicting_registrations_are_refused_with_a_reason(stack):
    client = dc.Client(stack.socket)
    try:
        ev = dc.Desc(42100, 0, 4, "SRP_OTHER_EVENT", seq=0, instance=2)  # msg_id of EngineService.CurrentMode
        elements = [dc.Element(32769, ev, is_event=True)]
        clash = client.register(1, dc.ROLE_PROVIDE, "srp.example.Other", 9, ["radio_a"], elements)
        assert clash.status == dc.CONFLICT_MSG_ID and "EngineService" in clash.text, clash
        twice = client.register(2, dc.ROLE_PROVIDE, "srp.example.EngineService", 2, ["radio_a"], elements)
        assert twice.status == dc.CONFLICT_INSTANCE, twice
        nowhere = client.register(3, dc.ROLE_PROVIDE, "srp.example.EngineService", 9, ["nowhere"], elements)
        assert nowhere.status == dc.UNKNOWN_LINK and "nowhere" in nowhere.text
    finally:
        client.close()


def test_random_garbage_for_10_s_does_not_stop_the_daemon_or_the_apps(stack):
    seed = int(time.time())
    rng = random.Random(seed)
    assert hn.wait_until(lambda: stack.lines("app2", "CALL SetMode ok"), 10.0)
    gs = stack.gs["radio_a"]
    valid = stack.send_event(0)
    end = time.monotonic() + 10.0
    while time.monotonic() < end:
        kind = rng.randrange(3)
        if kind == 0:
            junk = bytes(rng.randrange(256) for _ in range(rng.randrange(1, 300)))
        elif kind == 1:  # a valid frame with one flipped bit
            b = bytearray(valid)
            bit = rng.randrange(len(b) * 8)
            b[bit // 8] ^= 1 << (bit % 8)
            junk = bytes(b)
        else:  # a truncated frame followed by garbage
            junk = valid[:rng.randrange(1, len(valid))] + bytes(rng.randrange(256) for _ in range(20))
        gs.send_raw(junk)
        time.sleep(0.005)
    assert stack.daemon.proc.poll() is None and stack.app1.poll() is None and stack.app2.poll() is None, seed
    stack.send_event(250)
    assert hn.wait_until(lambda: 250 in stack.modes(), 5.0), seed
    assert set(stack.modes()) <= {0, 250}, (seed, stack.modes())  # only frames that were sent intact


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v", "-p", "no:cacheprovider"]))
