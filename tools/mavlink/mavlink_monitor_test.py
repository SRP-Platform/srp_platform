"""mavlink_monitor: frequency, REQ/RESP pairs, channel counters, signatures, the generated script."""
# pylint: disable=missing-function-docstring,redefined-outer-name,protected-access
import json
import os
import random
import socket
import subprocess
import sys
import tty

import pytest

import mavlink_monitor as mm

DIALECT_FILES = os.environ["EXAMPLE_DIALECT"].split()
SYSTEM_FILES = os.environ["EXAMPLE_SYSTEM"].split()


def _file(files, suffix):
    return next(p for p in files if p.endswith(suffix) and (suffix != ".py" or "_monitor" not in p))


DIALECT_PY = _file(DIALECT_FILES, ".py")
with open(_file(DIALECT_FILES, "_monitor.json"), encoding="utf-8") as _f:
    META = json.load(_f)
KEY = bytes(range(32))


@pytest.fixture(scope="module")
def mod():
    return mm.LoadDialect(DIALECT_PY)


class Sink:
    def __init__(self):
        self.buf = bytearray()

    def write(self, data):
        self.buf += data


def frame(mod, name, sysid=1, compid=1, key=None, v1=False, **fields):
    """Encodes one message of the dialect the way a CPU does."""
    sink = Sink()
    mav = mod.MAVLink(sink, srcSystem=sysid, srcComponent=compid)
    if key:
        mav.signing.secret_key, mav.signing.sign_outgoing, mav.signing.link_id = key, True, 2
        mav.signing.timestamp = 500_000_000_000
    mav.send(getattr(mod, "MAVLink_" + name + "_message")(**fields), force_mavlink1=v1)
    return bytes(sink.buf)


def mode(mod, seq, **kw):
    return frame(mod, "srp_engine_service_current_mode", seq=seq, instance=2, mode=3, **kw)


def req(mod, seq, sysid=2, compid=1):
    return frame(mod, "srp_engine_service_set_mode_req", sysid=sysid, compid=compid, seq=seq,
                 instance=2, target_system=1, target_component=1, mode=5)


def resp(mod, seq, status=0, target=(2, 1)):
    return frame(mod, "srp_engine_service_set_mode_resp", seq=seq, instance=2,
                 target_system=target[0], target_component=target[1], status=status, mode=5)


def event(snap, element, instance=2):
    return next(e for e in snap["events"] if e["element"] == element and e["instance"] == instance)


# ---------------------------------------------------------------- frequency

def test_event_frequency_window_average_and_limit(mod):
    mon = mm.Monitor(META, mod, link="radio_b")
    for i in range(100):  # 10 Hz for 10 s; the model limit of this link is 1 Hz
        mon.OnDatagram(mode(mod, i), now=100.0 + i * 0.1)
    ev = event(mon.Snapshot(now=109.9), "EngineService.CurrentMode")
    assert ev["frames"] == 100 and ev["src"] == "1:1"
    assert ev["hz_window"] == pytest.approx(10.0, rel=0.05)
    assert ev["hz_avg"] == pytest.approx(10.0, rel=0.02)
    assert ev["max_rate_hz"] == 1.0 and ev["over_limit"] and ev["in_model"]
    assert "mode=3" in ev["values"] and "seq" not in ev["values"]
    assert 0 <= ev["age_s"] < 0.2


def test_rate_within_the_limit_is_not_marked(mod):
    mon = mm.Monitor(META, mod, link="radio_b")
    for i in range(12):  # 1 Hz
        mon.OnDatagram(mode(mod, i), now=100.0 + i)
    ev = event(mon.Snapshot(now=111.0), "EngineService.CurrentMode")
    assert not ev["over_limit"]
    assert ev["hz_avg"] == pytest.approx(1.0, rel=0.02)


