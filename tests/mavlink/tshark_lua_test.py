"""AC2: tshark + Lua (mavgen --lang=WLua) dekoduje ramki z fikstury: >= 3 typy wiadomosci,
wartosci pol rowne nadanym, v1 / v2 / v2 podpisane.

Brak tshark = skip.
Uwaga: dissector WLua z mavgen NIE sprawdza CRC ani podpisu - dekoduje pola; walidacja to rola parsera.
"""
import os
import re
import sys
from datetime import datetime, timedelta, timezone

import pytest

import mav_helpers as mh
import tshark_harness as th

P = "mavlink_proto."


@pytest.fixture(autouse=True)
def _need_tshark():
    reason = th.available()
    if reason:
        pytest.skip(reason)


@pytest.fixture(scope="module", params=[1, 2], ids=["v1", "v2"])
def env(request, tmp_path_factory):
    v = request.param
    return v, mh.load_dialect(v), mh.generate_wlua(v), tmp_path_factory.mktemp(f"pcap{v}")


def messages(mod):
    return [
        mod.MAVLink_srp_example_status_message(seq=11, instance=3, state=7, temp=21.5, counter=123456, info_offset=-300),
        mod.MAVLink_srp_example_name_message(seq=12, instance=3, label=b"hello", value=2.5),
        mod.MAVLink_srp_example_set_mode_req_message(seq=13, instance=3, target_system=2, target_component=3, mode=9),
        mod.MAVLink_srp_example_set_mode_resp_message(seq=13, instance=3, target_system=1, target_component=1, status=2, mode=9),
    ]


def decode(env, frames, fields, name):
    v, mod, lua, d = env
    pcap = th.write_pcap(str(d / name), frames)
    return th.run_tshark(pcap, lua, fields)


def split_frames(buf):
    out, i = [], 0
    while i < len(buf):
        n = 10 + buf[i + 1] + 2 + (13 if buf[i + 2] & 1 else 0)
        out.append(buf[i:i + n])
        i += n
    return out


def test_message_types_and_values(env):
    v, mod, lua, d = env
    ids = mh.IDS[v]
    frames = [mh.encode(mod, m, sysid=5, compid=6) for m in messages(mod)]
    rows = decode(env, frames, [P + "msgid", P + "sysid", P + "compid"], "plain.pcap")
    assert [r[0] for r in rows] == [str(ids[k]) for k in ("status", "name", "mode_req", "mode_resp")]
    assert all(r[1] == "5" and r[2] == "6" for r in rows)

    inst = decode(env, frames, [P + "SRP_EXAMPLE_STATUS_instance", P + "SRP_EXAMPLE_NAME_instance",
                                P + "SRP_EXAMPLE_SET_MODE_REQ_instance", P + "SRP_EXAMPLE_SET_MODE_RESP_instance"], "inst.pcap")
    assert [r[i] for i, r in enumerate(inst)] == ["3", "3", "3", "3"]

    r = decode(env, frames[:1], [P + "SRP_EXAMPLE_STATUS_seq", P + "SRP_EXAMPLE_STATUS_state",
                                 P + "SRP_EXAMPLE_STATUS_temp", P + "SRP_EXAMPLE_STATUS_counter",
                                 P + "SRP_EXAMPLE_STATUS_info_offset"], "status.pcap")[0]
    assert r[0] == "11" and r[1] == "7" and float(r[2]) == 21.5 and r[3] == "123456" and r[4] == "-300"

    r = decode(env, frames[1:2], [P + "SRP_EXAMPLE_NAME_label", P + "SRP_EXAMPLE_NAME_value"], "name.pcap")[0]
    assert r[0] == "hello" and float(r[1]) == 2.5

    r = decode(env, frames[2:3], [P + "SRP_EXAMPLE_SET_MODE_REQ_seq", P + "SRP_EXAMPLE_SET_MODE_REQ_target_system",
                                  P + "SRP_EXAMPLE_SET_MODE_REQ_target_component", P + "SRP_EXAMPLE_SET_MODE_REQ_mode"],
               "req.pcap")[0]
    assert r == ["13", "2", "3", "9"]

    r = decode(env, frames[3:], [P + "SRP_EXAMPLE_SET_MODE_RESP_seq", P + "SRP_EXAMPLE_SET_MODE_RESP_status",
                                 P + "SRP_EXAMPLE_SET_MODE_RESP_mode"], "resp.pcap")[0]
    assert r == ["13", "2", "9"]


def test_glued_datagram_yields_all_frames_in_order_and_split_datagrams_too(env):
    v, mod, lua, d = env
    frames = [mh.encode(mod, mod.MAVLink_srp_example_status_message(seq=i, instance=3, state=1, temp=0.5, counter=i,
                                                                   info_offset=0)) for i in range(5)]
    rows = decode(env, [b"".join(frames)], [P + "SRP_EXAMPLE_STATUS_counter"], "glued.pcap")
    assert len(rows) == 1 and rows[0][0].split(",") == ["0", "1", "2", "3", "4"]
    rows = decode(env, frames, [P + "SRP_EXAMPLE_STATUS_counter"], "split.pcap")
    assert [r[0] for r in rows] == ["0", "1", "2", "3", "4"]


