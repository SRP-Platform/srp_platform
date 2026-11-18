"""Testy serial2udp: jedna ramka na datagram (v1, v2, v2 podpisane), resynchronizacja, tshark dekoduje strumien z narzedzia."""
import os
import random
import select
import socket
import sys
import threading
import time
import tty

import pytest

import mav_helpers as mh
import serial2udp
import tshark_harness as th


FIELD = "state" if 'name="state"' in mh.dialect_xml(2) else "status"  # nazwa pola zalezy od wersji fikstury dialektu


def status(mod, **kw):
    d = dict(seq=1, instance=3, temp=21.5, counter=1, info_offset=-3, **{FIELD: 7})
    d.update(kw)
    return mod.MAVLink_srp_example_status_message(**d)


def make_frames(n_each=5):
    m1, m2 = mh.load_dialect(1), mh.load_dialect(2)
    key = mh.make_key()
    frames = []
    for i in range(n_each):
        frames.append(mh.encode(m1, status(m1, counter=100 + i)))
        frames.append(mh.encode(m2, status(m2, counter=200 + i)))
        frames.append(mh.encode(m2, status(m2, counter=300 + i), key=key, link_id=2))
    return frames


class Run:
    """serial2udp na pty: zapis do mastera, odbior datagramow na lokalnym UDP."""

    def __init__(self, crc_extra=None):
        self.master, slave = os.openpty()
        tty.setraw(self.master)
        self.path = os.ttyname(slave)
        self._slave = slave
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("127.0.0.1", 0))
        self.port = self.sock.getsockname()[1]
        self.stop = threading.Event()
        self.t = threading.Thread(target=lambda: serial2udp.run(self.path, 57600, "127.0.0.1", self.port, stop=self.stop,
                                                     crc_extra=crc_extra))
        self.t.start()
        time.sleep(0.1)

    def write(self, data, chunk=None):
        rng = random.Random(1)
        i = 0
        while i < len(data):
            n = chunk or rng.randrange(1, 40)
            os.write(self.master, data[i:i + n])
            i += n
            time.sleep(0.001)

    def datagrams(self, expect, timeout=3.0):
        got, end = [], time.monotonic() + timeout
        while len(got) < expect and time.monotonic() < end:
            if select.select([self.sock], [], [], 0.05)[0]:
                got.append(self.sock.recv(65535))
        time.sleep(0.2)
        while select.select([self.sock], [], [], 0)[0]:
            got.append(self.sock.recv(65535))
        return got

    def close(self):
        self.stop.set()
        self.t.join(timeout=3)
        for fd in (self.master, self._slave):
            os.close(fd)
        self.sock.close()


@pytest.fixture
def run():
    r = Run()
    yield r
    r.close()


def dialect_crc_extra():
    """crc_extra z obu dialektow fikstury (v1 i v2): msgid sa rozne (200.. vs 42000..)."""
    m1, m2 = mh.load_dialect(1), mh.load_dialect(2)
    return lambda msgid: getattr((m1.mavlink_map.get(msgid) or m2.mavlink_map.get(msgid)), "crc_extra", None)


def test_one_frame_per_datagram_for_v1_v2_and_signed_v2(run):
    frames = make_frames(5)
    run.write(b"".join(frames))  # losowe kawalki 1-39 B: ramki przechodza przez granice odczytow
    assert run.datagrams(len(frames)) == frames  # dokladnie te same bajty, kolejnosc zachowana, 1 ramka = 1 datagram


def test_leading_garbage_and_false_start_are_skipped_without_dialect(run):
    frames = make_frames(3)
    stream = b"\x00\x11" + frames[0] + b"\xFD\x05\x00" + b"zzzz" + frames[1] + frames[2]
    run.write(stream)
    assert run.datagrams(3) == frames[:3]


def test_garbage_between_frames_is_skipped_with_dialect_crc():
    r = Run(crc_extra=dialect_crc_extra())
    try:
        frames = make_frames(3)
        stream = (b"\x00\x11" + frames[0] + b"\xFD\x05\x00" + b"zzzz" + frames[1] + b"\x01" + frames[2]
                  + b"\xFE" + b"\x99" * 7 + frames[3] + b"\xFD\xFD\xFD" + frames[4])
        r.write(stream)
        assert r.datagrams(5, timeout=5.0) == frames[:5]  # zly CRC / falszywe magic odrzucone, kazda prawdziwa ramka dojechala
    finally:
        r.close()


def test_binary_with_dialect_validates_crc(tmp_path):
    """Komenda z README: bazel run //tools/mavlink:serial2udp -- <pty> --dialect <dialekt>.py (py_binary, nie biblioteka)."""
    import shutil
    import subprocess

    exe = os.path.abspath(os.environ["SERIAL2UDP"])
    dialect = tmp_path / "srp_dialect.py"
    mh.generate_dialect(mh.dialect_xml(2), 2, str(tmp_path), name="srp_dialect")
    assert dialect.exists(), list(tmp_path.iterdir())
    frames = make_frames(2)
    master, slave = os.openpty()
    tty.setraw(master)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("127.0.0.1", 0))
    proc = subprocess.Popen([exe, os.ttyname(slave), "--port", str(sock.getsockname()[1]), "--dialect", str(dialect),
                             "--duration", "6"], stderr=subprocess.PIPE)
    try:
        time.sleep(1.5)
        v2 = [f for f in frames if f[0] == 0xFD]
        os.write(master, b"\x00\x01" + v2[0] + b"zz" + v2[1])
        got = []
        end = time.monotonic() + 4
        while len(got) < 2 and time.monotonic() < end:
            if select.select([sock], [], [], 0.1)[0]:
                got.append(sock.recv(65535))
        assert got == v2[:2], proc.poll()
    finally:
        proc.terminate()
        err = proc.communicate(timeout=10)[1].decode(errors="replace")
        os.close(master)
        os.close(slave)
        sock.close()
    assert "ModuleNotFoundError" not in err, err


def test_partial_frame_waits_for_the_rest(run):
    f = make_frames(1)[1]
    run.write(f[:5], chunk=5)
    assert not select.select([run.sock], [], [], 0.1)[0]  # niedokonczona ramka nie jest wysylana (cisza < 0.3 s)
    run.write(f[5:], chunk=len(f))
    assert run.datagrams(1) == [f]


def test_framer_unit_resync_and_signed_length():
    fr = serial2udp.Framer()
    frames = make_frames(2)
    out = fr.feed(b"\x42" + frames[2][:-3])  # podpisana ucieta w podpisie
    assert out == []
    assert fr.feed(frames[2][-3:] + frames[0]) == [frames[2], frames[0]]


def test_tshark_decodes_frames_forwarded_by_serial2udp(run, tmp_path):
    reason = th.available()
    if reason:
        pytest.skip(reason)
    m2 = mh.load_dialect(2)
    frames = [mh.encode(m2, status(m2, counter=700 + i, seq=i)) for i in range(4)]
    run.write(b"".join(frames))
    dgrams = run.datagrams(4)
    assert dgrams == frames
    pcap = th.write_pcap(str(tmp_path / "s2u.pcap"), dgrams)  # to, co Wireshark zobaczylby na lo:14550
    rows = th.run_tshark(pcap, mh.generate_wlua(2), ["mavlink_proto.SRP_EXAMPLE_STATUS_counter",
                                                      "mavlink_proto.SRP_EXAMPLE_STATUS_instance"])
    assert [r[0] for r in rows] == ["700", "701", "702", "703"] and all(r[1] == "3" for r in rows)


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v", "-p", "no:cacheprovider"]))