def test_a_silent_event_of_the_model_is_listed_with_zero_frames(mod):
    mon = mm.Monitor(META, mod, link="radio_a")
    snap = mon.Snapshot(now=1.0)
    telemetry = event(snap, "EngineService.Telemetry", 3)
    assert telemetry["frames"] == 0 and telemetry["age_s"] is None and telemetry["hz_avg"] == 0


def test_window_forgets_old_frames(mod):
    mon = mm.Monitor(META, mod)
    for i in range(10):
        mon.OnDatagram(mode(mod, i), now=10.0 + i * 0.1)
    assert event(mon.Snapshot(now=11.0), "EngineService.CurrentMode")["hz_window"] > 0
    assert event(mon.Snapshot(now=30.0), "EngineService.CurrentMode")["hz_window"] == 0


# ---------------------------------------------------------------- methods

def test_methods_pair_by_seq_with_latency_and_status(mod):
    mon = mm.Monitor(META, mod, timeout_ms=500)
    for i in range(5):
        mon.OnDatagram(req(mod, i), now=10.0 + i)
        mon.OnDatagram(resp(mod, i, status=0 if i < 4 else 1), now=10.0 + i + 0.02 * (i + 1))
    mon.OnDatagram(req(mod, 9), now=20.0)  # never answered
    m = mon.Snapshot(now=21.0)["methods"]
    assert len(m) == 1
    m = m[0]
    assert (m["element"], m["instance"], m["requester"]) == ("EngineService.SetMode", 2, "2:1")
    assert (m["req"], m["resp"], m["paired"], m["unanswered"], m["pending"]) == (6, 5, 5, 1, 0)
    assert m["status"] == {"0": 4, "1": 1}
    assert m["latency_ms"]["min"] == pytest.approx(20, abs=1)
    assert m["latency_ms"]["max"] == pytest.approx(100, abs=1)


def test_a_response_without_a_request_and_two_requesters(mod):
    mon = mm.Monitor(META, mod)
    mon.OnDatagram(resp(mod, 1), now=1.0)
    mon.OnDatagram(req(mod, 2, sysid=2, compid=1), now=2.0)
    mon.OnDatagram(req(mod, 2, sysid=2, compid=7), now=2.0)  # same seq, other requester
    snap = mon.Snapshot(now=2.1)
    assert snap["channel"]["resp_without_req"] == 1
    assert {m["requester"] for m in snap["methods"]} == {"2:1", "2:7"}


# ---------------------------------------------------------------- channel

def test_channel_counters(mod):
    mon = mm.Monitor(META, mod, link="radio_b")
    good = mode(mod, 1)
    bad_crc = bytearray(good)
    bad_crc[12] ^= 1
    v1 = frame(mod, "heartbeat", v1=True, type=2, autopilot=3, base_mode=0, custom_mode=0,
               system_status=4, mavlink_version=3)
    unknown = bytes([0xFD, 1, 0, 0, 0, 1, 1, 0x0F, 0x27, 0x00, 0x55, 0x00, 0x00])  # msgid 9999
    outside = frame(mod, "heartbeat", type=2, autopilot=3, base_mode=0, custom_mode=0,
                    system_status=4, mavlink_version=3)
    for i, data in enumerate((good, good, bytes(bad_crc), v1, unknown, outside)):
        mon.OnDatagram(data, now=1.0 + i)
    ch = mon.Snapshot(now=7.0)["channel"]
    assert ch["frames"] == 3 and ch["crc_errors"] == 1 and ch["v1_frames"] == 1
    assert ch["unknown_msgid"] == 1 and ch["out_of_model"] == 1 and ch["duplicate_seq"] == 1
    assert ch["bytes"] == sum(map(len, (good, good, bytes(bad_crc), v1, unknown, outside)))
    assert ch["air_rate_bps"] == 2400 and ch["air_share_pct"] > 0


