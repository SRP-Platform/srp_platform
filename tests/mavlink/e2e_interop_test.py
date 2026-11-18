"""Scenariusze 2, 3, 7, 8: interop peer <-> pymavlink (v1, v2, v2 signed), tshark+Lua na ramkach z peera,
signing (zly klucz, replay, niepodpisane, restart), max_rate_hz / exclude / lacze bez rejestracji.

Peer = implementacja z `peer_api.impls()`: `cpp` (MAVLINK_PEER, prawdziwy runtime). Oczekiwania pisane ze spec (rev. 4/5), nie z kodu runtime.
"""
import os
import sys
import time

import pytest

import mav_helpers as mh
import tshark_harness as th
from peer_api import (GS, Scenario, impls, inst_cfg, link_cfg, make_peer, peer_cfg, record, wait_until)

P = "mavlink_proto."


@pytest.fixture(params=impls())
def scn(request):
    s = Scenario(request.param)
    yield s
    s.close()


def status_msg(mod, **kw):
    d = dict(seq=1, instance=3, state=7, temp=21.5, counter=123456, info_offset=-300)
    d.update(kw)
    return mod.MAVLink_srp_example_status_message(**d)


def req_msg(mod, seq=5, instance=3, mode=9, ts=0, tc=0):
    return mod.MAVLink_srp_example_set_mode_req_message(seq=seq, instance=instance, target_system=ts, target_component=tc,
                                                       mode=mode)


def build(scn, version=2, key=None, link_id=1, accept_unsigned=False, instance_kw=None, gs_key="same", **link_kw):
    link = scn.link(baud=921600)
    cfg = peer_cfg([link_cfg("r1", link.peer_dev, version, key, link_id, accept_unsigned, **link_kw)],
                   [inst_cfg(3, ["r1"], **(instance_kw or {}))])
    peer = scn.peer(cfg)
    gs = scn.gs(link.gs_dev, version=version, key=key if gs_key == "same" else gs_key, link_id=2 if key else 0,
                accept_unsigned=accept_unsigned)
    return link, peer, gs


VARIANTS = [(1, False), (2, False), (2, True)]
IDS = ["v1", "v2", "v2signed"]


# ============================ scenariusz 2: interop =============================================================
@pytest.mark.parametrize("version,signed", VARIANTS, ids=IDS)
def test_peer_event_decoded_by_pymavlink(scn, version, signed):
    key = mh.make_key() if signed else None
    link, peer, gs = build(scn, version, key)
    peer.emit(3, "Status", state=7, temp=21.5, counter=123456, info_offset=-300)
    assert wait_until(lambda: gs.of("SRP_EXAMPLE_STATUS"))
    m = gs.of("SRP_EXAMPLE_STATUS")[0]
    d = m["d"]
    assert (d["instance"], d["state"], d["counter"], d["info_offset"]) == (3, 7, 123456, -300)
    assert d["temp"] == pytest.approx(21.5)
    assert (m["sysid"], m["compid"]) == (1, 1) and m["verdict"] == "ok"
    assert bool(m["raw"][2] & 1) == signed if version == 2 else True  # v2: flaga podpisu wg konfiguracji lacza
    peer.emit(3, "Name", label="hello", value=2.5)
    assert wait_until(lambda: gs.of("SRP_EXAMPLE_NAME"))
    assert gs.of("SRP_EXAMPLE_NAME")[0]["d"]["label"] == "hello"


@pytest.mark.parametrize("version,signed", VARIANTS, ids=IDS)
def test_seq_increments_per_instance_and_element(scn, version, signed):
    key = mh.make_key() if signed else None
    link, peer, gs = build(scn, version, key)
    for i in range(5):
        peer.emit(3, "Status", state=1, temp=0.0, counter=i, info_offset=0)
        time.sleep(0.03)  # TX koalescencjuje eventy (spec rev.4 #9/12) - odstep, by kazdy wyszedl
    assert wait_until(lambda: len(gs.of("SRP_EXAMPLE_STATUS")) == 5)
    seqs = [m["d"]["seq"] for m in gs.of("SRP_EXAMPLE_STATUS")]
    assert [(b - a) & 0xFFFF for a, b in zip(seqs, seqs[1:])] == [1, 1, 1, 1]


