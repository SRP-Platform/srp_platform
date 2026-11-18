"""E2E MavlinkDaemona: prawdziwy proces daemona (host bez ara::exec) + driver serial_radio / plugin .so na pty,
po drugiej stronie pymavlink (peer radiowy), a po stronie aplikacji niezalezny klient IPC w Pythonie.

Scenariusze (AC3 briefu D1): event w obu kierunkach (2 aplikacje na jednym laczu), metoda w obu kierunkach,
kBusy (driver testowy wstrzymuje) bez utraty i duplikatu, restart drivera po bledzie fatalnym, rozlaczenie klienta
sprzata rejestracje, konflikt msg_id/instancji odrzucony, podpis.
"""
import json
import os
import pty
import re
import shutil
import struct
import subprocess
import tempfile
import threading
import time
import tty

import pytest

import daemon_client as dc
import mav_helpers

SYS, COMP = 10, 3          # system/component daemona
PEER_SYS, PEER_COMP = 7, 1  # peer radiowy (pymavlink)


# ------------------------------------------------------------------ uklad wiadomosci z pymavlink (wyrocznia)

def layout(cls):
    """{pole: (offset, struct_fmt)} i pelna dlugosc payloadu wg ukladu na przewodzie (malejacy rozmiar elementu)."""
    fmt = cls.native_format
    fmt = fmt.decode() if isinstance(fmt, (bytes, bytearray)) else fmt
    tokens = re.findall(r"(\d*)([a-zA-Z])", fmt.lstrip("<>=!@"))
    out, pos = {}, 0
    for name, (cnt, ch) in zip(cls.ordered_fieldnames, tokens):
        f = (cnt or "") + ch
        out[name] = (pos, f)
        pos += struct.calcsize("<" + f)
    return out, pos


def payload(cls, **fields):
    lay, n = layout(cls)
    buf = bytearray(n)
    for name, val in fields.items():
        off, f = lay[name]
        struct.pack_into("<" + f, buf, off, val)
    return bytes(buf)


def field(cls, data, name):
    lay, _ = layout(cls)
    off, f = lay[name]
    return struct.unpack_from("<" + f, data, off)[0]


def desc(cls, tsys=None, tcomp=None, status=None, name=None):
    lay, n = layout(cls)
    off = lambda k: lay[k][0] if k in lay else dc.NO_OFFSET  # noqa: E731
    return dc.Desc(cls.id, cls.crc_extra, n, name or cls.msgname, seq=off("seq"), instance=off("instance"),
                   tsys=off("target_system") if tsys else dc.NO_OFFSET,
                   tcomp=off("target_component") if tcomp else dc.NO_OFFSET,
                   status=off("status") if status else dc.NO_OFFSET)


@pytest.fixture(scope="module")
def mod():
    return mav_helpers.load_dialect(2)


class Svc:
    """Usluga Example z fikstury mav_helpers: event Status + metoda SetMode."""

    def __init__(self, mod):
        self.mod = mod
        self.Status = mod.MAVLink_srp_example_status_message
        self.Req = mod.MAVLink_srp_example_set_mode_req_message
        self.Resp = mod.MAVLink_srp_example_set_mode_resp_message
        self.EV, self.M = 0x8001, 1

    def elements(self):
        return [
            dc.Element(self.EV, desc(self.Status), is_event=True),
            dc.Element(self.M, desc(self.Req, tsys=True, tcomp=True),
                       resp=desc(self.Resp, tsys=True, tcomp=True, status=True), timeout_ms=3000),
        ]

    def status(self, seq, instance, counter, state=1, temp=1.5, info_offset=-3):
        return payload(self.Status, seq=seq, instance=instance, state=state, temp=temp, counter=counter,
                       info_offset=info_offset)

    def req(self, seq, instance, tsys, tcomp, mode):
        return payload(self.Req, seq=seq, instance=instance, target_system=tsys, target_component=tcomp, mode=mode)

    def resp(self, seq, instance, tsys, tcomp, status, mode):
        return payload(self.Resp, seq=seq, instance=instance, target_system=tsys, target_component=tcomp,
                       status=status, mode=mode)


