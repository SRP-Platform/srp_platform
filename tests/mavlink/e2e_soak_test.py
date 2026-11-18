"""Scenariusz 9: soak - 2 lacza, eventy + metody co 100 ms, kanal 5% utraty.

Czas: MAVLINK_SOAK_SECONDS (domyslnie 20 s = smoke; wg briefu 1800 s = 30 min: target `e2e_soak_30min`, manual).
Kryteria: brak wyciekow (RSS peera: koniec w granicach +-10% wzgledem poziomu po rozgrzewce, + stala 4 MB tolerancji
na szum alokatora), brak zawieszen (petla nie traci ticków, liczniki rosna w ostatnim oknie), liczniki spojne.
"""
import os
import sys
import time

import pytest

from peer_api import Scenario, inst_cfg, link_cfg, peer_cfg, peer_rss_kb, primary_impl, record


def test_soak_two_links_5pct_loss_events_and_methods_every_100ms():
    seconds = float(os.environ.get("MAVLINK_SOAK_SECONDS", "20"))
    scn = Scenario(primary_impl())
    try:
        links = [scn.link(baud=921600, seed=40 + i, loss_pct=5) for i in range(2)]
        names = ["r1", "r2"]
        lim = 500  # historia zdarzen peera przycinana (test nie moze sam "przeciekac")
        skel = scn.peer(peer_cfg([link_cfg(n, l.peer_dev) for n, l in zip(names, links)], [inst_cfg(3, names)],
                                 sysid=1, events_limit=lim))
        proxy = scn.peer(peer_cfg([link_cfg(n, l.gs_dev) for n, l in zip(names, links)], [inst_cfg(3, names)],
                                  sysid=2, events_limit=lim))
        t0 = time.monotonic()
        tick, emitted, ok, calls = 0, 0, 0, 0
        rss = {}
        marks = {"warm": 0.25, "late": 0.85}
        last_window = None
        next_t = t0
        while time.monotonic() - t0 < seconds:
            now = time.monotonic()
            skel.emit(3, "Status", state=1, temp=0.5, counter=emitted & 0xFFFFFFFF, info_offset=0)
            emitted += 1
            calls += 1
            ok += proxy.call(3, "SetMode", timeout=0.08, mode=tick & 0xFF)[0] == "ok"
            tick += 1
            frac = (time.monotonic() - t0) / seconds
            for k, f in marks.items():
                if k not in rss and frac >= f:
                    rss[k] = (peer_rss_kb(skel), peer_rss_kb(proxy))
                    if k == "late":
                        last_window = (proxy.stats["rx_ok"], skel.stats["req_executed"])
            next_t += 0.1
            time.sleep(max(0.0, next_t - time.monotonic()))
        elapsed = time.monotonic() - t0
        end_rss = (peer_rss_kb(skel), peer_rss_kb(proxy))
        ps, ss = proxy.stats, skel.stats
        expected_ticks = elapsed / 0.1
        record("soak", seconds=round(elapsed, 1), ticks=tick, emitted=emitted, calls=calls, calls_ok=ok,
               call_timeouts=ps["call_timeout"], rx_duplicate_proxy=ps["rx_duplicate"], req_executed=ss["req_executed"],
               rss_warm_kb=list(rss["warm"]), rss_end_kb=list(end_rss))
        assert tick >= 0.9 * expected_ticks, "petla gubi ticki (zawieszenie?)"
        assert (ps["rx_ok"], ss["req_executed"]) > last_window  # liczniki rosna w ostatnim oknie
        # spojnosc licznikow: kazde wywolanie to sukces albo timeout; skeleton wykonal <= wywolan, >= sukcesow
        assert ps["calls"] == calls and ok + ps["call_timeout"] == calls
        assert ok <= ss["req_executed"] <= calls
        # 5% utraty na dwoch laczach: sukces wywolania ~ 1-(1-0.95^2... )^2 ~ blisko 100%
        assert ok / calls >= 0.93
        for who, w, e in (("skeleton", rss["warm"][0], end_rss[0]), ("proxy", rss["warm"][1], end_rss[1])):
            assert e <= 1.10 * w + 4096, f"RSS {who}: {w} kB -> {e} kB (wyciek?)"
    finally:
        scn.close()


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v", "-p", "no:cacheprovider"]))
