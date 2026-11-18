"""Scenariusz 7 (R1 i rev.6): signing.state_file - restart nadawcy i odbiorcy z persist.

R1: po restarcie pierwszy znacznik czasu nadawcy >= persisted + 60 s (jednorazowa podloga), monotonicznie wzgledem tego,
    co odbiorca juz widzial.
rev.6: odbiorca po restarcie pamieta last_valid_peer_ts - NOWY strumien starszy o > 60 s od niego (ramka nagrana
    przed restartem) jest odrzucany; swieza ramka przechodzi.
Persist dziala co >= 10 s (pierwszy zapis od razu), wiec testy trwaja ~13 s.
"""
import sys
import time

import pytest

import mav_helpers as mh
from peer_api import GS, Scenario, impls, inst_cfg, link_cfg, make_peer, peer_cfg, wait_until


@pytest.fixture(params=impls())
def scn(request):
    s = Scenario(request.param)
    yield s
    s.close()


def read_state(path):
    v, peer = (int(x) for x in open(path).read().split()[:2])
    return v, peer


def ts_of(raw):
    return int.from_bytes(raw[-12:-6], "little")


def test_sender_restart_with_state_file_starts_60s_above_persisted(scn, tmp_path):
    key, state = mh.make_key(), str(tmp_path / "tx.ts")
    link = scn.link(baud=921600)
    cfg = peer_cfg([link_cfg("r1", link.peer_dev, 2, key, 1, state_file=state)], [inst_cfg(3, ["r1"])])
    gs = GS(link.gs_dev, 2, key, link_id=2)
    peer = make_peer(scn.impl, cfg).start()
    t0 = time.monotonic()
    i = 0
    while time.monotonic() - t0 < 11.5:  # >= 10 s: zapis stanu po zmianie
        peer.emit(3, "Status", state=1, temp=0.0, counter=i, info_offset=0)
        i += 1
        time.sleep(0.25)
    assert wait_until(lambda: gs.of("SRP_EXAMPLE_STATUS"), 3.0)
    before = max(ts_of(m["raw"]) for m in gs.messages if m["type"] == "SRP_EXAMPLE_STATUS")
    peer.stop()
    persisted, _ = read_state(state)
    assert persisted >= before - 12 * 100000  # zapis z ostatnich ~10 s (ts w jednostkach 10 us)
    n_before = len(gs.messages)
    peer2 = scn.peer(cfg)
    peer2.emit(3, "Status", state=1, temp=0.0, counter=999, info_offset=0)
    assert wait_until(lambda: len(gs.messages) > n_before, 3.0), "odbiorca (StrictVerifier) odrzucil ramki po restarcie"
    first = ts_of(gs.messages[n_before]["raw"])
    assert first >= persisted + mh.SIXTY_S, (first, persisted)  # R1: podloga +60 s
    assert first > before and not gs.rejected
    gs.stop()


def test_receiver_restart_rejects_frame_recorded_over_60s_before_persisted_peer_ts(scn, tmp_path):
    key, state = mh.make_key(), str(tmp_path / "rx.ts")
    link = scn.link(baud=921600)
    cfg = peer_cfg([link_cfg("r1", link.peer_dev, 2, key, 1, state_file=state)], [inst_cfg(3, ["r1"])])
    gs = GS(link.gs_dev, 2, key, link_id=2)
    peer = make_peer(scn.impl, cfg).start()
    now = mh.signing_timestamp_now()
    mod = mh.load_dialect(2)
    old = gs.send(mod.MAVLink_srp_example_status_message(seq=1, instance=3, state=1, temp=0.0, counter=1, info_offset=0),
                  timestamp=now - 120 * 100000)  # nagrana >60 s przed pozniejszymi
    assert wait_until(lambda: len(peer.events_rx) == 1, 3.0)  # nowy strumien: przyjety (restart bez stanu)
    gs.send(mod.MAVLink_srp_example_status_message(seq=2, instance=3, state=1, temp=0.0, counter=2, info_offset=0),
            timestamp=now)
    assert wait_until(lambda: len(peer.events_rx) == 2, 3.0)
    time.sleep(11.5)  # persist po >= 10 s od pierwszego zapisu
    peer.stop()
    _, persisted_peer = read_state(state)
    assert persisted_peer >= now  # last_valid_peer_ts przezyl na dysku
    peer2 = scn.peer(cfg)
    gs.send_raw(old)  # replay ramki nagranej >60 s przed last_valid_peer_ts
    time.sleep(0.5)
    assert len(peer2.events_rx) == 0 and peer2.stats["rx_sig_err"] == 1  # rev.6: odrzucona po restarcie odbiorcy
    gs.send(mod.MAVLink_srp_example_status_message(seq=3, instance=3, state=1, temp=0.0, counter=3, info_offset=0),
            timestamp=now + 2 * 100000)
    assert wait_until(lambda: len(peer2.events_rx) == 1, 3.0)  # swieza ramka przechodzi
    gs.stop()


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v", "-p", "no:cacheprovider"]))