# ------------------------------------------------------------------ peer radiowy (pymavlink na pty)

class Peer:
    def __init__(self, mod, sysid=PEER_SYS, compid=PEER_COMP, key=None, link_id=0):
        self.mod = mod
        self.master, self.slave = pty.openpty()  # slave trzymamy otwarty: master nie dostaje EIO przed startem daemona
        tty.setraw(self.slave)
        self.path = os.ttyname(self.slave)
        os.set_blocking(self.master, False)
        self.tx, self.sink = mav_helpers.make_mav(mod, sysid, compid, key, link_id)
        self.rx, _ = mav_helpers.make_mav(mod, 255, 0)
        self.rx.robust_parsing = True
        self.raw = bytearray()
        self.msgs = []

    def send(self, msg):
        self.tx.send(msg)
        data = bytes(self.sink.buf)
        self.sink.buf.clear()
        self.send_bytes(data)

    def send_bytes(self, data):
        end = time.monotonic() + 5
        while data and time.monotonic() < end:
            try:
                n = os.write(self.master, data)
                data = data[n:]
            except BlockingIOError:
                time.sleep(0.005)

    def pump(self, seconds=0.2):
        end = time.monotonic() + seconds
        while True:
            try:
                data = os.read(self.master, 65536)
            except BlockingIOError:
                data = b""
            except OSError:
                data = b""
            if data:
                self.raw += data
                for m in self.rx.parse_buffer(data) or []:
                    if m.get_type() != "BAD_DATA":
                        self.msgs.append(m)
            elif time.monotonic() >= end:
                return
            else:
                time.sleep(0.005)

    def wait(self, pred, timeout=3.0):
        end = time.monotonic() + timeout
        while True:
            self.pump(0.05)
            for i, m in enumerate(self.msgs):
                if pred(m):
                    return self.msgs.pop(i)
            if time.monotonic() >= end:
                return None

    def of_type(self, name):
        return [m for m in self.msgs if m.get_type() == name]

    def close(self):
        os.close(self.master)
        os.close(self.slave)


# ------------------------------------------------------------------ srodowisko: daemon + lacza

class Env:
    pass


def _write(path, data):
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f)


