#!/usr/bin/env python3
"""Symulator kanalu radiowego dla testow MAVLink (T1).

Dwie pary pty (strona A i B) polaczone mostem z zakloceniami:
  * utrata (loss_pct)        - % ramek MAVLink (bajty spoza ramek: % pojedynczych bajtow) gubionych,
  * bitflip (bitflip_pct)    - % jednostek (ramek/bajtow) z odwroconym jednym losowym bitem,
  * opoznienie (delay_ms)    - stale opoznienie propagacji,
  * limit przepustowosci     - baud (8N1 = 10 bitow/bajt); 0 = bez limitu,
  * rx_buffer                - bufor odbiorczy [B]: gdy odbiorca nie czyta, nadmiar jest GUBIONY i liczony
                               (stats()[kierunek]["overflow"]) zamiast blokowac nadawce (jak prawdziwe radio/UART),
  * seed                     - decyzje losowe zaleza WYLACZNIE od seedu i kolejnosci jednostek
                               (nie od czasu), wiec ten sam strumien + seed = ten sam wynik.

Uzycie jako biblioteka:
    with ChannelSim(loss_pct=10, baud=57600, seed=1) as sim:
        a = os.open(sim.path_a, os.O_RDWR | os.O_NOCTTY)   # strona A
        ...
        sim.stats()
Uzycie z CLI:  channel_sim.py --loss 10 --flip 0.5 --delay 20 --baud 57600 --seed 1
"""
import argparse
import heapq
import os
import random
import select
import sys
import threading
import time
import tty


def frame_length(buf):
    """Dlugosc ramki MAVLink zaczynajacej sie w buf[0]; None gdy za malo bajtow; 0 gdy to nie magic."""
    if not buf:
        return None
    if buf[0] == 0xFD:
        if len(buf) < 3:
            return None
        return 10 + buf[1] + 2 + (13 if buf[2] & 1 else 0)
    if buf[0] == 0xFE:
        if len(buf) < 2:
            return None
        return 6 + buf[1] + 2
    return 0


