"""AC1 D2: E2E przykladow - example_app (provide) i example_app2 (require) na JEDNYM MavlinkDaemonie i dwoch laczach
(radio_a, radio_b = pty), na drugim koncu pymavlink (GS, system 2) z dialektem wygenerowanym dla CPU (test_cpu/cpu/ec:dialect).

Sprawdzamy: eventy obu aplikacji (provide idzie na radio, require dostaje eventy z radia), metoda z timeoutem, `exclude`
(wykluczona metoda = RESP status=2 / blad bez ruchu), `max_rate_hz` (1 Hz vs 20 Hz), redundancja 2 lacza -> 1 dostarczenie,
oraz tshark + wygenerowane Lua dekoduje ramki OBU aplikacji.
"""
import importlib.util
import json
import os
import re
import signal
import subprocess
import sys
import time
from collections import Counter, defaultdict

import pytest

import mav_helpers as mh
import peer_api as pa
import tshark_harness as th
from channel_sim import ChannelSim

APP1 = os.environ["EXAMPLE_APP1"]
APP2 = os.environ["EXAMPLE_APP2"]
DAEMON_HOST = os.environ["MAVLINK_DAEMON_HOST"]
DAEMON_CFG = os.environ["EXAMPLE_DAEMON_CONFIG"]
RUN_SECONDS = float(os.environ.get("EXAMPLE_RUN_SECONDS", "12"))
GS_SYSTEM = 2  # zdalny system z modeli przykladow (target_system_id/source_system_id)
K_SERVICE_NOT_AVAILABLE = 1  # ara::com::ComErrc::kServiceNotAvailable
K_PEER_UNREACHABLE = 5  # ara::com::ComErrc::kPeerIsUnreachable (timeout metody)


def dialect(ext):
    for path in os.environ["EXAMPLE_DIALECT"].split():
        if path.endswith(ext):
            return path
    raise AssertionError(f"brak {ext} w wyjsciu reguly dialect")