@pytest.fixture
def env(mod):
    host = os.environ["DAEMON_HOST"]
    plugin = os.environ["CTL_PLUGIN"]
    e = Env()
    e.dir = tempfile.mkdtemp(prefix="mavd_", dir="/tmp")
    e.svc = Svc(mod)
    e.mod = mod
    e.p1, e.ctl, e.sig = Peer(mod), Peer(mod), Peer(mod, key=None)
    e.key = mav_helpers.make_key()
    key_file = os.path.join(e.dir, "sig.key")
    with open(key_file, "w") as f:
        f.write(e.key.hex() + "\n")
    e.hold_file = os.path.join(e.dir, "hold")
    e.fatal_file = os.path.join(e.dir, "fatal")
    os.makedirs(os.path.join(e.dir, "plugins"))
    shutil.copy(plugin, os.path.join(e.dir, "plugins", "libtest_radio_plugin_ctl.so"))
    _write(os.path.join(e.dir, "p1.json"), {"device": e.p1.path, "baudrate": 115200})
    _write(os.path.join(e.dir, "ctl.json"), {"device": e.ctl.path, "baudrate": 115200, "hold_file": e.hold_file,
                                             "fatal_file": e.fatal_file})
    _write(os.path.join(e.dir, "sig.json"), {"device": e.sig.path, "baudrate": 115200})
    e.sock = os.path.join(e.dir, "d.sock")
    e.restart_ms = 500
    _write(os.path.join(e.dir, "daemon.json"), {
        "system_id": SYS, "component_id": COMP, "socket_path": e.sock, "driver_dir": os.path.join(e.dir, "plugins"),
        "restart_delay_ms": e.restart_ms,
        "links": {
            "p1": {"driver": "serial_radio", "driver_config": os.path.join(e.dir, "p1.json"), "heartbeat_hz": 0},
            "ctl": {"driver": "test_ctl_radio", "driver_config": os.path.join(e.dir, "ctl.json"), "heartbeat_hz": 0},
            "sig": {"driver": "serial_radio", "driver_config": os.path.join(e.dir, "sig.json"), "heartbeat_hz": 0,
                    "signing": {"enabled": True, "key_file": key_file, "link_id": 6}},
        }})
    e.log_path = os.path.join(e.dir, "daemon.log")
    e.log = open(e.log_path, "w")
    # daemon_host: ten sam rdzen co produkcyjny daemon; gotowosc = {"ev":"ready"} na stdout, stop = zamkniecie stdin
    e.proc = subprocess.Popen([host, os.path.join(e.dir, "daemon.json")], stdin=subprocess.PIPE,
                              stdout=subprocess.PIPE, stderr=e.log, text=True)
    e.clients = []
    ready = threading.Event()

    def watch_stdout():
        for line in e.proc.stdout:
            if '"ready"' in line:
                ready.set()

    threading.Thread(target=watch_stdout, daemon=True).start()
    end = time.monotonic() + 15
    while not ready.wait(0.05):
        assert e.proc.poll() is None, open(e.log_path).read()
        assert time.monotonic() < end, "daemon nie wystartowal:\n" + open(e.log_path).read()

    def client():
        c = dc.Client(e.sock)
        e.clients.append(c)
        return c

    e.client = client
    yield e
    for c in e.clients:
        c.close()
    try:
        e.proc.stdin.close()  # EOF = zatrzymanie rdzenia (zapis stanu podpisu) i wyjscie
        e.proc.wait(10)
    except subprocess.TimeoutExpired:
        e.proc.kill()
    e.log.close()
    for p in (e.p1, e.ctl, e.sig):
        p.close()
    log = open(e.log_path).read()
    shutil.rmtree(e.dir, ignore_errors=True)
    assert "ThreadSanitizer" not in log


def register_ok(client, handle, role, svc, instance, links, com_id="srp.Example", **kw):
    r = client.register(handle, role, com_id, instance, links, svc.elements(), **kw)
    assert r is not None and r.status == dc.OK, r
    return r


# ------------------------------------------------------------------ testy

def test_event_in_both_directions_with_two_applications_on_one_link(env):
    s = env.svc
    a, b = env.client(), env.client()  # dwie „aplikacje” na jednym lacze
    ra = register_ok(a, 1, dc.ROLE_PROVIDE, s, 3, ["p1"])
    register_ok(b, 1, dc.ROLE_REQUIRE, s, 4, ["p1"])
    assert (ra.system_id, ra.component_id) == (SYS, COMP)
    assert a.wait_link("p1", True) and b.wait_link("p1", True)

    # aplikacja A -> eter: pymavlink dekoduje event
    a.event(1, s.EV, s.status(seq=11, instance=3, counter=77, temp=2.5))
    m = env.p1.wait(lambda x: x.get_type() == "SRP_EXAMPLE_STATUS")
    assert m is not None, open(env.log_path).read()
    assert (m.instance, m.counter, m.state, m.info_offset) == (3, 77, 1, -3)
    assert m.temp == pytest.approx(2.5)
    assert (m.get_srcSystem(), m.get_srcComponent()) == (SYS, COMP)

    # eter -> aplikacja B (instancja 4): pymavlink koduje event, B dostaje pelny payload
    env.p1.send(s.Status(seq=5, instance=4, state=2, temp=9.25, counter=88, info_offset=-7))
    ev = b.expect(dc.EVENT_IN)
    assert ev is not None and ev.element_id == s.EV and ev.link == "p1"
    assert (ev.sender_system, ev.sender_component) == (PEER_SYS, PEER_COMP)
    assert field(s.Status, ev.payload, "counter") == 88
    assert field(s.Status, ev.payload, "info_offset") == -7
    assert field(s.Status, ev.payload, "state") == 2
    assert a.nothing(dc.EVENT_IN)  # A ma instancje 3 i role provide: nic nie dostaje