class Impairer:
    """Czysta (bez czasu i I/O) logika zaklocen jednego kierunku. Deterministyczna po seedzie."""

    def __init__(self, loss_pct=0.0, bitflip_pct=0.0, seed=0):
        self.loss = loss_pct / 100.0
        self.flip = bitflip_pct / 100.0
        self.rng = random.Random(seed)
        self.buf = bytearray()
        self.st = dict(units=0, dropped=0, flipped=0, bytes_in=0, bytes_out=0, overflow=0)

    def _split(self, final=False):
        units = []
        while self.buf:
            n = frame_length(self.buf)
            if n is None or (n and len(self.buf) < n):
                if final:
                    units.append(bytes(self.buf))
                    self.buf.clear()
                break
            if n == 0:
                n = 1
            units.append(bytes(self.buf[:n]))
            del self.buf[:n]
        return units

    def feed(self, data, final=False):
        """Zwraca liste (dlugosc_przed_zakloceniem, bajty_po_zakloceniu | None gdy utracone)."""
        self.buf += data
        self.st["bytes_in"] += len(data)
        out = []
        for u in self._split(final):
            self.st["units"] += 1
            r1, r2, r3 = self.rng.random(), self.rng.random(), self.rng.random()
            if r1 < self.loss:
                self.st["dropped"] += 1
                out.append((len(u), None))
                continue
            if r2 < self.flip:
                b = bytearray(u)
                bit = int(r3 * len(b) * 8)
                b[bit // 8] ^= 1 << (bit % 8)
                u = bytes(b)
                self.st["flipped"] += 1
            self.st["bytes_out"] += len(u)
            out.append((len(u), u))
        return out


class _Direction(threading.Thread):
    def __init__(self, rfd, wfd, imp, baud, delay_ms, stop, rx_buffer=0):
        super().__init__(daemon=True)
        self.rfd, self.wfd, self.imp = rfd, wfd, imp
        self.rx_buffer = rx_buffer  # B; 0 = bez limitu. Przepelnienie (nieczytajacy odbiorca) = utrata jednostki + licznik
        self.out, self.pending = bytearray(), 0
        self.baud, self.delay = baud, delay_ms / 1000.0
        self.stop_ev = stop
        self.heap, self.seq, self.line_free = [], 0, 0.0
        self.last_rx = time.monotonic()

    def _schedule(self, units, now):
        for n, data in units:
            t = now
            if self.baud:  # czas zajecia lacza liczony tez dla ramek, ktore zostana zgubione
                self.line_free = max(self.line_free, now) + n * 10.0 / self.baud
                t = self.line_free
            if data is not None:
                if self.rx_buffer and self.pending + len(self.out) + len(data) > self.rx_buffer:
                    self.imp.st["overflow"] += 1  # odbiorca nie nadaza: radio/UART gubi, nie blokuje nadawcy
                    continue
                heapq.heappush(self.heap, (t + self.delay, self.seq, data))
                self.pending += len(data)
                self.seq += 1

    def run(self):
        while not self.stop_ev.is_set():
            timeout = 0.005
            if self.out:
                timeout = 0.002
            if self.heap:
                timeout = max(0.0, min(timeout, self.heap[0][0] - time.monotonic()))
            r, _, _ = select.select([self.rfd], [], [], timeout)
            now = time.monotonic()
            if r:
                try:
                    d = os.read(self.rfd, 65536)
                except OSError:
                    d = b""
                if d:
                    self.last_rx = now
                    self._schedule(self.imp.feed(d), now)
            elif self.imp.buf and now - self.last_rx > 0.1:  # urwana ramka: przepusc jako surowe bajty
                self._schedule(self.imp.feed(b"", final=True), now)
            while self.heap and self.heap[0][0] <= now:
                d = heapq.heappop(self.heap)[2]
                self.pending -= len(d)
                self.out += d
            self._flush()

    def _flush(self):
        """Nieblokujacy zapis do pty; reszta zostaje w self.out (odbiorca nie czyta -> bufor, potem overflow)."""
        while self.out:
            try:
                n = os.write(self.wfd, bytes(self.out))
                del self.out[:n]
            except BlockingIOError:
                return
            except OSError:
                self.out.clear()
                return


class ChannelSim:
    def __init__(self, loss_pct=0.0, bitflip_pct=0.0, delay_ms=0.0, baud=0, seed=0, rx_buffer=0):
        self.cfg = dict(loss_pct=loss_pct, bitflip_pct=bitflip_pct, delay_ms=delay_ms, baud=baud, seed=seed,
                        rx_buffer=rx_buffer)
        self._stop = threading.Event()
        self._fds, self._dirs = [], {}
        self.path_a = self.path_b = None

    def start(self):
        ma, sa = os.openpty()
        mb, sb = os.openpty()
        for fd in (sa, sb, ma, mb):
            tty.setraw(fd)  # raw: bez echa i konwersji
        for fd in (ma, mb):
            os.set_blocking(fd, False)
        self._fds = [ma, sa, mb, sb]  # slave'y trzymane otwarte, by master nie dostawal EIO
        self.path_a, self.path_b = os.ttyname(sa), os.ttyname(sb)
        c = self.cfg
        for name, (r, w), seed in (("a2b", (ma, mb), c["seed"]), ("b2a", (mb, ma), c["seed"] + 1)):
            imp = Impairer(c["loss_pct"], c["bitflip_pct"], seed)
            d = _Direction(r, w, imp, c["baud"], c["delay_ms"], self._stop, c["rx_buffer"])
            self._dirs[name] = d
            d.start()
        return self

    def stats(self):
        return {k: dict(d.imp.st) for k, d in self._dirs.items()}

    def stop(self):
        self._stop.set()
        for d in self._dirs.values():
            d.join(timeout=2)
        for fd in self._fds:
            try:
                os.close(fd)
            except OSError:
                pass
        self._fds = []

    def __enter__(self):
        return self.start()

    def __exit__(self, *a):
        self.stop()


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--loss", type=float, default=0.0, help="%% utraconych ramek")
    p.add_argument("--flip", type=float, default=0.0, help="%% ramek z bitflipem")
    p.add_argument("--delay", type=float, default=0.0, help="opoznienie [ms]")
    p.add_argument("--baud", type=int, default=0, help="limit przepustowosci (0 = brak)")
    p.add_argument("--seed", type=int, default=0)
    p.add_argument("--rx-buffer", type=int, default=0, help="bufor odbiorczy [B]; przepelnienie = utrata + licznik (0 = brak)")
    p.add_argument("--duration", type=float, default=0.0, help="sekundy (0 = do Ctrl+C)")
    a = p.parse_args(argv)
    with ChannelSim(a.loss, a.flip, a.delay, a.baud, a.seed, a.rx_buffer) as sim:
        print(f"A={sim.path_a} B={sim.path_b}", flush=True)
        try:
            if a.duration:
                time.sleep(a.duration)
            else:
                threading.Event().wait()
        except KeyboardInterrupt:
            pass
        print(sim.stats())
    return 0


if __name__ == "__main__":
    sys.exit(main())
