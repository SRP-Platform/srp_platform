"""Testy pomocnikow pymavlink i kodowania MAVLink wg spec (v1, v2, v2 podpisane).

Oczekiwania liczone niezaleznie od implementacji (CRC, crc_extra, obcinanie zer, HMAC), tak by
ten sam zestaw mogl pozniej sprawdzic ramki z generatora/runtime (T2).
"""
import hashlib
import struct
import sys

import pytest
from pymavlink.generator.mavcrc import x25crc

import mav_helpers as mh


def status(mod, **kw):
    d = dict(seq=1, instance=3, state=7, temp=21.5, counter=123456, info_offset=-300)
    d.update(kw)
    return mod.MAVLink_srp_example_status_message(**d)


# ---- mavgen z XML w czasie testu ----
@pytest.mark.parametrize("version", [1, 2])
def test_mavgen_accepts_fixture_xml(version):
    mod = mh.load_dialect(version)
    ids = mh.IDS[version]
    for key in ("status", "name", "mode_req", "mode_resp"):
        assert ids[key] in mod.mavlink_map


@pytest.mark.parametrize("version", [1, 2])
def test_message_names_follow_spec(version):
    mod = mh.load_dialect(version)
    names = {mod.mavlink_map[i].msgname for i in mh.IDS[version].values()}
    assert names == {"SRP_EXAMPLE_STATUS", "SRP_EXAMPLE_NAME", "SRP_EXAMPLE_SET_MODE_REQ", "SRP_EXAMPLE_SET_MODE_RESP"}


def test_mavgen_rejects_broken_xml():
    with pytest.raises(Exception):
        mh.load_dialect(2, name="broken", xml_text="<mavlink><messages><message id='1'")


def test_wlua_generated():
    path = mh.generate_wlua(2)
    text = open(path, encoding="utf-8").read()
    assert "SRP_EXAMPLE_STATUS" in text and "SRP_EXAMPLE_SET_MODE_RESP" in text


# ---- kodowanie / dekodowanie ----
@pytest.mark.parametrize("version", [1, 2])
def test_roundtrip_status(version):
    mod = mh.load_dialect(version)
    frame = mh.encode(mod, status(mod), sysid=3, compid=4)
    assert frame[0] == (0xFE if version == 1 else 0xFD)
    good, bad = mh.decode_all(mod, frame)
    assert not bad and len(good) == 1
    m = good[0]
    assert (m.get_srcSystem(), m.get_srcComponent()) == (3, 4)
    assert (m.state, m.counter, m.info_offset) == (7, 123456, -300)
    assert m.temp == pytest.approx(21.5)


@pytest.mark.parametrize("version", [1, 2])
def test_roundtrip_string_and_double(version):
    mod = mh.load_dialect(version)
    msg = mod.MAVLink_srp_example_name_message(seq=1, instance=3, label=b"hello", value=3.141592653589793)
    good, bad = mh.decode_all(mod, mh.encode(mod, msg))
    assert not bad
    assert good[0].label == "hello"
    assert good[0].value == 3.141592653589793


def test_string_longer_than_max_len_is_cut_to_16():
    mod = mh.load_dialect(2)
    msg = mod.MAVLink_srp_example_name_message(seq=1, instance=3, label=b"0123456789ABCDEFXYZ", value=1.0)
    good, _ = mh.decode_all(mod, mh.encode(mod, msg))
    assert good[0].label == "0123456789ABCDEF"


@pytest.mark.parametrize("version", [1, 2])
def test_method_header_fields_roundtrip(version):
    mod = mh.load_dialect(version)
    req = mod.MAVLink_srp_example_set_mode_req_message(seq=65535, instance=3, target_system=0, target_component=0, mode=9)
    resp = mod.MAVLink_srp_example_set_mode_resp_message(seq=65535, instance=3, target_system=1, target_component=1, status=2, mode=9)
    good, bad = mh.decode_all(mod, mh.encode(mod, req) + mh.encode(mod, resp))
    assert not bad and [m.get_type() for m in good] == ["SRP_EXAMPLE_SET_MODE_REQ", "SRP_EXAMPLE_SET_MODE_RESP"]
    assert good[0].seq == 65535 and good[1].status == 2


