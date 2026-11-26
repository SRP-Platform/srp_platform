"""Self-test of the channel simulator: pass-through, loss rate, determinism, bit flips, pty bridge."""
import os
import select
import sys
import time
import tty

import pytest

from channel_sim import ChannelSim, Impairer


def frame(i, payload=8):
    """A MAVLink 2 frame with a dummy payload and CRC (the simulator only needs the length)."""
    return bytes([0xFD, payload, 0, 0, i % 256, 1, 1, 0, 0, 0]) + bytes([i % 256]) * payload + b"\x00\x00"


def read_for(fd, seconds, want=None):
    got = bytearray()
    end = time.monotonic() + seconds
    while time.monotonic() < end and (want is None or len(got) < want):
        if select.select([fd], [], [], 0.02)[0]:
            got += os.read(fd, 65536)
    return bytes(got)


def test_frames_pass_unchanged_in_arbitrary_chunks():
    data = b"".join(frame(i, payload=i % 40) for i in range(200))
    imp, out = Impairer(), []
    for i in range(0, len(data), 7):
        out += imp.feed(data[i:i + 7])
    assert b"".join(d for _, d in out) == data


def test_loss_rate_and_determinism_by_seed():
    data = b"".join(frame(i) for i in range(2000))
    kept = [d is not None for _, d in Impairer(loss_pct=10, seed=3).feed(data)]
    assert 0.06 * len(kept) < kept.count(False) < 0.14 * len(kept)
    assert kept == [d is not None for _, d in Impairer(loss_pct=10, seed=3).feed(data)]
    assert kept != [d is not None for _, d in Impairer(loss_pct=10, seed=4).feed(data)]


def test_bit_flip_changes_exactly_one_bit_of_a_frame():
    data = frame(1)
    for _, out in Impairer(bitflip_pct=100, seed=1).feed(data):
        diff = [a ^ b for a, b in zip(data, out) if a != b]
        assert len(out) == len(data) and len(diff) == 1 and bin(diff[0]).count("1") == 1


def test_pty_bridge_matches_the_offline_impairer():
    data = b"".join(frame(i) for i in range(200))
    with ChannelSim(loss_pct=15, bitflip_pct=10, baud=921600, seed=5) as sim:
        a = os.open(sim.path_a, os.O_RDWR | os.O_NOCTTY)
        b = os.open(sim.path_b, os.O_RDWR | os.O_NOCTTY)
        tty.setraw(a)
        tty.setraw(b)
        os.write(a, data)
        got = read_for(b, 1.5)
        os.close(a)
        os.close(b)
    assert got == b"".join(d for _, d in Impairer(15, 10, 5).feed(data) if d)


def test_baud_limit_and_delay_pace_the_link():
    data = b"".join(frame(i) for i in range(100))  # 100 x 22 B
    with ChannelSim(baud=57600, delay_ms=100, seed=1) as sim:
        a = os.open(sim.path_a, os.O_RDWR | os.O_NOCTTY)
        b = os.open(sim.path_b, os.O_RDWR | os.O_NOCTTY)
        tty.setraw(a)
        tty.setraw(b)
        t0 = time.monotonic()
        os.write(a, data)
        got = read_for(b, 6.0, len(data))
        took = time.monotonic() - t0
        os.close(a)
        os.close(b)
    assert got == data
    assert 0.9 * (len(data) * 10 / 57600 + 0.1) <= took <= 1.5 * (len(data) * 10 / 57600 + 0.1), took


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v", "-p", "no:cacheprovider"]))