def test_method_in_both_directions(env):
    s = env.svc
    a, b = env.client(), env.client()
    register_ok(a, 1, dc.ROLE_PROVIDE, s, 3, ["p1"])
    register_ok(b, 1, dc.ROLE_REQUIRE, s, 4, ["p1"])
    assert b.wait_link("p1", True)

    # peer wywoluje metode u A (provide, instancja 3)
    env.p1.send(s.Req(seq=5, instance=3, target_system=SYS, target_component=COMP, mode=2))
    rq = a.expect(dc.REQUEST_IN)
    assert rq is not None and rq.element_id == s.M
    assert field(s.Req, rq.payload, "seq") == 5 and field(s.Req, rq.payload, "mode") == 2
    a.response(1, s.M, s.resp(5, 3, rq.sender_system, rq.sender_component, 0, 2))
    resp = env.p1.wait(lambda x: x.get_type() == "SRP_EXAMPLE_SET_MODE_RESP")
    assert resp is not None
    assert (resp.seq, resp.instance, resp.status, resp.mode) == (5, 3, 0, 2)
    assert (resp.target_system, resp.target_component) == (PEER_SYS, PEER_COMP)

    # B (require, instancja 4) wywoluje metode u peera
    b.request(1, s.M, s.req(9, 4, PEER_SYS, PEER_COMP, 3))
    req = env.p1.wait(lambda x: x.get_type() == "SRP_EXAMPLE_SET_MODE_REQ")
    assert req is not None and (req.seq, req.instance, req.mode) == (9, 4, 3)
    env.p1.send(s.Resp(seq=9, instance=4, target_system=SYS, target_component=COMP, status=0, mode=3))
    rs = b.expect(dc.RESPONSE_IN)
    assert rs is not None and field(s.Resp, rs.payload, "seq") == 9 and field(s.Resp, rs.payload, "status") == 0

    # nieznana instancja u providera: status 2 lacze, ktorym przyszlo
    env.p1.send(s.Req(seq=6, instance=99, target_system=SYS, target_component=COMP, mode=1))
    r2 = env.p1.wait(lambda x: x.get_type() == "SRP_EXAMPLE_SET_MODE_RESP" and x.seq == 6)
    assert r2 is not None and r2.status == 2


def test_busy_radio_holds_requests_without_loss_or_duplicate(env):
    s = env.svc
    b = env.client()
    register_ok(b, 1, dc.ROLE_REQUIRE, s, 4, ["ctl"])
    assert b.wait_link("ctl", True)
    open(env.hold_file, "w").close()  # driver testowy: Send() -> kBusy
    for seq in range(1, 6):
        b.request(1, s.M, s.req(seq, 4, PEER_SYS, PEER_COMP, seq))
    env.ctl.pump(0.5)
    assert env.ctl.of_type("SRP_EXAMPLE_SET_MODE_REQ") == [], "nic nie moze wyjsc, dopoki driver jest zajety"
    os.unlink(env.hold_file)  # driver zwalnia miejsce -> OnTxReady
    deadline = time.monotonic() + 5
    while len(env.ctl.of_type("SRP_EXAMPLE_SET_MODE_REQ")) < 5 and time.monotonic() < deadline:
        env.ctl.pump(0.1)
    env.ctl.pump(0.5)  # ewentualne duplikaty
    seqs = [m.seq for m in env.ctl.of_type("SRP_EXAMPLE_SET_MODE_REQ")]
    assert seqs == [1, 2, 3, 4, 5], seqs


def test_fatal_driver_error_restarts_the_link_after_the_delay(env):
    s = env.svc
    a = env.client()
    register_ok(a, 1, dc.ROLE_PROVIDE, s, 3, ["ctl"])
    assert a.wait_link("ctl", True)
    t0 = time.monotonic()
    open(env.fatal_file, "w").close()  # driver zglasza OnError(fatal)
    assert a.wait_link("ctl", False, 5)
    assert a.wait_link("ctl", True, 10)
    assert time.monotonic() - t0 >= env.restart_ms / 1000.0 * 0.9
    log = open(env.log_path).read()
    assert "test_ctl_radio: fatal_file" in log
    # po restarcie ruch idzie normalnie
    a.event(1, s.EV, s.status(seq=1, instance=3, counter=5))
    assert env.ctl.wait(lambda x: x.get_type() == "SRP_EXAMPLE_STATUS" and x.counter == 5, 5) is not None


