#!/usr/bin/env python3
"""Generator korpusu do fuzzingu parsera MAVLink (T1 -> uzyje T2).

Rodzaje wpisow (kind): valid, signed, truncated, bad_crc, bad_crc_extra, random, glued, mixed_v1_v2,
garbage_prefix, bad_len, split_magic.
Wpis = strumien bajtow + oczekiwanie (manifest.json):
  must_decode: lista {msgid, counter, ver} ktore poprawny parser MUSI zwrocic (w tej kolejnosci),
  max_known:   gorna granica liczby znanych wiadomosci (zlego CRC/crc_extra nie wolno zwrocic).
Ramki skladane recznie (wlasny CRC), by moc zrobic zly crc_extra niezaleznie od pymavlink.
Deterministyczne po `--seed`. Dialekt: fikstura z mav_helpers (msgid status: v2=42000, v1=200).

CLI:  fuzz_corpus.py --out DIR [--seed N] [--count N]   (zapisuje NNNN_<kind>.bin + manifest.json)
Biblioteka: build_corpus(seed, count) -> list[dict(kind, data, must_decode, max_known, ...)]
"""
import argparse
import json
import os
import random
import struct
import sys

from pymavlink.generator.mavcrc import x25crc

import mav_helpers as mh

KINDS = ["valid", "signed", "truncated", "bad_crc", "bad_crc_extra", "random", "glued", "mixed_v1_v2",
         "garbage_prefix", "bad_len", "split_magic", "magic_garbage", "fake_header_before_valid", "signed_bad_sig",
         "signed_truncated_sig", "len255_then_valid", "len255_truncated", "unknown_incompat_flag",
         "signed_flag_without_signature", "v2_zero_tail"]
SIGNED_NOW_BASE = 700000000000  # zegar odbiorcy dla wpisow podpisanych: ts ramki = base + counter*10
# Wpisy sa niezalezne: miedzy nimi wysylamy separator (300 zer, bez magic), ktory domyka stany "czekam na reszte ramki"
# (najdluzsza ramka v2: 10 + 255 + 2 + 13 B). Bez niego niekompletny wpis polykalby nastepny - to wlasciwosc strumienia.
ENTRY_SEPARATOR = bytes(300)
NO_MAGIC = [b for b in range(256) if b not in (0xFD, 0xFE)]


def _crc(body, extra):
    c = x25crc(bytes(body))
    c.accumulate(bytes([extra]))
    return struct.pack("<H", c.crc)


class Gen:
    def __init__(self, rng):
        self.rng = rng
        self.mods = {1: mh.load_dialect(1), 2: mh.load_dialect(2)}
        self.seq = 0

    def payload(self, ver, counter, zero_tail=False):
        mod = self.mods[ver]
        msg = mod.MAVLink_srp_example_status_message(
            seq=counter & 0xFFFF, instance=3, state=0 if zero_tail else self.rng.randrange(1, 256), temp=self.rng.random() * 100,
            counter=counter, info_offset=0 if zero_tail else self.rng.randrange(-30000, 30000))
        msg.pack(mod.MAVLink(None))
        return mod, msg.get_msgId(), bytes(msg.get_payload())

    def frame(self, ver, counter, sysid=1, compid=1, extra_delta=0, crc_flip=False, incompat=0, zero_tail=False):
        mod, msgid, pl = self.payload(ver, counter, zero_tail)
        extra = (mod.mavlink_map[msgid].crc_extra + extra_delta) & 0xFF
        seq = self.seq = (self.seq + 1) & 0xFF
        if ver == 1:
            body = struct.pack("<BBBBB", len(pl), seq, sysid, compid, msgid) + pl
            fr = b"\xFE" + body + _crc(body, extra)
        else:
            pl = pl.rstrip(b"\x00") or b"\x00"
            body = struct.pack("<BBBBBB3s", len(pl), incompat, 0, seq, sysid, compid, msgid.to_bytes(3, "little")) + pl
            fr = b"\xFD" + body + _crc(body, extra)
        if crc_flip:
            fr = fr[:-1] + bytes([fr[-1] ^ 0xFF])
        return fr, dict(msgid=msgid, counter=counter, ver=ver)

    def signed_frame(self, counter, key):
        mod = self.mods[2]
        mav, sink = mh.make_mav(mod, key=key, timestamp=SIGNED_NOW_BASE + counter * 10)  # staly, by korpus byl deterministyczny
        mav.send(mod.MAVLink_srp_example_status_message(seq=counter & 0xFFFF, instance=3, state=1, temp=1.0, counter=counter,
                                                        info_offset=0))
        return bytes(sink.buf), dict(msgid=mh.IDS[2]["status"], counter=counter, ver=2)


