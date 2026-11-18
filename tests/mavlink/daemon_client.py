"""Klient IPC MavlinkDaemona w Pythonie (niezalezna implementacja protokolu z ara/com/mavlink/daemon_protocol.h).

Sluzy testom E2E jako „aplikacja": rejestruje instancje, wysyla Event/Request/Response i zbiera komunikaty daemona.
Uklad komunikatow: pierwszy bajt = typ, little-endian; string = u8 dlugosc + bajty; payload = u8 dlugosc + bajty.
"""
import queue
import socket
import struct
import threading
import time

PROTOCOL_VERSION = 1
NO_OFFSET = 0xFF

REGISTER, REGISTER_REPLY, UNREGISTER, EVENT, REQUEST, RESPONSE, SEND_ERROR, EVENT_IN, REQUEST_IN, RESPONSE_IN, \
    LINK_STATUS = range(1, 12)

ROLE_PROVIDE, ROLE_REQUIRE = 0, 1

# RegisterStatus
OK, BAD_VERSION, INVALID, UNKNOWN_LINK, CONFLICT_INSTANCE, CONFLICT_MSG_ID, DUPLICATE_HANDLE = range(7)
# SendErrc
ERR_UNKNOWN_ELEMENT, ERR_UNAVAILABLE, ERR_NO_LINK, ERR_QUEUE_FULL = 1, 2, 3, 4


def _str(s):
    b = s.encode("utf-8")
    return struct.pack("<B", len(b)) + b


def _bytes(b):
    return struct.pack("<B", len(b)) + bytes(b)


class Desc:
    """Opis wiadomosci MAVLink: msg_id, crc_extra, pelna dlugosc payloadu i offsety pol naglowka."""

    def __init__(self, msg_id, crc_extra, full_len, name, seq=0, instance=2, tsys=NO_OFFSET, tcomp=NO_OFFSET,
                 status=NO_OFFSET):
        self.msg_id, self.crc_extra, self.full_len, self.name = msg_id, crc_extra, full_len, name
        self.offsets = (seq, instance, tsys, tcomp, status)

    def pack(self):
        return struct.pack("<IBB5B", self.msg_id, self.crc_extra, self.full_len, *self.offsets) + _str(self.name)


class Element:
    def __init__(self, element_id, req, resp=None, is_event=False, available=True, timeout_ms=1000,
                 min_interval_ms=0):
        self.id, self.req, self.resp = element_id, req, resp
        self.is_event, self.available, self.timeout_ms, self.min_interval_ms = is_event, available, timeout_ms, \
            min_interval_ms

    def pack(self):
        resp = self.resp if self.resp is not None else Desc(0, 0, 1, "")
        return struct.pack("<HBBBII", self.id, self.is_event, self.available, self.resp is not None, self.timeout_ms,
                           self.min_interval_ms) + self.req.pack() + resp.pack()


def register_message(handle, role, com_id, instance, links, elements, target=(0, 0), source=(0, 0), dedup_ttl_ms=10000,
                     version=PROTOCOL_VERSION):
    body = struct.pack("<HIB", version, handle, role) + _str(com_id) + struct.pack(
        "<HBBBBI", instance, target[0], target[1], source[0], source[1], dedup_ttl_ms)
    body += struct.pack("<B", len(links)) + b"".join(_str(link) for link in links)
    body += struct.pack("<B", len(elements)) + b"".join(e.pack() for e in elements)
    return bytes([REGISTER]) + body


class Reader:
    def __init__(self, data):
        self.d, self.p = data, 0

    def take(self, n):
        v = self.d[self.p:self.p + n]
        if len(v) != n:
            raise ValueError("urwany komunikat")
        self.p += n
        return v

    def u8(self):
        return self.take(1)[0]

    def u16(self):
        return struct.unpack("<H", self.take(2))[0]

    def u32(self):
        return struct.unpack("<I", self.take(4))[0]

    def str(self):
        return self.take(self.u8()).decode("utf-8")

    def bytes(self):
        return self.take(self.u8())