def load_dialect_module():
    spec = importlib.util.spec_from_file_location("srp_example", dialect(".py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def start_app(binary, sock, out_path):
    env = dict(os.environ, SRP_EXAMPLE_NO_EM="1", SRP_MAVLINK_DAEMON_SOCKET=sock)
    out = open(out_path, "wb")
    return subprocess.Popen([binary], stdout=out, stderr=subprocess.STDOUT, env=env, start_new_session=True), out


def stop_app(proc):
    """SIGKILL: okno pomiaru konczy sie dokladnie z czasem testu."""
    if proc.poll() is None:
        os.killpg(proc.pid, signal.SIGKILL)
        proc.wait()


class ExampleGS:
    """Dwa konce lacz (radio_a, radio_b): pymavlink jako system 2 z dialektem CPU."""

    def __init__(self, mod, path_a, path_b):
        self.mod = mod
        self.gs = {name: pa.GS(path, version=2, sysid=GS_SYSTEM, compid=1, mod=mod)
                   for name, path in (("radio_a", path_a), ("radio_b", path_b))}

    def of(self, link, name):
        return self.gs[link].of(name)

    def stop(self):
        for g in self.gs.values():
            g.stop()


@pytest.fixture(scope="module")
def run(tmp_path_factory):
    d = tmp_path_factory.mktemp("ex")
    mod = load_dialect_module()
    sim_a, sim_b = ChannelSim(baud=921600, seed=1).start(), ChannelSim(baud=921600, seed=2).start()
    cfg = json.load(open(DAEMON_CFG, encoding="utf-8"))
    sock = str(d / "daemon.sock")
    cfg["socket_path"] = sock
    for name, sim in (("radio_a", sim_a), ("radio_b", sim_b)):
        drv = str(d / f"{name}.json")
        json.dump({"device": sim.path_a, "baudrate": 921600}, open(drv, "w"))
        cfg["links"][name]["driver_config"] = drv
    cfg_path = str(d / "mavlink_daemon_config.json")
    json.dump(cfg, open(cfg_path, "w"))
    daemon = pa.JsonProc([DAEMON_HOST, cfg_path])
    daemon.wait_event("ready")
    gs = ExampleGS(mod, sim_a.path_b, sim_b.path_b)
    app1, f1 = start_app(APP1, sock, str(d / "app1.out"))
    app2, f2 = start_app(APP2, sock, str(d / "app2.out"))
    handled = set()
    t0 = time.monotonic()
    n_events = 0
    while time.monotonic() - t0 < RUN_SECONDS:
        # GS jako zdalny silnik: eventy dla app2 (instancja 2) na OBU laczach - ta sama ramka (redundancja)
        if time.monotonic() - t0 > 3.0 and n_events < 40:
            n_events += 1
            msg = mod.MAVLink_srp_engine_service_current_mode_message(seq=n_events, instance=2, mode=n_events)
            data = gs.gs["radio_a"].send(msg)
            gs.gs["radio_b"].send_raw(data)
        # GS odpowiada na SetMode z app2: instancja 2 i 4 -> OK (tryb odeslany), instancja 3 -> cisza (timeout)
        for link, g in gs.gs.items():
            for m in g.of("SRP_ENGINE_SERVICE_SET_MODE_REQ"):
                key = (link, m["d"]["seq"], m["d"]["instance"])
                if key in handled or m["d"]["instance"] == 3:
                    continue
                handled.add(key)
                resp = mod.MAVLink_srp_engine_service_set_mode_resp_message(
                    seq=m["d"]["seq"], instance=m["d"]["instance"], target_system=m["sysid"],
                    target_component=m["compid"], status=0, mode=m["d"]["mode"])
                g.send(resp)
        time.sleep(0.05)
    # GS jako zdalna stacja wywoluje metody app1 (provide): SetMode / DebugDump na instancjach 3 (radio_a) i 4 (radio_b)
    def req(link, instance, name, **kw):
        cls = getattr(mod, f"MAVLink_srp_engine_service_{name}_req_message")
        gs.gs[link].send(cls(seq=900 + instance, instance=instance, target_system=1, target_component=1, **kw))
    req("radio_a", 3, "set_mode", mode=7)
    req("radio_b", 4, "set_mode", mode=8)   # SetMode wykluczone w instancji 4
    req("radio_a", 3, "debug_dump")           # DebugDump wykluczone w instancji 3
    req("radio_b", 4, "debug_dump")
    time.sleep(1.5)
    stats = daemon.rpc("stats")  # przed zatrzymaniem aplikacji: po ich zamknieciu daemon sprzata rejestracje
    stop_app(app2)
    stop_app(app1)
    f1.close()
    f2.close()
    out1 = open(d / "app1.out", "rb").read().decode("utf-8", "replace")
    out2 = open(d / "app2.out", "rb").read().decode("utf-8", "replace")
    result = dict(gs=gs, out1=out1, out2=out2, stats=stats, events_sent=n_events, mod=mod, tmp=d)
    yield result
    gs.stop()
    daemon.terminate()
    sim_a.stop()
    sim_b.stop()


def lines(text, prefix):
    return [l for l in text.splitlines() if l.startswith(prefix)]


def app2_events(text, name):
    per = defaultdict(list)
    for l in lines(text, f"EVT {name} "):
        m = re.match(r"EVT \w+ instance=(\d+) (?:mode|rpm)=(\d+)", l)
        if m:
            per[int(m.group(1))].append(int(m.group(2)))
    return per


def test_both_apps_registered_in_one_daemon(run):
    st = run["stats"]
    assert st["sessions"] == 2 and st["register_rejected"] == 0, st
    assert st["instances"] == 6  # app1: provide 2,3,4; app2: require 2,3,4


def test_provide_app_events_reach_gs_on_their_links(run):
    gs = run["gs"]
    inst_a = {m["d"]["instance"] for m in gs.of("radio_a", "SRP_ENGINE_SERVICE_CURRENT_MODE")}
    inst_b = {m["d"]["instance"] for m in gs.of("radio_b", "SRP_ENGINE_SERVICE_CURRENT_MODE")}
    assert inst_a == {2, 3} and inst_b == {2, 4}, (inst_a, inst_b)  # instancja 2 idzie na oba lacza


def test_telemetry_rate_1hz_vs_20hz(run):
    gs = run["gs"]
    n3 = sum(1 for m in gs.of("radio_a", "SRP_ENGINE_SERVICE_TELEMETRY") if m["d"]["instance"] == 3)
    n4 = sum(1 for m in gs.of("radio_b", "SRP_ENGINE_SERVICE_TELEMETRY") if m["d"]["instance"] == 4)
    secs = RUN_SECONDS + 1.5
    assert 0 < n3 <= 1.5 * secs + 2, (n3, secs)          # ~1 Hz (limit instancji 1 < limit uslugi 2)
    assert n4 >= 8 * (secs - 2), (n4, secs)              # ~20 Hz
    assert n4 >= 8 * n3


def test_method_call_from_gs_executes_and_excluded_gets_status_2(run):
    gs, out1 = run["gs"], run["out1"]
    ok = [m for m in gs.of("radio_a", "SRP_ENGINE_SERVICE_SET_MODE_RESP") if m["d"]["instance"] == 3]
    assert ok and ok[0]["d"]["status"] == 0 and ok[0]["d"]["mode"] == 7, ok  # instancja 3: SetMode dozwolone
    excl = [m for m in gs.of("radio_b", "SRP_ENGINE_SERVICE_SET_MODE_RESP") if m["d"]["instance"] == 4]
    assert excl and excl[0]["d"]["status"] == 2, excl  # instancja 4: SetMode wykluczone -> status=2 (niedostepne)
    assert any(l.startswith("REQ SetMode instance=3") for l in out1.splitlines())
    assert not any(l.startswith("REQ SetMode instance=4") for l in out1.splitlines())  # handler skeletonu nie wolany


def test_excluded_debug_dump_is_not_executed(run):
    out1 = run["out1"]
    assert any(l.startswith("REQ DebugDump instance=4") for l in out1.splitlines())
    assert not any(l.startswith("REQ DebugDump instance=3") for l in out1.splitlines())


def test_require_app_gets_each_gs_event_once_despite_two_links(run):
    modes = app2_events(run["out2"], "CurrentMode")[2]
    assert len(modes) >= 30, (len(modes), run["events_sent"])
    dup = [m for m, c in Counter(modes).items() if c > 1]
    assert not dup, f"duplikaty w aplikacji: {dup}"  # kopia z drugiego lacza odrzucona przez daemon
    assert modes == sorted(modes)
    assert run["stats"]["rx_duplicate"] >= len(modes)


def test_require_app_method_answered_by_gs_ok_ignored_one_times_out_excluded_fails_fast(run):
    out2 = run["out2"]
    ok = [l for l in lines(out2, "CALL SetMode instance=2 ") if " ok " in l]
    assert len(ok) >= 3, lines(out2, "CALL SetMode instance=2")  # GS odpowiada
    timeouts = lines(out2, "CALL SetMode instance=3 ")
    assert timeouts and all(f"err={K_PEER_UNREACHABLE}" in l for l in timeouts), timeouts  # GS milczy -> timeout metody
    dd = lines(out2, "CALL DebugDump instance=3 ")
    assert dd and all("err=" in l for l in dd), dd  # DebugDump wykluczone w instancji 3: blad bez nadawania
    assert not any(m["d"].get("instance") == 3 and m["type"] == "SRP_ENGINE_SERVICE_DEBUG_DUMP_REQ"
                   for g in run["gs"].gs.values() for m in g.messages)  # nic nie wyszlo na radio


def test_tshark_with_cpu_lua_decodes_frames_of_both_apps(run, tmp_path):
    reason = th.available()
    if reason:
        pytest.skip(reason)
    gs = run["gs"]
    frames = []
    for g in gs.gs.values():
        for m in g.messages:
            # eventy app1 (provide) oraz REQ app2 (require) - ramki obu aplikacji jednego CPU
            if m["type"] in ("SRP_ENGINE_SERVICE_CURRENT_MODE", "SRP_ENGINE_SERVICE_TELEMETRY",
                             "SRP_ENGINE_SERVICE_SET_MODE_REQ") and m["sysid"] == 1:
                frames.append(m["raw"])
    types = {m["type"] for g in gs.gs.values() for m in g.messages if m["sysid"] == 1}
    assert {"SRP_ENGINE_SERVICE_CURRENT_MODE", "SRP_ENGINE_SERVICE_SET_MODE_REQ"} <= types, types
    pcap = th.write_pcap(str(tmp_path / "cpu.pcap"), frames)
    rows = th.run_tshark(pcap, dialect(".lua"), ["mavlink_proto.msgid", "mavlink_proto.sysid"])
    ids = Counter(int(r[0]) for r in rows if r and r[0])
    assert ids[42100] > 0 and ids[42110] > 0, ids  # CURRENT_MODE (app1) i SET_MODE_REQ (app2)
    assert all(r[1] == "1" for r in rows if r and r[0])  # system_id daemona CPU


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v", "-p", "no:cacheprovider"]))