def build_corpus(seed=0, count=220):
    rng = random.Random(seed)
    g = Gen(rng)
    key = bytes(rng.randrange(256) for _ in range(32))  # klucz losowy per seed, tylko do korpusu
    out, n = [], 0

    def add(kind, data, must=(), max_known=None, **extra):
        d = dict(kind=kind, data=bytes(data), must_decode=list(must),
                 max_known=len(must) if max_known is None else max_known)
        d.update(extra)
        out.append(d)

    while len(out) < count:
        kind = KINDS[len(out) % len(KINDS)]
        n += 1
        ver = rng.choice([1, 2])
        if kind == "valid":
            f, m = g.frame(ver, n)
            add(kind, f, [m])
        elif kind == "signed":
            # parser bez klucza moze zwrocic ramke (max 1); z kluczem musi zwrocic dokladnie ja
            f, m = g.signed_frame(n, key)
            add(kind, f, [], max_known=1, signed_key_hex=key.hex(), valid_with_key=m,
                now_ts=SIGNED_NOW_BASE + n * 10)
        elif kind == "truncated":
            f, _ = g.frame(ver, n)
            add(kind, f[:rng.randrange(1, len(f))], [], 0)
        elif kind == "bad_crc":
            f, _ = g.frame(ver, n, crc_flip=True)
            add(kind, f, [], 0)
        elif kind == "bad_crc_extra":
            f, _ = g.frame(ver, n, extra_delta=rng.randrange(1, 256))
            add(kind, f, [], 0)
        elif kind == "random":
            add(kind, bytes(rng.choice(NO_MAGIC) for _ in range(rng.randrange(1, 400))), [], 0)
        elif kind == "glued":
            fs = [g.frame(rng.choice([1, 2]), n * 100 + i) for i in range(rng.randrange(2, 12))]
            add(kind, b"".join(f for f, _ in fs), [m for _, m in fs])
        elif kind == "mixed_v1_v2":
            fs = [g.frame(1, n * 100), g.frame(2, n * 100 + 1), g.frame(1, n * 100 + 2), g.frame(2, n * 100 + 3)]
            add(kind, b"".join(f for f, _ in fs), [m for _, m in fs])
        elif kind == "garbage_prefix":
            junk = bytes(rng.choice(NO_MAGIC) for _ in range(rng.randrange(1, 50)))
            f, m = g.frame(ver, n)
            add(kind, junk + f, [m])
        elif kind == "bad_len":
            f, _ = g.frame(ver, n)
            b = bytearray(f)
            b[1] = rng.choice([255, 0, b[1] + 1, max(0, b[1] - 1)])
            if b[1] == f[1]:
                b[1] = (f[1] + 1) & 0xFF
            add(kind, bytes(b), [], 0)
        elif kind in ("signed_bad_sig", "signed_truncated_sig"):
            f, m = g.signed_frame(n, key)
            if kind == "signed_bad_sig":
                b = bytearray(f)
                b[-1 - rng.randrange(6)] ^= 1 << rng.randrange(8)  # jeden bit podpisu
                data = bytes(b)
            else:
                data = f[:-rng.randrange(1, 7)]  # ucinamy 1..6 B z podpisu (ramka niekompletna)
            # z kluczem: odrzucone. Parser BEZ klucza: zly podpis = ramka poprawna (max 1); ucięty podpis dopelniony
            # separatorem tez bywa zwrocony (CRC nie obejmuje podpisu) - stad optional.
            add(kind, data, [], 1, optional=[m], signed_key_hex=key.hex(), now_ts=SIGNED_NOW_BASE + n * 10,
                note="z kluczem: odrzucone (zly/ucięty podpis)")
        elif kind == "magic_garbage":
            # losowe bajty Z 0xFD/0xFE i wiarygodnym len -> falszywy start ramki; oczekiwanie liczy pymavlink
            buf = bytearray(rng.randrange(256) for _ in range(rng.randrange(20, 400)))
            for _ in range(rng.randrange(2, 8)):
                i = rng.randrange(len(buf))
                buf[i] = rng.choice([0xFD, 0xFE])
                if i + 1 < len(buf):
                    buf[i + 1] = rng.randrange(0, 40)
            add(kind, bytes(buf), [], 0)
        elif kind == "fake_header_before_valid":
            f, m = g.frame(ver, n)
            fake = bytes([rng.choice([0xFD, 0xFE])]) + bytes(rng.randrange(256) for _ in range(rng.randrange(1, 10)))
            # parser, ktory uwierzy w falszywy naglowek, polknie poczatek poprawnej ramki: ramka MOZE zginac
            add(kind, fake + f, [], 1, may_be_lost=True, optional=[m])
        elif kind in ("len255_then_valid", "len255_truncated"):
            pl = bytes(rng.randrange(1, 256) for _ in range(255))
            body = struct.pack("<BBBBBB3s", 255, 0, 0, 1, 1, 1, (0xBEEF).to_bytes(3, "little")) + pl
            fr = b"\xFD" + body + _crc(body, 0)  # nieznany msgid: parser nie zna crc_extra -> nie zwraca znanej
            if kind == "len255_then_valid":
                f2, m2 = g.frame(ver, n)  # po ramce o maks. len parser MUSI sie zsynchronizowac i zdekodowac nastepna
                add(kind, fr + f2, [m2])
            else:
                add(kind, fr[:rng.randrange(20, len(fr) - 1)], [], 0)
        elif kind == "unknown_incompat_flag":
            f, _ = g.frame(2, n, incompat=rng.choice([0x02, 0x04, 0x80, 0x06]))  # CRC przeliczony: poprawna ramka z nieznana flaga (bez bitu podpisu)
            add(kind, f, [], 0, note="MAVLink v2: ramka z nieznanym incompat_flag musi byc odrzucona")
        elif kind == "signed_flag_without_signature":
            # incompat_flags=0x01 (podpis) ale brak 13 B podpisu: ramka ucieta/niespojna -> nie wolno zwrocic wiadomosci
            f, m = g.frame(2, n, incompat=0x01)
            # samodzielnie: ramka niekompletna (brak 13 B) -> nic. Gdy cos dalej w strumieniu "dopelni" podpis, parser bez
            # klucza zwroci wiadomosc (CRC nie obejmuje podpisu) - stad optional; z kluczem podpis nie przejdzie.
            add(kind, f, [], 1, optional=[m], note="flaga podpisu bez podpisu: samodzielnie 0 wiadomosci")
        elif kind == "v2_zero_tail":
            # spec: v2 obcina koncowe zera payloadu (len < pelny rozmiar); parser musi je odtworzyc (kluczowe dla C++)
            f, m = g.frame(2, n, zero_tail=True)
            add(kind, f, [m])
        elif kind == "split_magic":
            f1, m1 = g.frame(ver, n)
            f2, m2 = g.frame(2, n + 1000)
            # po pojedynczym magic parser moze stracic f2 (len=0xFD polyka dane) - wymagana tylko f1, f2 opcjonalna
            add(kind, f1 + f2[:1] + f2, [m1], max_known=2, note="pojedynczy magic przed ramka; f2 opcjonalna", optional=[m2])
    for e in out:  # referencyjny wynik pymavlink (do porownania z parserem C++ w T2)
        e["reference_pymavlink"] = [list(x) for x in known_messages(e)]
    return out