@pytest.mark.parametrize("version,signed", VARIANTS, ids=IDS)
def test_gs_request_gets_response_with_same_seq_and_target(scn, version, signed):
    key = mh.make_key() if signed else None
    link, peer, gs = build(scn, version, key)
    gs.send(req_msg(gs.mod, seq=1234, mode=9))
    assert wait_until(lambda: gs.of("SRP_EXAMPLE_SET_MODE_RESP"))
    d = gs.of("SRP_EXAMPLE_SET_MODE_RESP")[0]["d"]
    assert (d["seq"], d["instance"], d["status"], d["mode"]) == (1234, 3, 0, 9)
    assert (d["target_system"], d["target_component"]) == (255, 190)  # RESP.target = nadawca REQ (spec rev.4 #4)
    assert peer.stats["req_executed"] == 1


def test_excluded_method_answers_status_2_without_calling_handler(scn):
    link, peer, gs = build(scn, 2, None, instance_kw=dict(exclude=["SetMode"]))
    gs.send(req_msg(gs.mod, seq=77))
    assert wait_until(lambda: gs.of("SRP_EXAMPLE_SET_MODE_RESP"))
    assert gs.of("SRP_EXAMPLE_SET_MODE_RESP")[0]["d"]["status"] == 2  # spec rev.4 #7
    assert peer.stats["req_executed"] == 0  # handler nie wolany; licznik odrzuconych jest po stronie daemona (jego testy)


def test_unknown_instance_answers_status_2_and_counts(scn):
    link, peer, gs = build(scn, 2, None)
    gs.send(req_msg(gs.mod, seq=5, instance=99))
    assert wait_until(lambda: gs.of("SRP_EXAMPLE_SET_MODE_RESP"))
    d = gs.of("SRP_EXAMPLE_SET_MODE_RESP")[0]["d"]
    assert d["status"] == 2 and d["instance"] == 99
    assert peer.stats["req_executed"] == 0  # spec rev.4 #7: nieznana instancja = RESP status=2 od daemona, bez wykonania


def test_request_for_other_target_is_ignored(scn):
    link, peer, gs = build(scn, 2, None)
    gs.send(req_msg(gs.mod, seq=5, ts=9, tc=0))  # target_system 9 != 1
    time.sleep(0.3)
    assert not gs.of("SRP_EXAMPLE_SET_MODE_RESP") and peer.stats["req_executed"] == 0
    gs.send(req_msg(gs.mod, seq=6, ts=1, tc=1))  # dokladnie nasz
    assert wait_until(lambda: gs.of("SRP_EXAMPLE_SET_MODE_RESP"))


@pytest.mark.parametrize("version,signed", VARIANTS, ids=IDS)
def test_gs_events_reach_proxy_peer_and_duplicate_is_dropped(scn, version, signed):
    key = mh.make_key() if signed else None
    link, peer, gs = build(scn, version, key)
    frame = gs.send(status_msg(gs.mod, seq=10, counter=42))
    if not signed:
        gs.send_raw(frame)  # ta sama ramka drugi raz = duplikat (seq + CRC32 payloadu, spec rev.5 R2)
    else:
        gs.send_raw(frame)  # podpisany: replay odrzucony podpisem, nie dedupem
    assert wait_until(lambda: len(peer.events_rx) >= 1)
    time.sleep(0.3)
    assert len(peer.events_rx) == 1 and peer.events_rx[0]["values"]["counter"] == 42
    st = peer.stats
    assert st["rx_duplicate"] + st["rx_sig_err"] == 1


@pytest.mark.parametrize("version,signed", VARIANTS, ids=IDS)
def test_heartbeat_type_autopilot_and_rate(scn, version, signed):
    """Spec rev.4 #13: HEARTBEAT type = MAV_TYPE_ONBOARD_CONTROLLER (18), autopilot = MAV_AUTOPILOT_INVALID (8);
    heartbeat_hz per lacze (podpisany tak samo jak reszta ruchu)."""
    key = mh.make_key() if signed else None
    link, peer, gs = build(scn, version, key, hb=5)
    time.sleep(1.5)
    hbs = gs.of("HEARTBEAT")
    assert 4 <= len(hbs) <= 11, len(hbs)  # ~5 Hz * 1.5 s
    d = hbs[0]["d"]
    assert (d["type"], d["autopilot"]) == (18, 8)
    assert (hbs[0]["sysid"], hbs[0]["compid"]) == (1, 1)
    if signed:
        assert all(m["raw"][2] & 1 for m in hbs)


def test_no_heartbeat_when_rate_is_zero(scn):
    link, peer, gs = build(scn, 2, None, hb=0)
    time.sleep(0.6)
    assert not gs.of("HEARTBEAT") and len(gs.raw) == 0