def test_a_byte_stream_resynchronizes_after_garbage_and_a_bad_crc(mod):
    mon = mm.Monitor(META, mod)
    good = mode(mod, 1)
    broken = bytearray(mode(mod, 2))
    broken[11] ^= 1
    stream = b"\x00\x13\x7f" + good + bytes(broken) + mode(mod, 3) + mode(mod, 4)
    for i in range(0, len(stream), 7):  # not frame aligned
        mon.OnBytes(stream[i:i + 7], now=1.0)
    ch = mon.Snapshot(now=2.0)["channel"]
    assert ch["frames"] == 3 and ch["crc_errors"] == 1 and ch["garbage_bytes"] >= 3


def test_a_noisy_stream_loses_no_real_frame_to_a_false_stx(mod):
    rng = random.Random(21)
    # A false v2 STX with a msgid the dialect does not know, and a false v1 STX, both with a length byte of 30.
    fake_v2 = bytes([0xFD, 30, 0, 0, 0, 1, 1, 0xEE, 0xEE, 0xEE])
    fake_v1 = bytes([0xFE, 30, 0, 1, 1, 0x55])
    frames = [mode(mod, i) for i in range(40)]
    stream = bytearray()
    for i, f in enumerate(frames):
        stream += bytes(rng.randrange(256) for _ in range(rng.randrange(0, 6)))
        stream += fake_v2 if i % 3 == 0 else fake_v1 if i % 3 == 1 else b""
        stream += f
    # A false length byte holds the buffer back until that many bytes arrive: trailing frames flush it.
    stream += b"".join(mode(mod, 100 + i) for i in range(10))
    mon = mm.Monitor(META, mod)
    for i in range(0, len(stream), 13):
        mon.OnBytes(bytes(stream[i:i + 13]), now=1.0)
    ch = mon.Snapshot(now=2.0)["channel"]
    assert ch["frames"] >= len(frames)
    # The same bytes as datagrams are not resynchronized: a datagram is one frame.
    dg = mm.Monitor(META, mod)
    dg.OnDatagram(fake_v2 + frames[0], now=1.0)
    assert dg.Snapshot(now=2.0)["channel"]["frames"] == 0


def test_signatures_are_counted_ok_bad_and_unsigned(mod):
    mon = mm.Monitor(META, mod, key=KEY)
    signed = mode(mod, 1, key=KEY)
    forged = bytearray(mode(mod, 2, key=KEY))
    forged[-1] ^= 1
    other = mode(mod, 3, key=bytes(reversed(range(32))))
    for data in (signed, bytes(forged), other, mode(mod, 4)):
        mon.OnDatagram(data, now=1.0)
    ch = mon.Snapshot(now=2.0)["channel"]
    assert (ch["sig_ok"], ch["sig_bad"], ch["unsigned"], ch["frames"]) == (1, 2, 1, 2)


def test_signed_frames_without_a_key_are_decoded_and_marked_unchecked(mod):
    mon = mm.Monitor(META, mod)
    mon.OnDatagram(mode(mod, 1, key=KEY), now=1.0)
    ch = mon.Snapshot(now=2.0)["channel"]
    assert ch["signed_unchecked"] == 1 and ch["frames"] == 1


def test_filter_by_system_and_component(mod):
    mon = mm.Monitor(META, mod, sysid=1, compid=2)
    mon.OnDatagram(mode(mod, 1, ), now=1.0)
    mon.OnDatagram(frame(mod, "srp_engine_service_current_mode", compid=2, seq=1, instance=2,
                         mode=3), now=1.0)
    ch = mon.Snapshot(now=2.0)["channel"]
    assert ch["frames"] == 1 and ch["filtered"] == 1


def test_a_short_datagram_is_counted_and_never_raises(mod):
    mon = mm.Monitor(META, mod)
    for data in (b"", b"\xfd", b"\xfd\x05\x00", b"\xff" * 30):
        mon.OnDatagram(data, now=1.0)
    ch = mon.Snapshot(now=2.0)["channel"]
    assert ch["frames"] == 0 and ch["short_datagrams"] >= 3


