"""Self-test of the signature verifier that the end-to-end tests use as an oracle."""
import sys

import pytest

import mav_helpers as mh

KEY = bytes(range(32))


def signed_frame(ts, link_id=1, sysid=1, compid=1):
    """A hand-built signed MAVLink 2 frame (msg 0, 9 B payload)."""
    head = bytes([0xFD, 9, 1, 0, 0, sysid, compid, 0, 0, 0]) + bytes(9) + b"\x00\x00"  # incompat=1
    body = head + bytes([link_id]) + ts.to_bytes(6, "little")
    return body + mh.hmac48(KEY, body)


def verifier(now=100_000_000):
    return mh.StrictVerifier(KEY, now)


def test_valid_signature_is_accepted_and_unsigned_is_not():
    v = verifier()
    assert v.check(signed_frame(100_000_000)) == "ok"
    unsigned = bytes([0xFD, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0])
    assert v.check(unsigned) == "unsigned"
    assert mh.StrictVerifier(KEY, 0, accept_unsigned=True).check(unsigned) == "ok"


def test_wrong_key_is_bad_signature():
    frame = signed_frame(100_000_000)
    assert mh.StrictVerifier(bytes(32), 100_000_000).check(frame) == "bad_sig"


def test_replay_is_older_or_equal_timestamp_of_the_same_stream():
    v = verifier()
    assert v.check(signed_frame(100_000_100)) == "ok"
    assert v.check(signed_frame(100_000_100)) == "replay"
    assert v.check(signed_frame(100_000_050)) == "replay"
    assert v.check(signed_frame(100_000_200)) == "ok"
    assert v.check(signed_frame(100_000_000, link_id=2)) == "ok"  # another stream


def test_new_stream_older_than_sixty_seconds_is_refused():
    assert verifier().check(signed_frame(100_000_000 - mh.SIXTY_S - 1)) == "too_old"


def test_split_frames_handles_signed_and_unsigned():
    unsigned = bytes([0xFD, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0])
    frames = [signed_frame(5), unsigned, signed_frame(6)]
    assert mh.split_v2_frames(b"".join(frames)) == frames


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v", "-p", "no:cacheprovider"]))