# ============================ scenariusz 7: signing ============================================================
def test_wrong_key_is_rejected(scn):
    key = mh.make_key()
    link, peer, gs = build(scn, 2, key, gs_key=mh.make_key())
    gs.send(req_msg(gs.mod))
    time.sleep(0.3)
    assert not gs.of("SRP_EXAMPLE_SET_MODE_RESP") and peer.stats["rx_sig_err"] == 1 and peer.stats["req_executed"] == 0


def test_replayed_frame_is_rejected_and_executes_once(scn):
    key = mh.make_key()
    link, peer, gs = build(scn, 2, key)
    raw = gs.send(req_msg(gs.mod, seq=1))
    assert wait_until(lambda: gs.of("SRP_EXAMPLE_SET_MODE_RESP"))
    gs.send_raw(raw)  # replay tej samej podpisanej ramki
    time.sleep(0.3)
    st = peer.stats
    assert st["req_executed"] == 1 and st["rx_sig_err"] == 1


def test_older_timestamp_on_known_stream_is_rejected(scn):
    key = mh.make_key()
    link, peer, gs = build(scn, 2, key)
    t = mh.signing_timestamp_now()
    gs.send(req_msg(gs.mod, seq=1), timestamp=t + 1000)
    assert wait_until(lambda: gs.of("SRP_EXAMPLE_SET_MODE_RESP"))
    gs.send(req_msg(gs.mod, seq=2), timestamp=t + 10)  # starszy niz ostatni zaakceptowany: replay (spec; pymavlink tego nie wykrywa)
    time.sleep(0.3)
    assert peer.stats["req_executed"] == 1 and peer.stats["rx_sig_err"] == 1


def test_unsigned_rejected_when_accept_unsigned_false(scn):
    key = mh.make_key()
    link, peer, gs = build(scn, 2, key, accept_unsigned=False, gs_key=None)  # GS nie podpisuje
    gs.send(req_msg(gs.mod))
    time.sleep(0.3)
    assert not gs.of("SRP_EXAMPLE_SET_MODE_RESP") and peer.stats["rx_sig_err"] == 1


def test_unsigned_accepted_when_accept_unsigned_true(scn):
    key = mh.make_key()
    link, peer, gs = build(scn, 2, key, accept_unsigned=True, gs_key=None)
    gs.send(req_msg(gs.mod))
    assert wait_until(lambda: gs.of("SRP_EXAMPLE_SET_MODE_RESP"))
    assert peer.stats["req_executed"] == 1


def test_restart_of_both_sides_with_pymavlink_gs_keeps_working(scn):
    """R1: po restarcie peera i GS (zegar rzeczywisty) podpisane ramki obu stron sa akceptowane."""
    key = mh.make_key()
    link = scn.link(baud=921600)
    cfg = peer_cfg([link_cfg("r1", link.peer_dev, 2, key, 1)], [inst_cfg(3, ["r1"])])
    peer = make_peer(scn.impl, cfg).start()
    gs = GS(link.gs_dev, 2, key, link_id=2)
    peer.emit(3, "Status", state=1, temp=0.0, counter=1, info_offset=0)
    gs.send(req_msg(gs.mod, seq=1))
    assert wait_until(lambda: gs.of("SRP_EXAMPLE_STATUS") and gs.of("SRP_EXAMPLE_SET_MODE_RESP"))
    peer.stop()
    gs.stop()
    time.sleep(0.3)
    peer2 = scn.peer(cfg)
    gs2 = scn.gs(link.gs_dev, version=2, key=key, link_id=2)
    peer2.emit(3, "Status", state=1, temp=0.0, counter=2, info_offset=0)
    gs2.send(req_msg(gs2.mod, seq=2))
    assert wait_until(lambda: gs2.of("SRP_EXAMPLE_STATUS") and gs2.of("SRP_EXAMPLE_SET_MODE_RESP"))
    assert gs2.of("SRP_EXAMPLE_STATUS")[0]["d"]["counter"] == 2 and peer2.stats["req_executed"] == 1