def test_plain_view_has_the_three_sections(mod):
    mon = mm.Monitor(META, mod, link="radio_b")
    mon.OnDatagram(mode(mod, 1), now=1.0)
    mon.OnDatagram(req(mod, 1), now=1.0)
    mon.OnDatagram(resp(mod, 1), now=1.1)
    text = mm.RenderPlain(mon.Snapshot(now=2.0))
    for part in ("EVENT", "METHOD", "CHANNEL", "EngineService.CurrentMode", "EngineService.SetMode",
                 "signatures:"):
        assert part in text, text


# ---------------------------------------------------------------- the generated script

def _run_script(script, extra, datagrams):
    """The generated monitor as a bazel target (`script` = its rootpath) on UDP."""
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    probe.bind(("127.0.0.1", 0))
    port = probe.getsockname()[1]
    probe.close()
    proc = subprocess.Popen([script, "--udp", f"127.0.0.1:{port}", "--jsonl", "--duration", "1.5",
                             *extra], stdout=subprocess.PIPE, text=True)
    sender = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    for _ in range(40):
        for data in datagrams:
            sender.sendto(data, ("127.0.0.1", port))
        if proc.poll() is not None:
            break
        try:
            proc.wait(0.05)
        except subprocess.TimeoutExpired:
            pass
    out, _ = proc.communicate(timeout=10)
    assert proc.returncode == 0
    return [json.loads(line) for line in out.splitlines()]


def test_the_generated_script_of_a_cpu_runs_on_udp(mod):
    lines = _run_script(os.environ["MONITOR_DIALECT"], [], [mode(mod, 0)])
    assert lines and lines[-1]["channel"]["frames"] >= 1
    assert event(lines[-1], "EngineService.CurrentMode")["frames"] >= 1


def test_the_system_monitor_tells_the_cpus_apart_by_component_id():
    sysmod = mm.LoadDialect(_file(SYSTEM_FILES, ".py"))
    with open(_file(SYSTEM_FILES, "_monitor.json"), encoding="utf-8") as f:
        meta = json.load(f)
    assert len(meta["cpus"]) == 2
    assert {(c["system_id"], c["component_id"]) for c in meta["cpus"]} == {(1, 1), (1, 2)}
    mon = mm.Monitor(meta, sysmod)
    for compid in (1, 2):
        mon.OnDatagram(frame(sysmod, "srp_engine_service_current_mode", compid=compid, seq=1,
                             instance=2, mode=3), now=1.0)
    srcs = {e["src"] for e in mon.Snapshot(now=2.0)["events"]
            if e["element"] == "EngineService.CurrentMode" and e["frames"]}
    assert srcs == {"1:1", "1:2"}


def test_the_script_of_the_system_runs():
    sysmod = mm.LoadDialect(_file(SYSTEM_FILES, ".py"))
    data = frame(sysmod, "srp_engine_service_current_mode", compid=2, seq=1, instance=2, mode=3)
    lines = _run_script(os.environ["MONITOR_SYSTEM"], [], [data])
    assert lines[-1]["channel"]["frames"] >= 1


# ---------------------------------------------------------------- serial input (a pty)

def test_a_pty_is_read_like_a_uart(mod):
    master, slave = os.openpty()
    tty.setraw(master)
    tty.setraw(slave)
    src = mm.OpenSerial(os.ttyname(slave), 57600)
    mon = mm.Monitor(META, mod)
    try:
        data = mode(mod, 1) + mode(mod, 2)
        os.write(master, data)
        got = 0
        while got < len(data):
            chunk = src.Read()
            got += len(chunk)
            mon.OnBytes(chunk)
    finally:
        src.Close()
        os.close(master)
        os.close(slave)
    assert mon.Snapshot()["channel"]["frames"] == 2


def test_an_unknown_link_is_refused(capsys):
    assert mm.main(META, DIALECT_PY, ["--udp", "0", "--link", "nope"]) == 2
    assert "unknown link 'nope'" in capsys.readouterr().err


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v", "-p", "no:cacheprovider"]))
