"""Pomocniki pymavlink dla testow MAVLink (T1). Pisane ze specyfikacji, nie z kodu generatora."""
import hashlib
import importlib.util
import os
import secrets
import shutil
import sys
import tempfile

import pymavlink
from pymavlink.generator import mavgen as _mavgen

MDEF_DIR = os.path.join(os.path.dirname(pymavlink.__file__), "message_definitions", "v1.0")

# Fikstura recznie zgodna ze spec: SRP_<SERVICE>_<EVENT>, metody _REQ/_RESP z naglowkiem seq/target.
# Dla v1 ID musza byc <= 255, dlatego baza ID zalezy od wersji.
IDS = {2: dict(status=42000, name=42002, mode_req=42010, mode_resp=42011),
       1: dict(status=200, name=202, mode_req=210, mode_resp=211)}


def dialect_xml(version=2, name="srp_radio"):
    i = IDS[version]
    return f"""<?xml version="1.0"?>
<mavlink>
  <include>minimal.xml</include>
  <version>3</version>
  <messages>
    <message id="{i['status']}" name="SRP_EXAMPLE_STATUS">
      <description>Event Status uslugi Example</description>
      <field type="uint16_t" name="seq">Numer sekwencji (deduplikacja)</field>
      <field type="uint16_t" name="instance">Instancja uslugi</field>
      <field type="uint8_t" name="state">Status</field>
      <field type="float" name="temp">Temperatura</field>
      <field type="uint32_t" name="counter">Licznik</field>
      <field type="int16_t" name="info_offset">Pole spłaszczone info.offset</field>
    </message>
    <message id="{i['name']}" name="SRP_EXAMPLE_NAME">
      <description>Event z stringiem (max_len 16)</description>
      <field type="uint16_t" name="seq">Numer sekwencji (deduplikacja)</field>
      <field type="uint16_t" name="instance">Instancja uslugi</field>
      <field type="char[16]" name="label">Etykieta</field>
      <field type="double" name="value">Wartosc</field>
    </message>
    <message id="{i['mode_req']}" name="SRP_EXAMPLE_SET_MODE_REQ">
      <description>Metoda SetMode (zadanie)</description>
      <field type="uint16_t" name="seq">Numer sekwencji</field>
      <field type="uint16_t" name="instance">Instancja uslugi</field>
      <field type="uint8_t" name="target_system">System docelowy</field>
      <field type="uint8_t" name="target_component">Komponent docelowy</field>
      <field type="uint8_t" name="mode">Tryb</field>
    </message>
    <message id="{i['mode_resp']}" name="SRP_EXAMPLE_SET_MODE_RESP">
      <description>Metoda SetMode (odpowiedz)</description>
      <field type="uint16_t" name="seq">Numer sekwencji</field>
      <field type="uint16_t" name="instance">Instancja uslugi</field>
      <field type="uint8_t" name="target_system">System docelowy</field>
      <field type="uint8_t" name="target_component">Komponent docelowy</field>
      <field type="uint8_t" name="status">0 OK, 1 blad metody, 2 nieznana metoda</field>
      <field type="uint8_t" name="mode">Tryb</field>
    </message>
  </messages>
</mavlink>
"""


def generate_dialect(xml_text, version, outdir, name="srp_radio", language="Python3"):
    """mavgen w czasie testu. Zwraca sciezke wyjscia (plik .py albo .lua). Rzuca RuntimeError przy odrzuceniu XML."""
    os.makedirs(outdir, exist_ok=True)
    shutil.copy(os.path.join(MDEF_DIR, "minimal.xml"), outdir)
    xml_path = os.path.join(outdir, name + ".xml")
    with open(xml_path, "w", encoding="utf-8") as f:
        f.write(xml_text)
    ext = {"WLua": ".lua"}.get(language, ".py")
    out = os.path.join(outdir, name + ext)
    opts = _mavgen.Opts(out, wire_protocol="1.0" if version == 1 else "2.0",
                        language=language, validate=True)
    if not _mavgen.mavgen(opts, [xml_path]):
        raise RuntimeError("mavgen odrzucil XML")
    return out


_cache = {}


def load_dialect(version=2, name="srp_radio", xml_text=None):
    """Generuje i importuje modul pymavlink dla fikstury (cache per wersja)."""
    key = (version, name, xml_text)
    if key not in _cache:
        d = tempfile.mkdtemp(prefix="mavdialect_", dir=os.environ.get("TEST_TMPDIR"))
        path = generate_dialect(xml_text or dialect_xml(version), version, d, name)
        spec = importlib.util.spec_from_file_location(f"{name}_v{version}_{len(_cache)}", path)
        mod = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = mod
        spec.loader.exec_module(mod)
        _cache[key] = mod
    return _cache[key]


