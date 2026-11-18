"""Scenariusz 4: fuzz parsera peera korpusem z T1 + losowe bajty/mutacje (seed w logu).

Kryteria: 0 crashy, 0 zawieszen (peer odpowiada na poprawne REQ po fuzzingu i w trakcie), liczniki bledow rosna
zgodnie z oczekiwaniem (rx_crc_err/rx_dropped > 0, nigdy malejace), dostarczone zdarzenia: zawsze sa te z `must_decode`
korpusu i nigdy wiecej niz dopuszcza referencyjny parser (`reference_pymavlink`) + `optional`.
Czas losowego fuzzingu: MAVLINK_FUZZ_SECONDS (domyslnie 10 s; pelny przebieg wg briefu = 600), seed: MAVLINK_FUZZ_SEED.
ASan/UBSan: dotyczy peera C++ (budowa z --config=asan, jesli toolchain pozwala) - tu: ten sam scenariusz.
"""
import os
import random
import sys
import time

import pytest

import fuzz_corpus as fc
import mav_helpers as mh
from peer_api import Scenario, inst_cfg, link_cfg, make_peer, peer_cfg, primary_impl, record, wait_until
from peer_api import GS


@pytest.fixture
def scn():
    s = Scenario(primary_impl())
    yield s
    s.close()


class Peers:
    """Dwa peery (v1 i v2), kazdy z jednym laczem - dziala tak samo dla ref i dla peera C++ (osobne modele v1/v2)."""

    def __init__(self, p1, p2):
        self.p1, self.p2 = p1, p2

    @property
    def events_rx(self):
        return [dict(e, link="v1") for e in self.p1.events_rx] + [dict(e, link="v2") for e in self.p2.events_rx]

    @property
    def stats(self):
        s1, s2 = self.p1.stats, self.p2.stats
        return {k: s1[k] + s2[k] for k in s1}


def two_version_peer(scn, key=None):
    """key: lacze v2 z podpisem (accept_unsigned=True, zeby przechodzily tez ramki niepodpisane)."""
    l1, l2 = scn.link(baud=0), scn.link(baud=0)
    p1 = scn.peer(peer_cfg([link_cfg("v1", l1.peer_dev, 1)], [inst_cfg(3, ["v1"])], events_limit=100000))
    p2 = scn.peer(peer_cfg([link_cfg("v2", l2.peer_dev, 2, key, 1, accept_unsigned=True)], [inst_cfg(3, ["v2"])],
                           events_limit=100000))
    g1 = scn.gs(l1.gs_dev, version=1)
    g2 = scn.gs(l2.gs_dev, version=2)
    return Peers(p1, p2), g1, g2


def alive(peer, g2, g1=None):
    """Peer zyje i odpowiada: poprawny REQ na lacze v2 -> RESP (najpierw separator domykajacy stany parsera)."""
    for g in (g1, g2):
        if g:
            g.send_raw(fc.ENTRY_SEPARATOR)
    seq = random.randrange(65536)
    g2.send(g2.mod.MAVLink_srp_example_set_mode_req_message(seq=seq, instance=3, target_system=0, target_component=0, mode=1))
    return wait_until(lambda: any(m["d"]["seq"] == seq for m in g2.of("SRP_EXAMPLE_SET_MODE_RESP")), 3.0)


def counters_monotone(prev, cur):
    return all(cur[k] >= prev.get(k, 0) for k in cur)


def test_corpus_from_t1_is_handled_per_manifest(scn):
    corpus = fc.build_corpus(seed=1, count=220)
    peer, g1, g2 = two_version_peer(scn)
    for e in corpus:
        for g in (g1, g2):
            g.send_raw(e["data"] + fc.ENTRY_SEPARATOR)  # wpisy niezalezne (separator domyka niekompletne ramki)
        time.sleep(0.004)
    assert wait_until(lambda: peer.stats["rx_ok"] > 0, 3.0)
    time.sleep(1.0)
    delivered = {"v1": set(), "v2": set()}
    for ev in peer.events_rx:
        delivered[ev["link"]].add(ev["values"]["counter"])
    must = {1: set(), 2: set()}
    allowed = {1: set(), 2: set()}
    for e in corpus:
        for m in e["must_decode"]:
            must[m["ver"]].add(m["counter"])
            allowed[m["ver"]].add(m["counter"])
        for m in e.get("optional", []):
            allowed[m["ver"]].add(m["counter"])
        for ver, _mid, counter in e["reference_pymavlink"]:
            allowed[ver].add(counter)
    for ver, name in ((1, "v1"), (2, "v2")):
        missing = must[ver] - delivered[name]
        extra = delivered[name] - allowed[ver]
        assert not missing, f"{name}: nie dostarczono {sorted(missing)[:10]}"
        assert not extra, f"{name}: dostarczono ponad dopuszczalne {sorted(extra)[:10]}"
    st = peer.stats
    record("fuzz_corpus_t1", entries=len(corpus), rx_ok=st["rx_ok"], rx_crc_err=st["rx_crc_err"], rx_dropped=st["rx_dropped"],
           delivered_v1=len(delivered["v1"]), delivered_v2=len(delivered["v2"]))
    assert st["rx_crc_err"] + st["rx_dropped"] > 0  # smieci/zle CRC musza byc policzone
    assert alive(peer, g2, g1)


