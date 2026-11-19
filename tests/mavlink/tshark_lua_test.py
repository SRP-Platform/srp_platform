"""tshark with the generated Lua dissector decodes the frames of the CPU dialect.

A missing tshark fails the test. The dissector decodes fields only: it neither checks the CRC
nor verifies a signature (that is the parser's job).
"""
import re
import sys
from datetime import datetime, timedelta, timezone

import pytest

import harness as hn
import mav_helpers as mh
import tshark_harness as th

P = "mavlink_proto."


@pytest.fixture(scope="module")
def dialect():
    th.require_tshark()
    return hn.load_dialect_module(), hn.dialect_file(".lua")


def parse_time(text):
    """tshark prints either `Feb 27, 2015 20:53:21.2345 UTC` or ISO 8601 (tshark 4.x)."""
    iso = re.match(r"(\d{4})-(\d\d)-(\d\d)T(\d\d):(\d\d):(\d\d)", text)
    if iso:
        return datetime(*map(int, iso.groups()), tzinfo=timezone.utc)
    m = re.match(r"(\w+)\s+(\d+), (\d+) (\d+):(\d+):(\d+)", text)
    assert m, text
    return datetime.strptime(" ".join(m.group(1, 2, 3)) + " " + ":".join(m.group(4, 5, 6)),
                             "%b %d %Y %H:%M:%S").replace(tzinfo=timezone.utc)


def decode(lua, frames, fields, tmp_path):
    return th.run_tshark(th.write_pcap(str(tmp_path / "f.pcap"), frames), lua, fields)


def test_v1_frame(dialect, tmp_path):
    mod, lua = dialect
    mav, sink = mh.make_mav(mod, sysid=5, compid=6)
    mav.send(mod.MAVLink_heartbeat_message(type=2, autopilot=3, base_mode=0, custom_mode=0,
                                           system_status=4, mavlink_version=3),
             force_mavlink1=True)
    assert sink.buf[0] == 0xFE
    rows = decode(lua, [bytes(sink.buf)], [P + "msgid", P + "sysid", P + "compid"], tmp_path)
    assert rows == [["0", "5", "6"]]


def test_v2_frames_with_values(dialect, tmp_path):
    mod, lua = dialect
    mav, sink = mh.make_mav(mod, sysid=5, compid=6)
    mav.send(mod.MAVLink_srp_engine_service_current_mode_message(seq=11, instance=3, mode=9))
    mav.send(mod.MAVLink_srp_engine_service_set_mode_resp_message(
        seq=12, instance=3, target_system=2, target_component=1, status=2, mode=7))
    frames = mh.split_v2_frames(bytes(sink.buf))
    rows = decode(lua, frames, [P + "msgid", P + "sysid", P + "SRP_ENGINE_SERVICE_CURRENT_MODE_seq",
                                P + "SRP_ENGINE_SERVICE_CURRENT_MODE_instance",
                                P + "SRP_ENGINE_SERVICE_CURRENT_MODE_mode",
                                P + "SRP_ENGINE_SERVICE_SET_MODE_RESP_status",
                                P + "SRP_ENGINE_SERVICE_SET_MODE_RESP_target_system"], tmp_path)
    assert rows[0][:5] == ["42100", "5", "11", "3", "9"]
    assert rows[1][0] == "42111" and rows[1][5:] == ["2", "2"]


def test_signed_frame_exposes_link_id_timestamp_and_signature(dialect, tmp_path):
    mod, lua = dialect
    ts = 500_000_000_000 + 123_456  # 10 us units since 2015-01-01
    mav, sink = mh.make_mav(mod, sysid=9, compid=8, key=mh.make_key(), link_id=3, timestamp=ts)
    mav.send(mod.MAVLink_srp_engine_service_current_mode_message(seq=1, instance=2, mode=1))
    frame = bytes(sink.buf)
    assert frame[2] & 1
    rows = decode(lua, [frame], [P + "msgid", P + "signature_link", P + "signature_signature",
                                 P + "signature_time"], tmp_path)
    msgid, link, sig, when = rows[0]
    assert msgid == "42100" and link == "3"
    assert sig.replace(":", "").lower() == frame[-6:].hex()
    expected = datetime(2015, 1, 1, tzinfo=timezone.utc) + timedelta(microseconds=ts * 10)
    assert abs((parse_time(when) - expected).total_seconds()) < 2, (when, expected)


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v", "-p", "no:cacheprovider"]))