def generate_wlua(version=2, name="srp_radio", xml_text=None):
    """Zwraca sciezke do <name>.lua (mavgen --lang=WLua)."""
    d = tempfile.mkdtemp(prefix="mavlua_", dir=os.environ.get("TEST_TMPDIR"))
    return generate_dialect(xml_text or dialect_xml(version), version, d, name, language="WLua")


def make_key():
    """Klucz testowy (32 B) generowany w czasie testu; nigdy w repo."""
    return secrets.token_bytes(32)


class _Sink:
    def __init__(self):
        self.buf = bytearray()

    def write(self, b):
        self.buf += b


def make_mav(mod, sysid=1, compid=1, key=None, link_id=0, timestamp=None, accept_unsigned=False):
    """Obiekt MAVLink z opcjonalnym podpisem. Zwraca (mav, sink)."""
    sink = _Sink()
    mav = mod.MAVLink(sink, srcSystem=sysid, srcComponent=compid)
    mav.robust_parsing = True
    if key is not None:
        mav.signing.secret_key = key
        mav.signing.sign_outgoing = True
        mav.signing.link_id = link_id
        mav.signing.timestamp = timestamp if timestamp is not None else signing_timestamp_now()
        mav.signing.allow_unsigned_callback = (lambda m, mid: True) if accept_unsigned else None
    return mav, sink


def signing_timestamp_now():
    import time
    return int((time.time() - 1420070400) * 100000)  # 10 us od 2015-01-01


def encode(mod, msg, sysid=1, compid=1, key=None, link_id=0, timestamp=None):
    mav, sink = make_mav(mod, sysid, compid, key, link_id, timestamp)
    mav.send(msg)
    return bytes(sink.buf)


def decode_all(mod, data, key=None, accept_unsigned=False, sysid=255, compid=0, now_ts=None):
    """Dekoduje strumien; zwraca (lista_wiadomosci_poprawnych, lista_bledow)."""
    mav, _ = make_mav(mod, sysid, compid, None)
    mav.robust_parsing = True
    if key is not None:
        mav.signing.secret_key = key
        mav.signing.sign_outgoing = False
        mav.signing.timestamp = now_ts if now_ts is not None else signing_timestamp_now()
        mav.signing.allow_unsigned_callback = (lambda m, mid: True) if accept_unsigned else None
    good, bad = [], []
    for m in mav.parse_buffer(data) or []:
        (bad if m.get_type() == "BAD_DATA" else good).append(m)
    return good, bad


def hmac48(key, frame_wo_sig):
    return hashlib.sha256(key + frame_wo_sig).digest()[:6]



SIXTY_S = 6_000_000  # 60 s w jednostkach 10 us


def split_v2_frames(buf):
    """Dzieli strumien poprawnych ramek v2 (z podpisem lub bez) na ramki."""
    out, i = [], 0
    while i < len(buf):
        n = 10 + buf[i + 1] + 2 + (13 if buf[i + 2] & 1 else 0)
        out.append(bytes(buf[i:i + n]))
        i += n
    return out


class StrictVerifier:
    """Referencyjny weryfikator podpisu wg SPEC (nie wg pymavlink): HMAC-SHA256 48 bit, replay = timestamp <=
    ostatniego zaakceptowanego dla (link_id, sysid, compid), NOWY strumien akceptowany tylko gdy ts + 60 s >= zegar
    odbiorcy. Dziala jako wyrocznia dla parsera C++ (T2). pymavlink 2.4.43 NIE aktualizuje znacznika strumienia po
    pierwszej ramce, wiec nie wykrywa replayu starszej-niz-ostatnia ramki (patrz mav_helpers_test)."""

    def __init__(self, key, now_ts, accept_unsigned=False, floor=None):
        self.key, self.now, self.accept_unsigned = key, now_ts, accept_unsigned
        self.floor = floor  # rev.6: callable -> max(last_peer, last_tx); gdy brak: zegar odbiorcy now_ts
        self.last = {}

    def check(self, frame):
        """Zwraca 'ok' albo powod odrzucenia: unsigned | bad_sig | replay | too_old."""
        if not frame[2] & 1:
            return "ok" if self.accept_unsigned else "unsigned"
        if frame[-6:] != hmac48(self.key, frame[:-6]):
            return "bad_sig"
        link = frame[-13]
        ts = int.from_bytes(frame[-12:-6], "little")
        sk = (link, frame[5], frame[6])
        if sk in self.last:
            if ts <= self.last[sk]:
                return "replay"
        elif ts + SIXTY_S < (self.floor() if self.floor else self.now):
            return "too_old"
        self.last[sk] = ts
        return "ok"