class Msg:
    """Komunikat daemona: `type` + pola jako atrybuty."""

    def __init__(self, type_, **kw):
        self.type = type_
        self.__dict__.update(kw)

    def __repr__(self):
        return f"Msg({self.__dict__})"


def parse(packet):
    t, r = packet[0], Reader(packet[1:])
    if t == REGISTER_REPLY:
        m = Msg(t, handle=r.u32(), status=r.u8(), system_id=r.u8(), component_id=r.u8(), text=r.str())
    elif t == SEND_ERROR:
        m = Msg(t, handle=r.u32(), element_id=r.u16(), seq=r.u16(), code=r.u8())
    elif t in (EVENT_IN, REQUEST_IN, RESPONSE_IN):
        m = Msg(t, handle=r.u32(), element_id=r.u16(), sender_system=r.u8(), sender_component=r.u8(), link=r.str(),
                payload=r.bytes())
    elif t == LINK_STATUS:
        m = Msg(t, handle=r.u32(), link=r.str(), ready=bool(r.u8()))
    else:
        raise ValueError(f"nieoczekiwany typ komunikatu {t}")
    if r.p != len(r.d):
        raise ValueError("nadmiarowe bajty")
    return m


class Client:
    """Polaczenie z daemonem + watek zbierajacy komunikaty."""

    def __init__(self, path):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        self.sock.connect(path)
        self.inbox = queue.Queue()
        self.closed = threading.Event()
        self._stop = False
        self._lock = threading.Lock()
        self._backlog = []
        self._thread = threading.Thread(target=self._read, daemon=True)
        self._thread.start()

    def _read(self):
        self.sock.settimeout(0.1)
        while not self._stop:
            try:
                data = self.sock.recv(65536)
            except socket.timeout:
                continue
            except OSError:
                break
            if not data:
                break
            self.inbox.put(parse(data))
        self.closed.set()

    def close(self):
        self._stop = True
        try:
            self.sock.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        self._thread.join(2)
        self.sock.close()

    def send_raw(self, packet):
        self.sock.send(packet)

    def register(self, handle, role, com_id, instance, links, elements, timeout=3.0, **kw):
        self.send_raw(register_message(handle, role, com_id, instance, links, elements, **kw))
        return self.expect(REGISTER_REPLY, lambda m: m.handle == handle, timeout)

    def unregister(self, handle):
        self.send_raw(bytes([UNREGISTER]) + struct.pack("<I", handle))

    def event(self, handle, element_id, payload):
        self.send_raw(bytes([EVENT]) + struct.pack("<IH", handle, element_id) + _bytes(payload))

    def request(self, handle, element_id, payload):
        self.send_raw(bytes([REQUEST]) + struct.pack("<IH", handle, element_id) + _bytes(payload))

    def response(self, handle, element_id, payload):
        self.send_raw(bytes([RESPONSE]) + struct.pack("<IH", handle, element_id) + _bytes(payload))

    def expect(self, type_, pred=lambda m: True, timeout=3.0):
        """Zwraca pierwszy komunikat danego typu spelniajacy `pred` albo None po timeout; reszta zostaje."""
        end = time.monotonic() + timeout
        while True:
            for i, m in enumerate(self._backlog):
                if m.type == type_ and pred(m):
                    del self._backlog[i]
                    return m
            left = end - time.monotonic()
            if left <= 0:
                return None
            try:
                self._backlog.append(self.inbox.get(timeout=min(left, 0.1)))
            except queue.Empty:
                pass

    def wait_link(self, link, ready, timeout=5.0):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            m = self.expect(LINK_STATUS, lambda x: x.link == link, max(0.05, end - time.monotonic()))
            if m is None:
                return False
            if m.ready == ready:
                return True
        return False

    def nothing(self, type_, pred=lambda m: True, timeout=0.3):
        return self.expect(type_, pred, timeout) is None
