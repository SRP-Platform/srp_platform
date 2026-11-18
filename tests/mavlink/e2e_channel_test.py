"""Scenariusze 5 i 6: kanal stratny (10%/30% utraty, bitflip 0,1%, opoznienie) oraz redundancja 2 laczy.

Topologia: skeleton (peer A) --[ChannelSim]-- proxy (peer B); przy redundancji dwa lacza r1/r2 (dwa ChannelSim).
Liczby (dostarczone/zgubione/duplikaty, odsetek timeoutow metod) trafiaja do raportu testu (`record`).
Peer = `primary_impl()` (peer C++ z MAVLINK_PEER).
"""
import sys
import time

import pytest

from peer_api import Scenario, inst_cfg, link_cfg, make_peer, peer_cfg, primary_impl, record, wait_until


@pytest.fixture
def scn():
    s = Scenario(primary_impl())
    yield s
    s.close()


def topology(scn, links_kw, skel_kw=None, proxy_kw=None, inst_kw=None):
    """links_kw: lista argumentow ChannelSim per lacze. Zwraca (links, skeleton, proxy)."""
    links = [scn.link(baud=921600, seed=10 + i, **kw) for i, kw in enumerate(links_kw)]
    names = [f"r{i + 1}" for i in range(len(links))]
    ikw = inst_kw or {}
    skel = scn.peer(peer_cfg([link_cfg(n, l.peer_dev) for n, l in zip(names, links)],
                             [inst_cfg(3, names, **ikw)], sysid=1, compid=1, events_limit=100000, **(skel_kw or {})))
    proxy = scn.peer(peer_cfg([link_cfg(n, l.gs_dev) for n, l in zip(names, links)],
                              [inst_cfg(3, names, **ikw)], sysid=2, compid=1, events_limit=100000, **(proxy_kw or {})))
    return links, skel, proxy


def emit_n(skel, n, start=0, pace=0.002):
    """Emisja z odstepem: TX koalescencjuje eventy (zostaje najnowsza wartosc per element - spec rev.4 #9/12), wiec seria
    bez przerw gubilaby ramki jeszcze przed lacznem. Dodatkowo testy liczace dostarczenie licza wzgledem ramek NADANYCH."""
    for i in range(start, start + n):
        skel.emit(3, "Status", state=1, temp=0.0, counter=i, info_offset=-(i % 1000))
        time.sleep(pace)


def settle(peer, quiet=0.6, timeout=15.0):
    """Czeka az liczba zdarzen przestanie rosnac."""
    end, last, last_t = time.monotonic() + timeout, -1, time.monotonic()
    while time.monotonic() < end:
        n = len(peer.events_rx)
        if n != last:
            last, last_t = n, time.monotonic()
        elif time.monotonic() - last_t >= quiet:
            return n
        time.sleep(0.05)
    return len(peer.events_rx)


def unique_counters(peer):
    return {e["values"]["counter"] for e in peer.events_rx}


def consistent(e):
    c = e["values"]["counter"]
    return e["values"]["info_offset"] == -(c % 1000) and e["values"]["state"] == 1


# ============================ scenariusz 5: kanal stratny ======================================================
@pytest.mark.parametrize("loss", [10, 30])
def test_event_delivery_matches_channel_loss(scn, loss):
    n = 2000
    links, skel, proxy = topology(scn, [dict(loss_pct=loss)])
    emit_n(skel, n)
    got = settle(proxy)
    uniq = unique_counters(proxy)
    st = proxy.stats
    ratio = len(uniq) / n
    record(f"loss_{loss}pct_events", sent=n, delivered=len(uniq), lost=n - len(uniq), duplicates=got - len(uniq),
           rx_duplicate=st["rx_duplicate"], rx_crc_err=st["rx_crc_err"], ratio=round(ratio, 4))
    assert abs(ratio - (1 - loss / 100)) <= 0.03, ratio
    assert got == len(uniq) and st["rx_duplicate"] == 0  # jedno lacze: 0 duplikatow
    assert all(consistent(e) for e in proxy.events_rx)  # nic uszkodzonego nie przechodzi


def test_bitflip_0_1_percent_never_delivers_corrupted_content(scn):
    n = 3000
    links, skel, proxy = topology(scn, [dict(bitflip_pct=0.1)])
    emit_n(skel, n)
    got = settle(proxy)
    uniq = unique_counters(proxy)
    st = proxy.stats
    record("bitflip_0_1pct_events", sent=n, delivered=len(uniq), lost=n - len(uniq), rx_crc_err=st["rx_crc_err"],
           rx_dropped=st["rx_dropped"])
    assert all(consistent(e) for e in proxy.events_rx)  # zero zepsutych tresci (CRC lapie pojedynczy bit)
    assert got == len(uniq)
    if not 0.985 <= len(uniq) / n <= 1.0:
        pytest.fail(f"deframer nie resynchronizuje sie po uszkodzonej ramce: dostarczone {len(uniq)}/{n}, "
                    f"przy ~{round(n * 0.001)} uszkodzonych ramkach")


def test_delay_100ms_delivers_everything_with_latency(scn):
    links, skel, proxy = topology(scn, [dict(delay_ms=100)])
    t0 = time.monotonic()
    emit_n(skel, 1)
    assert wait_until(lambda: proxy.events_rx, 3.0)
    latency = time.monotonic() - t0
    emit_n(skel, 49, start=1)
    assert settle(proxy) == 50
    record("delay_100ms", latency_first_s=round(latency, 3), delivered=50)
    assert 0.1 <= latency < 0.6


