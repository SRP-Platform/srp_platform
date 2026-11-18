"""serial2udp: one frame per datagram (v2, signed), no v1, resynchronization after garbage."""
import importlib.util
import os
import socket
import sys
import threading
import tty

import pytest

import serial2udp

DIALECT = next(p for p in os.environ["EXAMPLE_DIALECT"].split()
               if p.endswith(".py") and not p.endswith("_monitor.py"))


class Sink:
    def __init__(self):
        self.buf = bytearray()

    def write(self, data):
        self.buf += data


def encode(msg, force_v1=False, key=None):
    spec = importlib.util.spec_from_file_location("dialect", DIALECT)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    sink = Sink()
    mav = mod.MAVLink(sink, srcSystem=1, srcComponent=1)
    if key:
        mav.signing.secret_key, mav.signing.sign_outgoing, mav.signing.link_id = key, True, 2
        mav.signing.timestamp = 500_000_000_000
    mav.send(getattr(mod, msg[0])(**msg[1]), force_mavlink1=force_v1)
    return bytes(sink.buf)


def forward(stream, expected):
    """Writes `stream` to a pty read by serial2udp; returns the `expected` datagrams received."""
    master, slave = os.openpty()
    tty.setraw(master)
    tty.setraw(slave)  # raw before the first byte, so the order of the threads does not matter
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("127.0.0.1", 0))
    sock.settimeout(3.0)
    stop = threading.Event()
    worker = threading.Thread(target=serial2udp.run, args=(os.ttyname(slave), DIALECT),
                              kwargs=dict(port=sock.getsockname()[1], stop=stop))
    worker.start()
    try:
        for i in range(0, len(stream), 13):  # arbitrary chunks, not frame aligned
            os.write(master, stream[i:i + 13])
        return [sock.recv(512) for _ in range(expected)]
    finally:
        stop.set()
        worker.join()
        sock.close()
        os.close(master)
        os.close(slave)


MODE = ("MAVLink_srp_engine_service_current_mode_message", dict(seq=1, instance=2, mode=3))
HEARTBEAT = ("MAVLink_heartbeat_message", dict(type=2, autopilot=3, base_mode=0, custom_mode=0,
                                               system_status=4, mavlink_version=3))


def test_one_datagram_per_frame_for_v2_and_signed_frames():
    frames = [encode(MODE), encode(MODE, key=bytes(range(32)))]
    assert frames[0][0] == 0xFD and frames[1][2] & 1
    assert forward(b"".join(frames), 2) == frames


def test_a_v1_frame_is_not_forwarded():
    # negative test only: the one place where a v1 frame is injected on purpose
    v1 = encode(HEARTBEAT, force_v1=True)
    v2 = encode(MODE)
    assert v1[0] == 0xFE
    assert forward(v1 + v2, 1) == [v2]


def test_garbage_between_frames_is_skipped():
    frame = encode(MODE)
    assert forward(b"\x00\x13\x7f" + frame + b"\x55\x01\xaa" + frame, 2) == [frame, frame]


def flip_payload_bit(frame):
    broken = bytearray(frame)
    broken[12] ^= 0x01  # inside the payload: header and msgid stay valid, the CRC does not
    return bytes(broken)


def test_a_frame_with_a_bad_crc_is_dropped_and_the_next_one_forwarded():
    good, sentinel = encode(MODE), encode(HEARTBEAT)
    assert forward(flip_payload_bit(good) + good + sentinel, 2) == [good, sentinel]


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v", "-p", "no:cacheprovider"]))
