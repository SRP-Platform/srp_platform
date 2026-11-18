"""Radio channel simulator for the MAVLink tests.

Two ptys (sides A and B) joined by a bridge that impairs the traffic:
  * loss (loss_pct)     - % of MAVLink frames dropped (outside frames: % of single bytes),
  * bitflip (bitflip_pct) - % of units (frames or bytes) with one random bit inverted,
  * delay (delay_ms)    - constant propagation delay,
  * baud                - throughput limit (8N1 = 10 bits per byte); 0 = unlimited,
  * seed                - the random decisions depend ONLY on the seed and the order of the
                          units (not on time), so the same stream and seed give the same result.

    with ChannelSim(loss_pct=10, baud=57600, seed=1) as sim:
        a = os.open(sim.path_a, os.O_RDWR | os.O_NOCTTY)
        ...
        sim.stats()
"""
import heapq
import os
import random
import select
import threading
import time
import tty


def frame_length(buf):
    """Length of the MAVLink frame starting at buf[0]; None when too few bytes; 0 when not a magic."""
    if not buf:
        return None
    if buf[0] == 0xFD:
        if len(buf) < 3:
            return None
        return 10 + buf[1] + 2 + (13 if buf[2] & 1 else 0)
    return 0


class Impairer:
    """Impairment logic of one direction, free of time and I/O; deterministic for a seed."""

    def __init__(self, loss_pct=0.0, bitflip_pct=0.0, seed=0):
        self.loss = loss_pct / 100.0
        self.flip = bitflip_pct / 100.0
        self.rng = random.Random(seed)
        self.buf = bytearray()
        self.st = dict(units=0, dropped=0, flipped=0, bytes_in=0, bytes_out=0)

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
        """Returns a list of (length before impairment, bytes after impairment | None when lost)."""
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
    def __init__(self, rfd, wfd, imp, baud, delay_ms, stop):
        super().__init__(daemon=True)
        self.rfd, self.wfd, self.imp = rfd, wfd, imp
        self.out = bytearray()
        self.baud, self.delay = baud, delay_ms / 1000.0
        self.stop_ev = stop
        self.heap, self.seq, self.line_free = [], 0, 0.0
        self.last_rx = time.monotonic()

    def _schedule(self, units, now):
        for n, data in units:
            t = now
            if self.baud:  # line time is spent on frames that are then lost as well
                self.line_free = max(self.line_free, now) + n * 10.0 / self.baud
                t = self.line_free
            if data is not None:
                heapq.heappush(self.heap, (t + self.delay, self.seq, data))
                self.seq += 1

    def run(self):
        while not self.stop_ev.is_set():
            timeout = 0.002 if self.out else 0.005
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
            elif self.imp.buf and now - self.last_rx > 0.1:  # truncated frame: pass it on as raw bytes
                self._schedule(self.imp.feed(b"", final=True), now)
            while self.heap and self.heap[0][0] <= now:
                self.out += heapq.heappop(self.heap)[2]
            self._flush()

    def _flush(self):
        """Non-blocking write to the pty; the rest stays in self.out until the reader catches up."""
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
    def __init__(self, loss_pct=0.0, bitflip_pct=0.0, delay_ms=0.0, baud=0, seed=0):
        self.cfg = dict(loss_pct=loss_pct, bitflip_pct=bitflip_pct, delay_ms=delay_ms, baud=baud,
                        seed=seed)
        self._stop = threading.Event()
        self._fds, self._dirs = [], {}
        self.path_a = self.path_b = None

    def release_exclusive(self):
        """Clears TIOCEXCL on the ptys after the process that opened a port exclusively died.

        A real UART loses exclusive mode on the last close; here the simulator keeps its own slave
        descriptors (so the master does not get EIO), and without this a restarted daemon fails
        to reopen the port with EBUSY.
        """
        import fcntl
        import termios
        for fd in (self._fds[1], self._fds[3]):
            try:
                fcntl.ioctl(fd, termios.TIOCNXCL)
            except OSError:
                pass

    def start(self):
        ma, sa = os.openpty()
        mb, sb = os.openpty()
        for fd in (sa, sb, ma, mb):
            tty.setraw(fd)  # raw: no echo, no conversion
        for fd in (ma, mb):
            os.set_blocking(fd, False)
        self._fds = [ma, sa, mb, sb]  # keep the slaves open so the master does not get EIO
        self.path_a, self.path_b = os.ttyname(sa), os.ttyname(sb)
        c = self.cfg
        for name, (r, w), seed in (("a2b", (ma, mb), c["seed"]), ("b2a", (mb, ma), c["seed"] + 1)):
            imp = Impairer(c["loss_pct"], c["bitflip_pct"], seed)
            d = _Direction(r, w, imp, c["baud"], c["delay_ms"], self._stop)
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
