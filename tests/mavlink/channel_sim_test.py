"""Testy symulatora kanalu (AC3): przepustowosc +-10%, utrata 10+-3% przy >=1000 ramek, determinizm po seedzie."""
import os
import select
import sys
import time
import tty

import pytest

import channel_sim
import mav_helpers as mh

STATE = "state" if 'name="state"' in mh.dialect_xml(2) else "status"
from channel_sim import ChannelSim, Impairer


def frames(n, version=2):
    mod = mh.load_dialect(version)
    mav, sink = mh.make_mav(mod)
    out = []
    for i in range(n):
        sink.buf.clear()
        mav.send(mod.MAVLink_srp_example_status_message(
            seq=i % 65536, instance=3, state=i % 256, temp=i * 0.5, counter=i, info_offset=i % 1000))
        out.append(bytes(sink.buf))
    return out


def read_for(fd, seconds, want=None):
    got, stamps = bytearray(), []
    end = time.monotonic() + seconds
    while time.monotonic() < end and (want is None or len(got) < want):
        r, _, _ = select.select([fd], [], [], 0.02)
        if r:
            d = os.read(fd, 65536)
            got += d
            stamps.append((time.monotonic(), len(d)))
    return bytes(got), stamps


def wait_for_stats(sim, pred, timeout=10.0):
    """Czeka (petla z limitem) na ustabilizowanie statystyk zamiast stalego sleep."""
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if pred(sim.stats()["a2b"]):
            return True
        time.sleep(0.01)
    return False


def open_ends(sim):
    a = os.open(sim.path_a, os.O_RDWR | os.O_NOCTTY)
    b = os.open(sim.path_b, os.O_RDWR | os.O_NOCTTY)
    tty.setraw(a)
    tty.setraw(b)
    return a, b


def run_stream(sim_kwargs, data, wait=2.0, want=None):
    with ChannelSim(**sim_kwargs) as sim:
        a, b = open_ends(sim)
        os.write(a, data)
        got, _ = read_for(b, wait, want)
        stats = sim.stats()
        os.close(a)
        os.close(b)
    return got, stats


# ---- logika offline (Impairer) ----
@pytest.mark.parametrize("version", [1, 2])
def test_framer_preserves_frames_without_impairment(version):
    fr = frames(50, version)
    out = Impairer(0, 0, 1).feed(b"".join(fr))
    assert [d for _, d in out] == fr


def test_framer_split_arbitrary_chunks():
    fr = frames(30)
    stream = b"".join(fr)
    imp, got = Impairer(0, 0, 1), []
    for i in range(0, len(stream), 7):
        got += [d for _, d in imp.feed(stream[i:i + 7])]
    assert got == fr


def test_loss_rate_10_percent_over_1000_frames():
    fr = frames(2000)
    out = Impairer(10, 0, 42).feed(b"".join(fr))
    lost = sum(1 for _, d in out if d is None) / len(fr)
    assert len(out) == 2000
    assert 0.07 <= lost <= 0.13, lost


@pytest.mark.parametrize("pct", [0, 1, 25, 50])
def test_loss_rate_various(pct):
    out = Impairer(pct, 0, 7).feed(b"".join(frames(4000)))
    lost = 100.0 * sum(1 for _, d in out if d is None) / 4000
    assert abs(lost - pct) <= 3, lost


def test_determinism_same_seed_same_result_and_other_seed_differs():
    data = b"".join(frames(500))

    def run(seed):
        return [d for _, d in Impairer(20, 20, seed).feed(data)]

    assert run(5) == run(5)
    assert run(5) != run(6)