def _check_crc(frame, crc_extra, v2):
    hdr = 10 if v2 else 6
    n = frame[1]
    body = frame[1:hdr + n]
    crc = x25crc(bytes(body))
    crc.accumulate(bytes([crc_extra]))
    assert struct.unpack("<H", frame[hdr + n:hdr + n + 2])[0] == crc.crc


@pytest.mark.parametrize("version", [1, 2])
def test_crc_uses_crc_extra(version):
    mod = mh.load_dialect(version)
    for key, factory in (("status", lambda: status(mod)),):
        frame = mh.encode(mod, factory())
        crc_extra = mod.mavlink_map[mh.IDS[version][key]].crc_extra
        _check_crc(frame, crc_extra, version == 2)


def test_v2_trailing_zero_truncation():
    mod = mh.load_dialect(2)
    zero = mh.encode(mod, status(mod, seq=0, instance=0, state=0, temp=0.0, counter=0, info_offset=0))
    assert zero[1] == 1  # obciecie zer w v2 zostawia minimum 1 bajt
    assert len(zero) == 10 + 1 + 2
    # niezerowy bajt 'status' (ostatnie pole wg rozmiaru) blokuje obcinanie
    full = mh.encode(mod, status(mod, state=1))
    assert full[1] == 4 + 4 + 2 + 2 + 2 + 1
    good, _ = mh.decode_all(mod, zero)
    assert good[0].counter == 0 and good[0].info_offset == 0


def test_v1_payload_not_truncated():
    mod = mh.load_dialect(1)
    zero = mh.encode(mod, status(mod, seq=0, instance=0, state=0, temp=0.0, counter=0, info_offset=0))
    assert zero[1] == 15 and len(zero) == 6 + 15 + 2


def test_field_order_by_size_on_wire():
    mod = mh.load_dialect(2)
    frame = mh.encode(mod, status(mod, seq=0x0708, instance=0x090A, state=0xAB, temp=1.0, counter=0x01020304, info_offset=0x0506))
    payload = frame[10:10 + frame[1]]
    # kolejnosc mavgen: 4-bajtowe wg deklaracji (temp, counter), potem 2-bajtowe, potem 1-bajtowe
    assert struct.unpack("<f", payload[0:4])[0] == 1.0
    assert struct.unpack("<I", payload[4:8])[0] == 0x01020304
    assert struct.unpack("<H", payload[8:10])[0] == 0x0708
    assert struct.unpack("<H", payload[10:12])[0] == 0x090A
    assert struct.unpack("<h", payload[12:14])[0] == 0x0506
    assert payload[14] == 0xAB


def test_sequence_increments_and_wraps():
    mod = mh.load_dialect(2)
    mav, sink = mh.make_mav(mod)
    seqs = []
    for _ in range(300):
        sink.buf.clear()
        mav.send(status(mod))
        seqs.append(sink.buf[4])
    assert seqs[:3] == [0, 1, 2] and seqs[255] == 255 and seqs[256] == 0


def test_concatenated_frames_all_decoded():
    mod = mh.load_dialect(2)
    data = b"".join(mh.encode(mod, status(mod, counter=i)) for i in range(20))
    good, bad = mh.decode_all(mod, data)
    assert [m.counter for m in good] == list(range(20)) and not bad


def test_garbage_prefix_is_skipped():
    mod = mh.load_dialect(2)
    good, _ = mh.decode_all(mod, b"\x00\x11\x22\x33" + mh.encode(mod, status(mod)))
    assert len(good) == 1


# ---- signing (v2) ----
def test_signed_frame_layout_and_hmac():
    mod = mh.load_dialect(2)
    key = mh.make_key()
    frame = mh.encode(mod, status(mod), key=key, link_id=5)
    assert frame[2] & 1  # incompat_flags: signed
    assert len(frame) == 10 + frame[1] + 2 + 13
    assert frame[-13] == 5  # link_id
    assert frame[-6:] == mh.hmac48(key, frame[:-6])
    ts = int.from_bytes(frame[-12:-6], "little")
    assert ts > 0


def test_signed_roundtrip_accepted():
    mod = mh.load_dialect(2)
    key = mh.make_key()
    mav, sink = mh.make_mav(mod, key=key)
    for i in range(5):
        mav.send(status(mod, counter=i))
    good, bad = mh.decode_all(mod, bytes(sink.buf), key=key)
    assert [m.counter for m in good] == list(range(5)) and not bad


