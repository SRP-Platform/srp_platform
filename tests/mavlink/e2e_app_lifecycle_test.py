"""AC2 i AC3 D2: cykl zycia polaczenia aplikacja <-> MavlinkDaemon na prawdziwych procesach.

* restart daemona: aplikacja sama laczy sie i rejestruje ponownie w <= 5 s, ruch wraca; bez daemona wywolanie metody
  konczy sie bledem od razu (nie czeka na timeout);
* restart aplikacji: daemon sprzata jej rejestracje (zadnych sierot), nowy proces rejestruje te same instancje bez konfliktu;
* kolizja msg_id miedzy aplikacjami: daemon odrzuca rejestracje drugiej, pierwsza dziala dalej.
"""
import os
import sys
import time

import pytest

from peer_api import ProcPeer, Scenario, inst_cfg, link_cfg, peer_cfg, primary_impl, wait_until


@pytest.fixture
def scn():
    s = Scenario(primary_impl())
    yield s
    s.close()


def _status(peer, counter):
    peer.emit(3, "Status", state=1, temp=0.0, counter=counter, info_offset=0)


def test_daemon_restart_apps_reregister_within_5s_and_traffic_resumes(scn):
    link = scn.link(baud=921600)
    peer = scn.peer(peer_cfg([link_cfg("r1", link.peer_dev)], [inst_cfg(3, ["r1"])]))
    gs = scn.gs(link.gs_dev)
    _status(peer, 1)
    assert wait_until(lambda: gs.of("SRP_EXAMPLE_STATUS"), 3.0)

    peer.kill_daemon()  # SIGKILL daemona: gniazdo sie zrywa
    link.sim.release_exclusive()  # pty trzymane przez symulator nie traci TIOCEXCL samo (UART tak)
    assert wait_until(lambda: peer._rpc("ready", timeout_ms=100) is False, 3.0)  # aplikacja widzi utrate rejestracji
    t0 = time.monotonic()
    res = peer.call(3, "SetMode", timeout=1.0, mode=1)
    assert res[0] == "error" and time.monotonic() - t0 < 0.5, res  # bez daemona: natychmiastowy blad, nie timeout
    _status(peer, 2)  # event bez daemona: no-op (Update nie rzuca), nic nie wychodzi
    assert peer._rpc("stats")["app_tx_dropped"] >= 1  # liczniki aplikacji (daemon martwy)

    t_start = time.monotonic()
    peer.start_daemon()
    assert peer.wait_registered(5.0), "aplikacja nie zarejestrowala sie ponownie w <= 5 s"
    assert time.monotonic() - t_start <= 5.0
    _status(peer, 3)
    assert wait_until(lambda: [m for m in gs.of("SRP_EXAMPLE_STATUS") if m["d"]["counter"] == 3], 3.0)
    gs.send(gs.mod.MAVLink_srp_example_set_mode_req_message(seq=11, instance=3, target_system=1, target_component=1, mode=5))
    assert wait_until(lambda: gs.of("SRP_EXAMPLE_SET_MODE_RESP"), 3.0)  # REQ dochodzi do apki po restarcie daemona
    assert peer.stats["req_executed"] == 1


def test_app_restart_daemon_cleans_registrations_and_new_process_registers_again(scn):
    link = scn.link(baud=921600)
    peer = scn.peer(peer_cfg([link_cfg("r1", link.peer_dev)], [inst_cfg(3, ["r1"])]))
    gs = scn.gs(link.gs_dev)
    assert peer.daemon_stats["instances"] == 2 and peer.daemon_stats["sessions"] == 1  # skeleton + proxy instancji 3
    peer.kill_app()  # SIGKILL: bez wyrejestrowania - daemon widzi tylko zamkniecie gniazda
    assert wait_until(lambda: peer.daemon_stats["instances"] == 0 and peer.daemon_stats["sessions"] == 0, 3.0)
    peer.start_app()  # te same instancje: brak konfliktu z posprzatana rejestracja
    assert peer.daemon_stats["instances"] == 2 and peer.daemon_stats["register_rejected"] == 0
    _status(peer, 1)
    assert wait_until(lambda: gs.of("SRP_EXAMPLE_STATUS"), 3.0)


def test_msg_id_collision_between_apps_is_rejected_by_daemon(scn):
    link = scn.link(baud=921600)
    first = scn.peer(peer_cfg([link_cfg("r1", link.peer_dev)], [inst_cfg(3, ["r1"])]))
    gs = scn.gs(link.gs_dev)
    other = ProcPeer([os.environ["MAVLINK_PEER_COLLIDE"]], peer_cfg([link_cfg("r1", link.peer_dev)], [inst_cfg(9, ["r1"])]),
                     daemon_from=first)
    scn._closers.append(other.stop)
    other.start(wait_ready=False)  # usluga Clash ma msg_id 42000 jak Example.Status, ale inna definicje
    assert wait_until(lambda: first.daemon_stats["register_rejected"] >= 1, 3.0), first.daemon_stats
    assert not other.stats["registered"]
    # pierwsza aplikacja dziala dalej, a jej msg_id nie zostal przejety
    _status(first, 1)
    assert wait_until(lambda: gs.of("SRP_EXAMPLE_STATUS"), 3.0)
    assert first.stats["registered"]


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v", "-p", "no:cacheprovider"]))