def test_bitflip_changes_exactly_one_bit_and_breaks_frames():
    mod = mh.load_dialect(2)
    fr = frames(400)
    out = Impairer(0, 100, 3).feed(b"".join(fr))
    for orig, (_, d) in zip(fr, out):
        diff = bin(int.from_bytes(orig, "big") ^ int.from_bytes(d, "big")).count("1")
        assert diff == 1
    good, _ = mh.decode_all(mod, b"".join(d for _, d in out))
    known = [m for m in good if not m.get_type().startswith("UNKNOWN")]
    assert not known  # pojedynczy bitflip zawsze lamie CRC (znane msgid); nieznane msgid nie maja crc_extra


def test_garbage_bytes_are_single_units():
    out = Impairer(0, 0, 1).feed(b"\x01\x02\x03")
    assert [d for _, d in out] == [b"\x01", b"\x02", b"\x03"]


# ---- przez pty ----
def measure_rate(baud, n):
    data = bytes(i % 251 for i in range(n))
    with ChannelSim(baud=baud, seed=1) as sim:
        a, b = open_ends(sim)
        t0 = time.monotonic()
        os.write(a, data)
        got, stamps = read_for(b, 6.0, n)
        os.close(a)
        os.close(b)
    assert got == data
    return len(got) / (stamps[-1][0] - t0)


@pytest.mark.parametrize("baud,n", [(57600, 8640), (115200, 11520)])
def test_throughput_within_10_percent(baud, n):
    """Pomiar zegarowy: pod obciazeniem CPU moze wypasc poza +-10% (opoznienia planisty), dlatego best-of-3:
    wystarczy jedno okno bez zaklocen; zaden pomiar nie moze byc SZYBSZY niz limit +10% (twarda gorna granica)."""
    rates = []
    for _ in range(3):
        rates.append(measure_rate(baud, n))
        assert rates[-1] <= 1.1 * baud / 10, rates
        if 0.9 * baud / 10 <= rates[-1]:
            return
    pytest.fail(f"zaden z 3 pomiarow nie mieści sie w +-10%: {rates}")


def test_pty_loss_10_percent_1500_frames():
    fr = frames(1500)
    got, st = run_stream(dict(loss_pct=10, baud=921600, seed=11), b"".join(fr), wait=3.0)
    good, bad = mh.decode_all(mh.load_dialect(2), got)
    assert not bad
    assert 0.87 <= len(good) / 1500 <= 0.93, len(good)
    assert st["a2b"]["units"] == 1500 and st["a2b"]["dropped"] == 1500 - len(good)


def test_signed_v2_frames_survive_lossy_channel():
    """C8: ramka v2 z podpisem (incompat 0x01, +13 B) musi byc dzielona na jednostki o poprawnej dlugosci; przy 10% utraty
    wszystkie ramki, ktore przeszly, sa nienaruszone i przechodza StrictVerifier, a liczba = units - dropped."""
    mod = mh.load_dialect(2)
    key = mh.make_key()
    mav, sink = mh.make_mav(mod, key=key, link_id=2)
    sent = []
    for i in range(200):
        sink.buf.clear()
        mav.send(mod.MAVLink_srp_example_status_message(seq=i, instance=3, **{STATE: i % 256}, temp=1.0, counter=i,
                                                       info_offset=0))
        sent.append(bytes(sink.buf))
    assert all(f[2] & 1 and len(f) == 10 + f[1] + 2 + 13 for f in sent)
    got, st = run_stream(dict(loss_pct=10, baud=921600, seed=21), b"".join(sent), wait=2.0)
    frames = mh.split_v2_frames(got)
    verifier = mh.StrictVerifier(key, mh.signing_timestamp_now())
    assert frames and all(verifier.check(f) == "ok" for f in frames)  # zadna ramka nie rozcieta ani zmieniona
    assert len(frames) == st["a2b"]["units"] - st["a2b"]["dropped"] == 200 - st["a2b"]["dropped"]
    assert set(frames) <= set(sent) and 150 <= len(frames) <= 195