def test_wrong_key_rejected():
    mod = mh.load_dialect(2)
    frame = mh.encode(mod, status(mod), key=mh.make_key())
    good, bad = mh.decode_all(mod, frame, key=mh.make_key())
    assert not good and bad


def test_tampered_payload_rejected():
    mod = mh.load_dialect(2)
    key = mh.make_key()
    f = bytearray(mh.encode(mod, status(mod), key=key))
    f[11] ^= 0x01
    good, bad = mh.decode_all(mod, bytes(f), key=key)
    assert not good


def test_replay_same_frame_rejected():
    mod = mh.load_dialect(2)
    key = mh.make_key()
    frame = mh.encode(mod, status(mod), key=key)
    good, bad = mh.decode_all(mod, frame + frame, key=key)
    assert len(good) == 1 and len(bad) == 1


def test_older_timestamp_rejected_after_newer():
    mod = mh.load_dialect(2)
    key = mh.make_key()
    t = mh.signing_timestamp_now()
    newer = mh.encode(mod, status(mod, counter=2), key=key, timestamp=t + 1000)
    older = mh.encode(mod, status(mod, counter=1), key=key, timestamp=t)
    good, bad = mh.decode_all(mod, newer + older, key=key)
    assert [m.counter for m in good] == [2] and len(bad) == 1


def test_unsigned_rejected_when_signing_required():
    mod = mh.load_dialect(2)
    key = mh.make_key()
    frame = mh.encode(mod, status(mod))
    good, bad = mh.decode_all(mod, frame, key=key, accept_unsigned=False)
    assert not good and bad


def test_unsigned_accepted_when_allowed():
    mod = mh.load_dialect(2)
    frame = mh.encode(mod, status(mod))
    good, bad = mh.decode_all(mod, frame, key=mh.make_key(), accept_unsigned=True)
    assert len(good) == 1


def test_streams_are_independent_by_link_id():
    mod = mh.load_dialect(2)
    key = mh.make_key()
    t = mh.signing_timestamp_now()
    a = mh.encode(mod, status(mod, counter=1), key=key, link_id=0, timestamp=t + 500)
    b = mh.encode(mod, status(mod, counter=2), key=key, link_id=1, timestamp=t)  # starszy, ale inny link
    good, bad = mh.decode_all(mod, a + b, key=key)
    assert [m.counter for m in good] == [1, 2] and not bad


def test_signature_is_sha256_prefix_of_key_plus_frame():
    mod = mh.load_dialect(2)
    key = mh.make_key()
    f = mh.encode(mod, status(mod), key=key)
    assert f[-6:] == hashlib.sha256(key + f[:-6]).digest()[:6]


def test_timestamps_strictly_increase_for_consecutive_frames():
    mod = mh.load_dialect(2)
    key = mh.make_key()
    mav, sink = mh.make_mav(mod, key=key)
    stamps = []
    for _ in range(10):
        sink.buf.clear()
        mav.send(status(mod))
        stamps.append(int.from_bytes(sink.buf[-12:-6], "little"))
    assert stamps == sorted(set(stamps))


def test_new_stream_older_than_60s_is_rejected_newer_accepted():
    """Regula pymavlink dla NOWEGO strumienia (sysid, compid, link_id): timestamp starszy o > 60 s od zegara
    odbiorcy -> odrzucony; do 60 s wstecz -> akceptowany (spec rev.4 #2 / AC restartu nadawcy)."""
    mod = mh.load_dialect(2)
    key = mh.make_key()
    now = 700_000_000_000
    old = mh.encode(mod, status(mod), key=key, timestamp=now - mh.SIXTY_S - 1)
    ok = mh.encode(mod, status(mod), key=key, timestamp=now - mh.SIXTY_S + 10)
    assert not mh.decode_all(mod, old, key=key, now_ts=now)[0]
    assert len(mh.decode_all(mod, ok, key=key, now_ts=now)[0]) == 1