def known_messages(entry):
    """Referencyjne dekodowanie pymavlink: lista (ver, msgid, counter) znanych wiadomosci."""
    res = []
    for ver in (1, 2):
        mav, _ = mh.make_mav(mh.load_dialect(ver))
        for m in mav.parse_buffer(entry["data"]) or []:
            if m.get_type() != "BAD_DATA" and not m.get_type().startswith("UNKNOWN") and hasattr(m, "counter"):
                res.append((ver, m.get_msgId(), m.counter))
    return res


def write_corpus(outdir, seed=0, count=220):
    os.makedirs(outdir, exist_ok=True)
    manifest = []
    for i, e in enumerate(build_corpus(seed, count)):
        name = f"{i:04d}_{e['kind']}.bin"
        with open(os.path.join(outdir, name), "wb") as f:
            f.write(e["data"])
        m = {k: v for k, v in e.items() if k != "data"}
        m["file"] = name
        manifest.append(m)
    with open(os.path.join(outdir, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump({"seed": seed, "entry_separator_hex": ENTRY_SEPARATOR.hex(), "entries": manifest}, f, indent=1)
    return manifest


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--out", required=True)
    p.add_argument("--seed", type=int, default=0)
    p.add_argument("--count", type=int, default=220)
    a = p.parse_args(argv)
    print(f"zapisano {len(write_corpus(a.out, a.seed, a.count))} wpisow do {a.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