def test_pty_determinism_same_seed_identical_bytes():
    data = b"".join(frames(300))
    kw = dict(loss_pct=15, bitflip_pct=10, baud=921600, seed=99)
    g1, _ = run_stream(kw, data, wait=1.0)
    g2, _ = run_stream(kw, data, wait=1.0)
    assert g1 == g2 and len(g1) > 0
    g3, _ = run_stream(dict(kw, seed=100), data, wait=1.0)
    assert g3 != g1


def test_pty_matches_offline_impairer():
    data = b"".join(frames(200))
    got, _ = run_stream(dict(loss_pct=15, bitflip_pct=10, baud=921600, seed=5), data, wait=1.0)
    exp = b"".join(d for _, d in Impairer(15, 10, 5).feed(data) if d)
    assert got == exp


def test_delay_adds_latency():
    with ChannelSim(delay_ms=150, seed=1) as sim:
        a, b = open_ends(sim)
        t0 = time.monotonic()
        os.write(a, b"x")
        got, st = read_for(b, 2.0, 1)
        os.close(a)
        os.close(b)
    assert got == b"x"
    assert 0.14 <= st[0][0] - t0 <= 3.0  # dolna granica twarda; gorna tylko jako sanity (zalezy od obciazenia)


def test_bidirectional():
    with ChannelSim(seed=2) as sim:
        a, b = open_ends(sim)
        os.write(a, b"ping")
        os.write(b, b"pong")
        assert read_for(b, 1.0, 4)[0] == b"ping"
        assert read_for(a, 1.0, 4)[0] == b"pong"
        os.close(a)
        os.close(b)


def test_stats_counters():
    fr = frames(100)
    _, st = run_stream(dict(loss_pct=0, baud=921600), b"".join(fr), wait=1.0)
    assert st["a2b"]["units"] == 100 and st["a2b"]["dropped"] == 0
    assert st["a2b"]["bytes_in"] == sum(map(len, fr)) == st["a2b"]["bytes_out"]


def test_unread_receiver_overflows_rx_buffer_instead_of_blocking_sender():
    frame = frames(1)[0]
    with ChannelSim(rx_buffer=2000, seed=1) as sim:
        a, b = open_ends(sim)
        os.set_blocking(a, False)
        sent = 0
        for _ in range(4000):  # ~100 kB, nikt nie czyta po stronie B
            view = memoryview(frame)
            while view:  # zapis nieblokujacy bywa czesciowy (pty pod obciazeniem) - dopisz reszte ramki
                try:
                    view = view[os.write(a, view):]
                except BlockingIOError:
                    time.sleep(0.001)
            sent += 1
        assert wait_for_stats(sim, lambda s: s["units"] == sent), "sim nie przetworzyl wszystkich ramek"
        st = sim.stats()["a2b"]
        assert st["overflow"] > 0 and st["units"] > st["overflow"]
        got, _ = read_for(b, 0.5)
        assert 0 < len(got) < sent * len(frame)
        # nadawca nigdy sie nie zablokowal na dluzej niz chwile: wszystkie ramki przyjete przez sim (read z A)
        assert st["units"] == sent
        os.close(a)
        os.close(b)


def test_truncated_frame_is_flushed_as_raw_bytes_after_idle():
    fr = frames(1)[0]
    part = fr[:len(fr) // 2]
    with ChannelSim(seed=1) as sim:
        a, b = open_ends(sim)
        t0 = time.monotonic()
        os.write(a, part)
        got, stamps = read_for(b, 2.0, len(part))
        assert got == part  # po >100 ms ciszy urwana ramka wychodzi jako surowe bajty
        assert stamps[0][0] - t0 >= 0.09  # nigdy wczesniej niz po ~100 ms (dolna granica nie zalezy od obciazenia)
        os.close(a)
        os.close(b)


def test_cli_prints_paths(capsys):
    assert channel_sim.main(["--duration", "0.1"]) == 0
    assert "A=/dev/pts/" in capsys.readouterr().out


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v", "-p", "no:cacheprovider"]))


