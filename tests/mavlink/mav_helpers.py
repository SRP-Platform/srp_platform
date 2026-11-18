"""pymavlink helpers for the MAVLink tests: frame signing and a reference signature verifier."""
import hashlib
import secrets
import time

SIXTY_S = 6_000_000  # 60 s in units of 10 us


def make_key():
    """A test key (32 B) generated at test time; never stored in the repo."""
    return secrets.token_bytes(32)


class _Sink:
    def __init__(self):
        self.buf = bytearray()

    def write(self, b):
        self.buf += b


def signing_timestamp_now():
    return int((time.time() - 1420070400) * 100000)  # 10 us units since 2015-01-01


def make_mav(mod, sysid=1, compid=1, key=None, link_id=0, timestamp=None):
    """A pymavlink MAVLink object with optional signing; returns (mav, sink)."""
    sink = _Sink()
    mav = mod.MAVLink(sink, srcSystem=sysid, srcComponent=compid)
    mav.robust_parsing = True
    if key is not None:
        mav.signing.secret_key = key
        mav.signing.sign_outgoing = True
        mav.signing.link_id = link_id
        mav.signing.timestamp = timestamp if timestamp is not None else signing_timestamp_now()
    return mav, sink


def hmac48(key, frame_wo_sig):
    return hashlib.sha256(key + frame_wo_sig).digest()[:6]


def split_v2_frames(buf):
    """Splits a stream of valid MAVLink 2 frames (signed or not) into frames."""
    out, i = [], 0
    while i < len(buf):
        n = 10 + buf[i + 1] + 2 + (13 if buf[i + 2] & 1 else 0)
        out.append(bytes(buf[i:i + n]))
        i += n
    return out


class StrictVerifier:
    """Reference signature verifier written from the MAVLink 2 signing spec, not from pymavlink.

    HMAC-SHA256 truncated to 48 bits; a frame is a replay when its timestamp is not above the
    last accepted one of its (link_id, sysid, compid) stream; a new stream is accepted only when
    its timestamp is within 60 s of the receiver clock. pymavlink does not update the stream
    timestamp after the first frame, so it would not catch an older-than-last replay.
    """

    def __init__(self, key, now_ts, accept_unsigned=False):
        self.key, self.now, self.accept_unsigned = key, now_ts, accept_unsigned
        self.last = {}

    def check(self, frame):
        """Returns 'ok' or the reason: unsigned | bad_sig | replay | too_old."""
        if not frame[2] & 1:
            return "ok" if self.accept_unsigned else "unsigned"
        if frame[-6:] != hmac48(self.key, frame[:-6]):
            return "bad_sig"
        stream = (frame[-13], frame[5], frame[6])
        ts = int.from_bytes(frame[-12:-6], "little")
        if stream in self.last:
            if ts <= self.last[stream]:
                return "replay"
        elif ts + SIXTY_S < self.now:
            return "too_old"
        self.last[stream] = ts
        return "ok"
