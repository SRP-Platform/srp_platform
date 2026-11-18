#!/usr/bin/env python3
"""serial2udp - strumien MAVLink z UART/pty -> UDP (domyslnie 127.0.0.1:14550), JEDNA RAMKA NA DATAGRAM.

Do Wiresharka na zywo: dissector Lua wygenerowany przez `ara_mavlink_dialect` (<dialect>.lua) rejestruje sie na UDP 14550,
a wymaga calej ramki w datagramie. Narzedzie sklada ramki ze strumienia bajtow (v1 0xFE, v2 0xFD, v2 z podpisem +13 B),
pomija smieci i resynchronizuje sie po magic. Bez dialektu CRC nie jest sprawdzane - ramka jest wtedy akceptowana, gdy po niej konczy sie strumien albo zaczyna
kolejny magic (smieci MIEDZY ramkami wymagaja dialektu). Z `--dialect <dialekt.py>` (modul pymavlink z regul
bazela `ara_mavlink_dialect`) kazda ramka jest weryfikowana CRC z crc_extra, a niepoprawne bajty pomijane (resynchronizacja).

Uzycie:
    serial2udp.py /dev/ttyUSB0 --baud 57600 --host 127.0.0.1 --port 14550
    serial2udp.py /dev/ttyUSB0 --dialect srp_radio.py      # z walidacja CRC
    wireshark -k -i lo -f "udp port 14550" -X lua_script:srp_radio.lua
"""
import argparse
import os
import select
import socket
import sys
import termios
import time
import tty

MAGIC_V1, MAGIC_V2 = 0xFE, 0xFD
STALE_S = 0.3  # cisza, po ktorej niedokonczona ramka jest uznana za falszywy start
BAUDS = {1200: termios.B1200, 2400: termios.B2400, 4800: termios.B4800, 9600: termios.B9600, 19200: termios.B19200,
         38400: termios.B38400, 57600: termios.B57600, 115200: termios.B115200, 230400: termios.B230400,
         460800: termios.B460800, 921600: termios.B921600}


def frame_total(buf):
    """Dlugosc ramki zaczynajacej sie w buf[0] (None gdy za malo bajtow na naglowek, 0 gdy to nie magic)."""
    if buf[0] == MAGIC_V2:
        if len(buf) < 3:
            return None
        return 10 + buf[1] + 2 + (13 if buf[2] & 1 else 0)
    if buf[0] == MAGIC_V1:
        if len(buf) < 2:
            return None
        return 6 + buf[1] + 2
    return 0


def load_crc_lookup(dialect_path):
    """crc_extra(msgid) z wygenerowanego modulu pymavlink (<dialekt>.py)."""
    import importlib.util
    spec = importlib.util.spec_from_file_location("srp_dialect", dialect_path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return lambda msgid: getattr(mod.mavlink_map.get(msgid), "crc_extra", None)


def crc_ok(frame, crc_extra):
    from pymavlink.generator.mavcrc import x25crc
    v2 = frame[0] == MAGIC_V2
    hdr = 10 if v2 else 6
    msgid = int.from_bytes(frame[7:10], "little") if v2 else frame[5]
    extra = crc_extra(msgid)
    if extra is None:
        return False
    n = frame[1]
    c = x25crc(bytes(frame[1:hdr + n]))
    c.accumulate(bytes([extra]))
    return int.from_bytes(frame[hdr + n:hdr + n + 2], "little") == c.crc


class Framer:
    """Skladanie ramek ze strumienia; `feed` zwraca liste kompletnych ramek. `crc_extra`: opcjonalna funkcja msgid->crc_extra."""

    def __init__(self, crc_extra=None):
        self.buf = bytearray()
        self.dropped = 0
        self.crc_extra = crc_extra

    def feed(self, data):
        self.buf += data
        out = []
        while self.buf:
            total = frame_total(self.buf)
            if total is None:
                break  # za malo na naglowek
            if total == 0:
                del self.buf[:1]
                self.dropped += 1
                continue
            if len(self.buf) < total:
                break  # niekompletna - czekamy na reszte
            if self.crc_extra is not None:
                if not crc_ok(self.buf[:total], self.crc_extra):
                    del self.buf[:1]  # zly CRC / nieznany msgid: falszywy start - resynchronizacja
                    self.dropped += 1
                    continue
            else:
                nxt = self.buf[total] if len(self.buf) > total else None
                if nxt is not None and nxt not in (MAGIC_V1, MAGIC_V2):
                    del self.buf[:1]  # po ramce nie ma magic: to byl falszywy start - resynchronizacja
                    self.dropped += 1
                    continue
            out.append(bytes(self.buf[:total]))
            del self.buf[:total]
        return out


def _flush_stale(self):
    """Strumien ucichl, a bufor zaczyna sie od niedokonczonej ramki (np. falszywy magic z duzym `len`): porzuca pierwszy
    bajt i skanuje reszte od nowa - odzyskuje ramki, ktore czekaly za falszywym naglowkiem."""
    if not self.buf:
        return []
    del self.buf[:1]
    self.dropped += 1
    return self.feed(b"")


Framer.flush_stale = _flush_stale


def open_port(path, baud):
    fd = os.open(path, os.O_RDONLY | os.O_NOCTTY)
    try:
        tty.setraw(fd)
        if baud:
            a = termios.tcgetattr(fd)
            a[4] = a[5] = BAUDS[baud]
            termios.tcsetattr(fd, termios.TCSANOW, a)
    except termios.error:
        pass  # np. zwykly plik/FIFO - czytamy bez konfiguracji
    return fd


def run(path, baud, host, port, duration=None, stop=None, out=None, crc_extra=None):
    """Czyta `path` i wysyla ramki; zwraca liczbe wyslanych ramek. `stop` - threading.Event (testy)."""
    fd = open_port(path, baud)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    framer, sent, end = Framer(crc_extra), 0, (time.monotonic() + duration) if duration else None
    last_data = time.monotonic()
    try:
        while not (stop and stop.is_set()) and not (end and time.monotonic() > end):
            if not select.select([fd], [], [], 0.05)[0]:
                if framer.buf and time.monotonic() - last_data > STALE_S:
                    for frame in framer.flush_stale():
                        sock.sendto(frame, (host, port))
                        sent += 1
                    last_data = time.monotonic()
                continue
            last_data = time.monotonic()
            try:
                data = os.read(fd, 4096)
            except OSError:
                break
            if not data:
                break
            for frame in framer.feed(data):
                sock.sendto(frame, (host, port))
                sent += 1
                if out:
                    print(f"frame {len(frame)} B", file=out, flush=True)
    finally:
        os.close(fd)
        sock.close()
    return sent


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("device", help="UART lub pty (np. /dev/ttyUSB0)")
    p.add_argument("--baud", type=int, default=57600, choices=sorted(BAUDS))
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=14550)
    p.add_argument("--dialect", help="modul pymavlink <dialekt>.py: walidacja CRC (crc_extra) ramek")
    p.add_argument("--duration", type=float, default=None, help="sekundy (domyslnie do przerwania)")
    a = p.parse_args(argv)
    try:
        n = run(a.device, a.baud, a.host, a.port, a.duration, out=sys.stderr,
                crc_extra=load_crc_lookup(a.dialect) if a.dialect else None)
    except KeyboardInterrupt:
        return 0
    print(f"serial2udp: wyslano {n} ramek", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