def test_client_disconnect_cleans_up_registrations(env):
    s = env.svc
    a = env.client()
    register_ok(a, 1, dc.ROLE_PROVIDE, s, 3, ["p1"])
    a.close()
    a2 = env.client()
    deadline = time.monotonic() + 5
    ok = False
    while time.monotonic() < deadline and not ok:
        r = a2.register(1, dc.ROLE_PROVIDE, "srp.Example", 3, ["p1"], s.elements())
        ok = r is not None and r.status == dc.OK
        if not ok:
            assert r.status == dc.CONFLICT_INSTANCE  # sprzatanie jest asynchroniczne: wtedy jeszcze konflikt
            time.sleep(0.05)
    assert ok, "rejestracja po rozlaczeniu poprzedniej aplikacji powinna sie udac"


def test_conflicts_of_instance_and_msg_id_are_rejected_with_a_reason(env):
    s = env.svc
    a, c = env.client(), env.client()
    register_ok(a, 1, dc.ROLE_PROVIDE, s, 3, ["p1"])
    dup = c.register(1, dc.ROLE_PROVIDE, "srp.Example", 3, ["p1"], s.elements())
    assert dup.status == dc.CONFLICT_INSTANCE and "srp.Example" in dup.text
    other = c.register(2, dc.ROLE_PROVIDE, "srp.Inna", 1, ["p1"], s.elements())  # te same msg_id, inna usluga
    assert other.status == dc.CONFLICT_MSG_ID and "srp.Example" in other.text, other.text
    ok_role = c.register(3, dc.ROLE_REQUIRE, "srp.Example", 3, ["p1"], s.elements())  # ta sama usluga, inna rola
    assert ok_role.status == dc.OK
    unknown = c.register(4, dc.ROLE_PROVIDE, "srp.Example", 8, ["nie_ma"], s.elements())
    assert unknown.status == dc.UNKNOWN_LINK and "nie_ma" in unknown.text


def test_signed_link_signs_tx_and_checks_rx(env, mod):
    s = env.svc
    a, b = env.client(), env.client()
    register_ok(a, 1, dc.ROLE_PROVIDE, s, 3, ["sig"])
    register_ok(b, 1, dc.ROLE_REQUIRE, s, 4, ["sig"])
    assert b.wait_link("sig", True)
    a.event(1, s.EV, s.status(seq=1, instance=3, counter=1))
    deadline = time.monotonic() + 3
    while len(env.sig.raw) == 0 and time.monotonic() < deadline:
        env.sig.pump(0.1)
    env.sig.pump(0.2)
    frames = mav_helpers.split_v2_frames(bytes(env.sig.raw))
    assert frames, "brak ramki z daemona"
    verifier = mav_helpers.StrictVerifier(env.key, mav_helpers.signing_timestamp_now())
    assert all(f[2] & 1 for f in frames), "ramki z lacza z podpisem musza byc podpisane"
    assert [verifier.check(f) for f in frames] == ["ok"] * len(frames)
    # RX: niepodpisana odrzucona, podpisana przechodzi
    env.sig.send(s.Status(seq=1, instance=4, state=1, temp=0.0, counter=10, info_offset=0))
    assert b.nothing(dc.EVENT_IN, timeout=0.5)
    signed, sink = mav_helpers.make_mav(mod, PEER_SYS, PEER_COMP, env.key, 5)
    signed.send(s.Status(seq=2, instance=4, state=1, temp=0.0, counter=11, info_offset=0))
    env.sig.send_bytes(bytes(sink.buf))
    ev = b.expect(dc.EVENT_IN, timeout=3)
    assert ev is not None and field(s.Status, ev.payload, "counter") == 11


if __name__ == "__main__":
    import sys
    sys.exit(pytest.main([__file__, "-v", "-p", "no:cacheprovider"]))