# ============================ scenariusz 3: tshark + Lua na ramkach z peera ======================================
@pytest.mark.parametrize("version,signed", VARIANTS, ids=IDS)
def test_tshark_decodes_frames_captured_from_peer(scn, version, signed, tmp_path):
    reason = th.available()
    if reason:
        pytest.skip(reason)
    key = mh.make_key() if signed else None
    link, peer, gs = build(scn, version, key, link_id=4)
    for i in range(3):
        peer.emit(3, "Status", state=5, temp=1.5, counter=1000 + i, info_offset=-7)
        time.sleep(0.03)
    peer.emit(3, "Name", label="abc", value=9.0)
    assert wait_until(lambda: len(gs.of("SRP_EXAMPLE_STATUS")) == 3 and gs.of("SRP_EXAMPLE_NAME"))
    frames = [m["raw"] for m in gs.messages if m["type"] != "HEARTBEAT"]
    pcap = th.write_pcap(str(tmp_path / f"peer_{scn.impl}_{version}_{signed}.pcap"), frames)
    lua = mh.generate_wlua(version)
    rows = th.run_tshark(pcap, lua, [P + "SRP_EXAMPLE_STATUS_instance", P + "SRP_EXAMPLE_STATUS_seq",
                                     P + "SRP_EXAMPLE_STATUS_counter", P + "SRP_EXAMPLE_STATUS_info_offset",
                                     P + "SRP_EXAMPLE_NAME_label", P + "signature_link", P + "sysid"])
    status_rows = [r for r in rows if r[0]]
    assert [r[2] for r in status_rows] == ["1000", "1001", "1002"]
    assert all(r[0] == "3" and r[3] == "-7" and r[6] == "1" for r in status_rows)
    assert all((r[5] == "4") if signed else (r[5] == "") for r in status_rows)  # link_id tylko w podpisanych
    seqs = [int(r[1]) for r in status_rows]
    assert [(b - a) & 0xFFFF for a, b in zip(seqs, seqs[1:])] == [1, 1]
    assert [r[4] for r in rows if r[4]] == ["abc"]


# ============================ scenariusz 8: max_rate_hz, exclude, lacze bez rejestracji =======================================
def test_max_rate_1hz_vs_20hz_on_two_instances(scn):
    link = scn.link(baud=921600)
    cfg = peer_cfg([link_cfg("r1", link.peer_dev)],
                   [inst_cfg(3, ["r1"], max_rate_hz={"Status": 1}), inst_cfg(4, ["r1"], max_rate_hz={"Status": 20})])
    peer = scn.peer(cfg)
    gs = scn.gs(link.gs_dev)
    t0 = time.monotonic()
    i = 0
    while time.monotonic() - t0 < 2.0:  # 100 Hz na obu instancjach
        peer.emit(3, "Status", state=1, temp=0.0, counter=i, info_offset=0)
        peer.emit(4, "Status", state=1, temp=0.0, counter=i, info_offset=0)
        i += 1
        time.sleep(0.01)
    last = i - 1

    def _of(inst):
        return [m for m in gs.of("SRP_EXAMPLE_STATUS") if m["d"]["instance"] == inst]

    # Ostatnia wartosc dojdzie w nastepnym oknie limitu (1 Hz = do ~1 s); pod obciazeniem peer/GS moze to trwac dluzej,
    # wiec czekamy na warunek (a nie stala pauze), a potem na brak dalszych ramek (nic nowego po ostatniej wartosci).
    assert wait_until(lambda: _of(3) and _of(3)[-1]["d"]["counter"] == last and _of(4) and _of(4)[-1]["d"]["counter"] == last,
                      timeout=8.0), (_of(3)[-1:], _of(4)[-1:], last)
    time.sleep(0.3)
    n3, n4 = _of(3), _of(4)
    record("rate_limit", inst3_1hz=len(n3), inst4_20hz=len(n4), emitted=i)
    assert 2 <= len(n3) <= 5 and 38 <= len(n4) <= 52
    assert n3[-1]["d"]["counter"] == last and n4[-1]["d"]["counter"] == last  # nadmiar = ostatnia wartosc, nie kolejka


def test_excluded_event_puts_zero_bytes_on_the_link(scn):
    link = scn.link(baud=921600)
    cfg = peer_cfg([link_cfg("r1", link.peer_dev)], [inst_cfg(3, ["r1"], exclude=["Name"])])
    peer = scn.peer(cfg)
    gs = scn.gs(link.gs_dev)
    for i in range(10):
        peer.emit(3, "Name", label="x", value=1.0)
    time.sleep(0.4)
    assert len(gs.raw) == 0 and peer.stats["tx_excluded"] == 10  # 0 bajtow na lacze (spec: element wylaczony)
    peer.emit(3, "Status", state=1, temp=0.0, counter=1, info_offset=0)
    assert wait_until(lambda: gs.of("SRP_EXAMPLE_STATUS"))


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v", "-p", "no:cacheprovider"]))