def test_truncated_frame_directly_followed_by_valid_frame_is_resynced(scn):
    """Bez ENTRY_SEPARATOR: urwana ramka (v1, v2, v2 podpisana - takze ucieta w podpisie) i zaraz poprawna ramka.
    Poprawna MUSI zostac zdekodowana (resynchronizacja po magic); oczekiwanie = ramki, ktore referencyjny pymavlink
    dekoduje z tego samego strumienia (tu: zawsze co najmniej ta poprawna) - test ma lapac polykanie nastepnej ramki."""
    key = mh.make_key()
    # lacze v2 z kluczem: ramka ucieta w podpisie + nastepna ramka wyglada jak CALA ramka podpisana (CRC nie obejmuje
    # podpisu) - dopiero weryfikacja podpisu ja odrzuca i parser MUSI wtedy zsynchronizowac sie od bajtu po magic
    peers, g1, g2 = two_version_peer(scn, key)
    rng = random.Random(11)
    gen = fc.Gen(rng)
    plan = {"v1": [], "v2": []}
    streams = {"v1": [], "v2": []}
    for i in range(12):
        for ver in (1, 2):
            name = f"v{ver}"
            f, _ = gen.frame(ver, 100 + i)
            cut = f[:rng.randrange(2, len(f))]  # >= 2 B: bajt len znany (przy ucieciu przed len nastepny magic bylby uznany za len - niejednoznacznosc strumienia)
            good, m = gen.frame(ver, 5000 + i + 100 * ver)
            streams[name].append(cut + good)
            plan[name].append(m["counter"])
        sf, _ = gen.signed_frame(300 + i, key)
        cut = sf[:len(sf) - rng.randrange(1, 14)]  # ucieta w podpisie (1..13 B)
        good, m = gen.frame(2, 9000 + i)
        streams["v2"].append(cut + good)
        plan["v2"].append(m["counter"])
    ref = {}
    for name, ver in (("v1", 1), ("v2", 2)):
        data = b"".join(streams[name])
        ref[name] = {c for (_v, _m, c) in fc.known_messages({"data": data}) if _v == ver}
    for s in streams["v1"]:
        g1.send_raw(s)
        time.sleep(0.01)
    for s in streams["v2"]:
        g2.send_raw(s)
        time.sleep(0.01)
    time.sleep(1.0)
    got = {"v1": set(), "v2": set()}
    for ev in peers.events_rx:
        got[ev["link"]].add(ev["values"]["counter"])
    for name in ("v1", "v2"):
        missing = [c for c in plan[name] if c not in got[name]]
        assert not missing, f"{name}: poprawne ramki polkniete przez urwana ramke: {missing} (pymavlink: {sorted(ref[name] & set(plan[name]))})"


def test_random_and_mutated_frames_never_crash_or_hang_peer(scn):
    seed = int(os.environ.get("MAVLINK_FUZZ_SEED", str(int(time.time()) % 100000)))
    seconds = float(os.environ.get("MAVLINK_FUZZ_SECONDS", "10"))
    print(f"FUZZ seed={seed} seconds={seconds}")
    rng = random.Random(seed)
    peer, g1, g2 = two_version_peer(scn)
    valids = [e["data"] for e in fc.build_corpus(seed=seed, count=60) if e["kind"] in ("valid", "glued", "mixed_v1_v2", "signed")]
    prev, sent, checks = {}, 0, 0
    end = time.monotonic() + seconds
    next_check = time.monotonic() + 1.0
    while time.monotonic() < end:
        kind = rng.randrange(4)
        if kind == 0:
            data = bytes(rng.randrange(256) for _ in range(rng.randrange(1, 300)))
        elif kind == 1:  # losowe bajty z magic i wiarygodnym len
            b = bytearray(rng.randrange(256) for _ in range(rng.randrange(12, 300)))
            for _ in range(rng.randrange(1, 6)):
                i = rng.randrange(len(b))
                b[i] = rng.choice([0xFD, 0xFE])
                if i + 1 < len(b):
                    b[i + 1] = rng.choice([0, 1, 9, 13, 255, rng.randrange(256)])
            data = bytes(b)
        else:  # mutacja poprawnej ramki: flip bitow / ciecie / sklejanie
            b = bytearray(rng.choice(valids))
            for _ in range(rng.randrange(1, 4)):
                if b:
                    b[rng.randrange(len(b))] ^= 1 << rng.randrange(8)
            if kind == 3 and b:
                b = b[:rng.randrange(1, len(b) + 1)]
            data = bytes(b)
        for g in (g1, g2):
            g.send_raw(data)
        sent += len(data)
        time.sleep(0.002)
        if time.monotonic() >= next_check:
            next_check = time.monotonic() + 1.0
            cur = peer.stats
            assert counters_monotone(prev, cur), (prev, cur)
            prev = dict(cur)
            assert alive(peer, g2, g1), f"peer przestal odpowiadac (seed={seed})"  # 0 zawieszen w trakcie
            checks += 1
    assert alive(peer, g2, g1)
    st = peer.stats
    record("fuzz_random", seed=seed, seconds=seconds, bytes_sent=sent, liveness_checks=checks + 1, rx_ok=st["rx_ok"],
           rx_crc_err=st["rx_crc_err"], rx_dropped=st["rx_dropped"])
    assert st["rx_crc_err"] + st["rx_dropped"] > 0


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v", "-p", "no:cacheprovider"]))