def test_pymavlink_does_not_track_stream_timestamp_after_first_frame_documented_quirk():
    """UDOKUMENTOWANE zachowanie pymavlink 2.4.43: znacznik strumienia ustawia tylko pierwsza ramka, wiec ramka
    starsza niz ostatnia (ale nowsza niz pierwsza) jest akceptowana - replay NIE jest wykrywany. Oracle replayu to
    StrictVerifier (spec); parser C++ ma sie zgadzac ze StrictVerifier, nie z pymavlink."""
    mod = mh.load_dialect(2)
    key = mh.make_key()
    t = 700_000_000_000
    frames = [mh.encode(mod, status(mod, counter=1), key=key, timestamp=t),
              mh.encode(mod, status(mod, counter=2), key=key, timestamp=t + 10 * mh.SIXTY_S),
              mh.encode(mod, status(mod, counter=3), key=key, timestamp=t + 5)]  # replay wzgledem ramki 2
    good, bad = mh.decode_all(mod, b"".join(frames), key=key, now_ts=t)
    assert [m.counter for m in good] == [1, 2, 3]  # pymavlink: przepuszcza
    sv = mh.StrictVerifier(key, now_ts=t)
    assert [sv.check(f) for f in frames] == ["ok", "ok", "replay"]  # spec: odrzuca


def test_strict_verifier_rules():
    mod = mh.load_dialect(2)
    key, other = mh.make_key(), mh.make_key()
    t = 700_000_000_000
    mk = lambda ts, k=key, link=0, sysid=1: mh.encode(mod, status(mod), sysid=sysid, key=k, link_id=link, timestamp=ts)
    sv = mh.StrictVerifier(key, now_ts=t)
    assert sv.check(mk(t)) == "ok"
    assert sv.check(mk(t)) == "replay"  # ten sam timestamp
    assert sv.check(mk(t + 1)) == "ok"
    assert sv.check(mk(t + 2, k=other)) == "bad_sig"
    assert sv.check(mk(t + 3, link=1)) == "ok"  # inny link_id = inny strumien
    assert sv.check(mk(t - mh.SIXTY_S - 1, sysid=2)) == "too_old"  # nowy strumien > 60 s wstecz
    assert sv.check(mk(t - mh.SIXTY_S + 5, sysid=3)) == "ok"
    assert sv.check(mh.encode(mod, status(mod))) == "unsigned"
    assert mh.StrictVerifier(key, t, accept_unsigned=True).check(mh.encode(mod, status(mod))) == "ok"


def test_sender_restart_with_lower_timestamp_is_rejected_by_known_stream():
    """Odbiorca zna strumien: nadawca po restarcie ze starym zegarem (timestamp < ostatniego) jest odrzucany -
    dlatego timestamp nadawcy musi startowac od persisted+60 s (spec rev.5 R1)."""
    mod = mh.load_dialect(2)
    key = mh.make_key()
    t = 700_000_000_000
    before = mh.encode(mod, status(mod, counter=1), key=key, timestamp=t + 1000)
    after_restart = mh.encode(mod, status(mod, counter=2), key=key, timestamp=t + 10)
    good, bad = mh.decode_all(mod, before + after_restart, key=key, now_ts=t)
    assert [m.counter for m in good] == [1] and len(bad) == 1


def test_split_v2_frames_handles_signed_unsigned_and_truncated():
    """H5: split_v2_frames musi uwzglednic +13 B podpisu (incompat 0x01) i dzielic mieszany strumien poprawnie."""
    mod = mh.load_dialect(2)
    key = mh.make_key()
    signed = [mh.encode(mod, status(mod, counter=i), key=key) for i in range(3)]
    plain = [mh.encode(mod, status(mod, counter=10 + i)) for i in range(3)]
    assert all(f[2] & 1 for f in signed) and not any(f[2] & 1 for f in plain)
    stream = signed[0] + plain[0] + signed[1] + plain[1] + plain[2] + signed[2]
    assert mh.split_v2_frames(stream) == [signed[0], plain[0], signed[1], plain[1], plain[2], signed[2]]
    cut = signed[0] + plain[0] + signed[1][:-5]  # ostatnia ramka obcieta (brak 5 B podpisu)
    parts = mh.split_v2_frames(cut)
    assert parts[:2] == [signed[0], plain[0]] and parts[2] == signed[1][:-5]  # reszta zwrocona jako niepelna


def test_make_key_is_32_random_bytes():
    a, b = mh.make_key(), mh.make_key()
    assert len(a) == 32 and a != b


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v", "-p", "no:cacheprovider"]))