def test_zero_value_fields_in_truncated_payload(env):
    v, mod, lua, d = env
    m = mod.MAVLink_srp_example_status_message(seq=0, instance=0, state=0, temp=0.0, counter=0, info_offset=0)
    frame = mh.encode(mod, m)
    if v == 2:
        assert frame[1] == 1  # payload obciety do 1 bajtu - dissector musi odtworzyc zera
    r = decode(env, [frame], [P + "SRP_EXAMPLE_STATUS_counter", P + "SRP_EXAMPLE_STATUS_info_offset",
                              P + "SRP_EXAMPLE_STATUS_state"], "zeros.pcap")[0]
    assert r == ["0", "0", "0"]


def test_dissector_does_not_validate_crc_documented_behaviour(env):
    """Dissector WLua nie liczy CRC: ramka ze zlym CRC jest nadal dekodowana. Test dokumentuje to zachowanie, by
    nikt nie uzywal tshark jako dowodu poprawnego CRC (od tego jest pymavlink w mav_helpers_test)."""
    v, mod, lua, d = env
    f = bytearray(mh.encode(mod, messages(mod)[0]))
    f[-1] ^= 0xFF
    good, bad = mh.decode_all(mod, bytes(f))
    assert not [m for m in good if not m.get_type().startswith("UNKNOWN")]  # pymavlink odrzuca
    rows = decode(env, [bytes(f)], [P + "msgid", P + "SRP_EXAMPLE_STATUS_counter"], "badcrc.pcap")
    assert rows == [[str(mh.IDS[v]["status"]), "123456"]]  # tshark dekoduje mimo zlego CRC


def test_signed_v2_decoded(tmp_path):
    mod, lua = mh.load_dialect(2), mh.generate_wlua(2)
    key = mh.make_key()
    mav, sink = mh.make_mav(mod, sysid=9, compid=8, key=key, link_id=3)
    for m in messages(mod):
        mav.send(m)
    frames = split_frames(bytes(sink.buf))
    assert len(frames) == 4 and all(f[2] & 1 for f in frames)
    pcap = th.write_pcap(str(tmp_path / "signed.pcap"), frames)
    rows = th.run_tshark(pcap, lua, [P + "msgid", P + "sysid", P + "SRP_EXAMPLE_STATUS_counter",
                                     P + "SRP_EXAMPLE_NAME_label"])
    assert [r[0] for r in rows] == ["42000", "42002", "42010", "42011"]
    assert all(r[1] == "9" for r in rows)
    assert rows[0][2] == "123456" and rows[1][3] == "hello"


def test_signed_frame_exposes_link_id_timestamp_and_signature(tmp_path):
    mod, lua = mh.load_dialect(2), mh.generate_wlua(2)
    key = mh.make_key()
    ts = 500_000_000_000 + 123_456  # 10 us od 2015-01-01
    f = mh.encode(mod, messages(mod)[0], key=key, link_id=3, timestamp=ts)
    pcap = th.write_pcap(str(tmp_path / "s.pcap"), [f])
    rows = th.run_tshark(pcap, lua, [P + "signature_link", P + "signature_signature", P + "signature_time"])
    link, sig, when = rows[0]
    assert link == "3"
    assert sig.replace(":", "").lower() == f[-6:].hex()
    # czas z dissectora = 2015-01-01 + ts*10us (TZ=UTC ustawiane w harnessie)
    expected = datetime(2015, 1, 1, tzinfo=timezone.utc) + timedelta(microseconds=ts * 10)
    m = re.match(r"(\w+)\s+(\d+), (\d+) (\d+):(\d+):(\d+)", when)
    assert m, when
    got = datetime.strptime(f"{m.group(1)} {m.group(2)} {m.group(3)} {m.group(4)}:{m.group(5)}:{m.group(6)}",
                            "%b %d %Y %H:%M:%S").replace(tzinfo=timezone.utc)
    assert abs((got - expected).total_seconds()) < 2, (when, expected)


def test_unsigned_frame_has_no_signature_fields(env):
    v, mod, lua, d = env
    rows = decode(env, [mh.encode(mod, messages(mod)[0])], [P + "signature_link"], "unsigned.pcap")
    assert rows == [[""]]


def test_non_mavlink_port_not_dissected(env):
    v, mod, lua, d = env
    pcap = th.write_pcap(str(d / "other.pcap"), [mh.encode(mod, messages(mod)[0])], dport=9999)
    rows = th.run_tshark(pcap, lua, [P + "msgid"])
    assert rows == [[""]] or rows == []


def test_harness_reports_missing_tshark(monkeypatch):
    monkeypatch.setattr(th.shutil, "which", lambda name: None)
    assert th.available() is not None


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v", "-p", "no:cacheprovider"]))