@pytest.mark.parametrize("loss,n", [(10, 200), (30, 150)])
def test_method_timeout_rate_matches_loss(scn, loss, n):
    links, skel, proxy = topology(scn, [dict(loss_pct=loss)])
    ok = 0
    for _ in range(n):
        r = proxy.call(3, "SetMode", timeout=0.1, mode=4)
        ok += r[0] == "ok"
    p = loss / 100
    expected_ok = (1 - p) ** 2  # REQ i RESP musza przejsc przez lacze
    ex = skel.stats["req_executed"]
    record(f"loss_{loss}pct_methods", calls=n, ok=ok, timeouts=n - ok, timeout_rate=round(1 - ok / n, 3),
           expected_timeout_rate=round(1 - expected_ok, 3), executed_by_skeleton=ex)
    assert abs(ok / n - expected_ok) <= 0.10, (ok, n)
    assert abs(ex / n - (1 - p)) <= 0.10  # REQ dochodzi z prawdop. 1-p (RESP moze zginac - bez retry)
    assert proxy.stats["call_timeout"] == n - ok


# ============================ scenariusz 6: redundancja 2 laczy ================================================
def test_one_dead_link_still_delivers_everything_once(scn):
    n = 300
    links, skel, proxy = topology(scn, [dict(loss_pct=100), dict()])
    emit_n(skel, n)
    got = settle(proxy)
    ok = sum(proxy.call(3, "SetMode", timeout=1.0, mode=1)[0] == "ok" for _ in range(30))
    record("redundancy_one_dead", sent=n, delivered=len(unique_counters(proxy)), duplicates_in_app=got - len(unique_counters(proxy)),
           calls=30, calls_ok=ok)
    assert len(unique_counters(proxy)) == n and got == n and ok == 30
    assert proxy.stats["rx_duplicate"] == 0 and skel.stats["req_executed"] == 30


def test_both_links_lossy_union_delivery_and_zero_app_duplicates(scn):
    n, p = 2000, 0.30
    links, skel, proxy = topology(scn, [dict(loss_pct=30), dict(loss_pct=30)])
    emit_n(skel, n)
    got = settle(proxy)
    uniq = len(unique_counters(proxy))
    st = proxy.stats
    record("redundancy_both_30pct", sent=n, delivered=uniq, lost=n - uniq, duplicates_in_app=got - uniq,
           rx_duplicate=st["rx_duplicate"], expected_delivery=round(1 - p * p, 3))
    assert abs(uniq / n - (1 - p * p)) <= 0.04
    assert got == uniq  # 0 duplikatow w aplikacji
    assert abs(st["rx_duplicate"] / n - (1 - p) ** 2) <= 0.06  # obie kopie doszly: ~49%
    assert all(consistent(e) for e in proxy.events_rx)


def test_second_link_delayed_3s_gives_no_duplicates_and_executes_request_once(scn):
    n = 50
    links, skel, proxy = topology(scn, [dict(), dict(delay_ms=3000)])
    emit_n(skel, n)
    assert settle(proxy, quiet=0.8) == n
    ok = sum(proxy.call(3, "SetMode", timeout=1.0, mode=7)[0] == "ok" for _ in range(20))
    assert ok == 20
    assert wait_until(lambda: proxy.stats["rx_duplicate"] >= n and skel.stats["rx_duplicate"] >= 20, 8.0)  # spoznione kopie
    time.sleep(0.5)
    record("redundancy_delay_3s", sent=n, delivered=len(proxy.events_rx), calls=20, req_executed=skel.stats["req_executed"],
           proxy_rx_duplicate=proxy.stats["rx_duplicate"], skeleton_rx_duplicate=skel.stats["rx_duplicate"])
    assert len(proxy.events_rx) == n  # zadnych duplikatow w aplikacji po dojsciu spoznionych kopii
    assert skel.stats["req_executed"] == 20  # REQ wykonany 1x mimo drugiej kopii po 3 s


def test_sender_restart_after_1s_loses_no_frames_with_different_content(scn):
    """R2: po restarcie nadawcy seq moze sie powtorzyc (tu: wymuszone seq_start), ale inna tresc != duplikat;
    identyczne seq + identyczny payload w oknie TTL = duplikat."""
    link = scn.link(baud=921600)
    proxy = scn.peer(peer_cfg([link_cfg("r1", link.gs_dev)], [inst_cfg(3, ["r1"])], sysid=2))

    def sender():
        return make_peer(scn.impl, peer_cfg([link_cfg("r1", link.peer_dev)], [inst_cfg(3, ["r1"])], seq_start=100)).start()

    s1 = sender()
    for i in range(1, 6):
        s1.emit(3, "Status", state=1, temp=0.0, counter=i, info_offset=0)
        time.sleep(0.03)  # TX koalescencjuje eventy - odstep, by kazdy wyszedl
    assert wait_until(lambda: len(proxy.events_rx) == 5)
    s1.stop()
    time.sleep(1.0)
    s2 = sender()  # seq znow od 100, inne counter
    for i in range(11, 16):
        s2.emit(3, "Status", state=1, temp=0.0, counter=i, info_offset=0)
        time.sleep(0.03)
    assert wait_until(lambda: len(proxy.events_rx) == 10)
    assert [e["values"]["counter"] for e in proxy.events_rx] == [1, 2, 3, 4, 5, 11, 12, 13, 14, 15]
    assert proxy.stats["rx_duplicate"] == 0
    s2.stop()
    s3 = sender()  # to samo seq=100 i ta sama tresc co pierwsza ramka s1 -> duplikat
    s3.emit(3, "Status", state=1, temp=0.0, counter=1, info_offset=0)
    assert wait_until(lambda: proxy.stats["rx_duplicate"] == 1)
    assert len(proxy.events_rx) == 10
    s3.stop()
    record("redundancy_restart_1s", delivered=10, lost_different_content=0, duplicates_dropped=1)


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v", "-p", "no:cacheprovider"]))
