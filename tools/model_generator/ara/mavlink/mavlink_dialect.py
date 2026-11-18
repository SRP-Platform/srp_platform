"""Generator dialektu MAVLink (XML) z modelu usługi (service.json).

Każdy event/metoda z blokiem `mavlink` staje się wiadomością dialektu:
  event  -> SRP_<SERVICE>_<EVENT>
  metoda -> SRP_<SERVICE>_<METHOD>_REQ (+ _RESP gdy metoda ma `out`)
Wszystkie błędy walidacji to MavlinkModelError z nazwą usługi/elementu.
"""
from __future__ import annotations

import keyword
import math
import re
from typing import TYPE_CHECKING
from xml.sax.saxutils import escape, quoteattr

from tools.model_generator.ara.mavlink.mavlink_errors import MavlinkModelError

if TYPE_CHECKING:  # tylko typy: unikamy cyklu zależności bazela (someip_lib -> mavlink)
    from tools.model_generator.ara.someip.lib.service import Service

# ID zajęte przez minimal.xml z pymavlink 2.4.43 (tylko HEARTBEAT).
RESERVED_IDS = {0: "HEARTBEAT"}
V1_MAX_ID = 255
MAX_PAYLOAD = 255
MAX_MSG_NAME = 50
# Nagłówek zawsze w stałym układzie: eventy seq+instance, metody seq+instance+target_*.
EVENT_HEADER = [("seq", "uint16_t", 2), ("instance", "uint16_t", 2)]
METHOD_HEADER = EVENT_HEADER + [("target_system", "uint8_t", 1),
                                ("target_component", "uint8_t", 1)]
RESP_STATUS = ("status", "uint8_t", 1)

# typ C++ z generatora -> (typ MAVLink, rozmiar)
_TYPES = {
    "std::uint8_t": ("uint8_t", 1), "std::uint16_t": ("uint16_t", 2),
    "std::uint32_t": ("uint32_t", 4), "std::uint64_t": ("uint64_t", 8),
    "std::int8_t": ("int8_t", 1), "std::int16_t": ("int16_t", 2),
    "std::int32_t": ("int32_t", 4), "std::int64_t": ("int64_t", 8),
    "float": ("float", 4), "double": ("double", 8), "bool": ("uint8_t", 1),
}


# Pola nagłówka + `status`: payload nie może ich redefiniować (rev. 4, pkt 14).
RESERVED_FIELD_NAMES = {"seq", "instance", "target_system", "target_component", "status"}
_C_KEYWORDS = {
    "auto", "break", "case", "char", "const", "continue", "default", "do", "double", "else",
    "enum", "extern", "float", "for", "goto", "if", "inline", "int", "long", "register",
    "restrict", "return", "short", "signed", "sizeof", "static", "struct", "switch",
    "typedef", "union", "unsigned", "void", "volatile", "while", "bool", "true", "false"}
_LUA_KEYWORDS = {
    "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto", "if",
    "in", "local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while"}
# Składowe klasy MAVLink_message z pymavlink (v10/v20): pole o takiej nazwie
# psuje wygenerowany moduł Pythona (TypeError/SyntaxError). Zgodność z pymavlink
# pilnuje test_python_reserved_names. `id`, `fieldnames` i `msgname` są poza
# listą: sprawdzone w praktyce (pack/decode działają, interop pymavlink OK).
_PY_MESSAGE_MEMBERS = {
    "self", "force_mavlink1", "pack", "array_lengths", "crc_extra",
    "fielddisplays_by_name", "fieldenums_by_name", "fieldtypes",
    "fieldunits_by_name", "format_attr", "get_crc", "get_fieldnames", "get_header",
    "get_link_id", "get_msgId", "get_msgbuf", "get_payload", "get_seq", "get_signed",
    "get_srcComponent", "get_srcSystem", "get_type", "instance_field",
    "instance_offset", "lengths", "native_format", "ordered_fieldnames", "orders",
    "sign_packet", "to_dict", "to_json", "unpacker", "get_timestamp", "get_instance_field",
    "mav", "msgbuf", "payload_len",
}
RESERVED_WORDS = (_C_KEYWORDS | _LUA_KEYWORDS | set(keyword.kwlist) | _PY_MESSAGE_MEMBERS)
_ELEMENT_NAME_RE = re.compile(r"^[A-Za-z][A-Za-z0-9_]*$")
_FIELD_NAME_RE = re.compile(r"^[a-z][a-z0-9_]*$")

EVENT_BLOCK_KEYS = ("msg_id", "max_rate_hz", "max_len")
METHOD_BLOCK_KEYS = ("req_id", "resp_id", "timeout_ms", "max_len")


def _distance(a: str, b: str) -> int:
    prev = list(range(len(b) + 1))
    for i, ca in enumerate(a, 1):
        cur = [i]
        for j, cb in enumerate(b, 1):
            cur.append(min(prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (ca != cb)))
        prev = cur
    return prev[-1]


def CheckElementKeys(keys, where: str) -> None:
    """Literówka w kluczu `mavlink` (case-insensitive, odległość <= 2) = błąd (rev. 4, A)."""
    for k in keys:
        if k != "mavlink" and isinstance(k, str) and _distance(k.lower(), "mavlink") <= 2:
            raise MavlinkModelError(
                f"{where}: klucz '{k}' wygląda na literówkę klucza 'mavlink' "
                "(brak bloku = element wyłączony z MAVLinka, więc literówka byłaby cicha)")


def _check_block_keys(block: dict, allowed, where: str) -> None:
    for k in block:
        if k not in allowed:
            raise MavlinkModelError(
                f"{where}: nieznane pole mavlink.{k} (dozwolone: {', '.join(allowed)})")


HEADER_ROLES = {"seq": "kSeq", "instance": "kInstance", "target_system": "kTargetSystem",
                "target_component": "kTargetComponent", "status": "kStatus"}


class Field:
    def __init__(self, name: str, mav_type: str, size: int, array_len: int = 0,
                 desc: str = "", role: str = "", big_endian: bool = False) -> None:
        self.name = name
        self.mav_type = mav_type
        self.size = size  # rozmiar całkowity w bajtach
        self.array_len = array_len
        self.desc = desc
        # kPayload dla pól z modelu, kSeq/kInstance/... dla nagłówka
        self.role = role or HEADER_ROLES.get(name, "kPayload")
        self.big_endian = big_endian  # endian reprezentacji SOME/IP


class Message:
    def __init__(self, msg_id: int, name: str, desc: str, fields: list[Field],
                 where: str) -> None:
        self.msg_id = msg_id
        self.name = name
        self.desc = desc
        self.fields = fields
        self.where = where  # "Usługa.Element" do komunikatów błędów
        self.crc_extra = ComputeCrcExtra(self)

    def payload_size(self) -> int:
        return sum(f.size for f in self.fields)


def _snake(name: str) -> str:
    s = re.sub(r"(.)([A-Z][a-z]+)", r"\1_\2", name)
    s = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", s)
    return re.sub(r"_+", "_", s).lower()


def _upper_snake(name: str) -> str:
    return _snake(name).upper()


def _short(service_name: str) -> str:
    return service_name.split(".")[-1]


def _as_block(block, where: str) -> dict:
    if not isinstance(block, dict):
        raise MavlinkModelError(f"{where}: blok mavlink musi być obiektem")
    return block


def _get_id(block: dict, key: str, where: str, version: int) -> int:
    if key not in block:
        raise MavlinkModelError(f"{where}: brak pola mavlink.{key}")
    v = block[key]
    if isinstance(v, bool) or not isinstance(v, int) or v < 0 or v > 0xFFFFFF:
        raise MavlinkModelError(f"{where}: mavlink.{key}={v!r} nie jest ID 0..16777215")
    if version == 1 and v > V1_MAX_ID:
        raise MavlinkModelError(
            f"{where}: mavlink.{key}={v} > {V1_MAX_ID} (MAVLink v1 dopuszcza ID <= 255)")
    return v


def _max_len(block: dict, field_name: str, where: str) -> int:
    ml = block.get("max_len")
    if isinstance(ml, dict):
        ml = ml.get(field_name)
    if ml is None:
        raise MavlinkModelError(
            f"{where}: pole string '{field_name}' wymaga mavlink.max_len")
    if isinstance(ml, bool) or not isinstance(ml, int) or ml < 1 or ml > MAX_PAYLOAD:
        raise MavlinkModelError(f"{where}: mavlink.max_len={ml!r} poza zakresem 1..255")
    return ml


def _check_element_name(name: str, where: str) -> None:
    """Nazwa elementu trafia do nazw wiadomości XML: tylko [A-Za-z][A-Za-z0-9_]*."""
    if not _ELEMENT_NAME_RE.match(name):
        raise MavlinkModelError(
            f"{where}: nazwa elementu '{name}' niedozwolona w MAVLink "
            "(dozwolone: ASCII [A-Za-z][A-Za-z0-9_]*)")


def _flatten(dt, prefix: str, block: dict, where: str, out: list[Field],
             endian: str = "little") -> None:
    """Spłaszcza typ modelu do pól MAVLink (struct -> a_b).

    `endian`: endian reprezentacji SOME/IP; pola struktury dziedziczą endian
    struktury (konwertery platformy), prymityw najwyższego poziomu = little.
    """
    if dt.typ_str == "struct":
        for vname, vtype in dt.variable_list.items():
            _flatten(vtype, (prefix + "_" if prefix else "") + _snake(vname), block,
                     where, out, dt.endian)
        return
    name = prefix or "value"
    if not _FIELD_NAME_RE.match(name):
        raise MavlinkModelError(
            f"{where}: nazwa pola '{name}' niedozwolona w MAVLink po spłaszczeniu "
            "(dozwolone: ASCII [a-z][a-z0-9_]*)")
    if dt.typ_str == "std::string":
        n = _max_len(block, name, where)
        out.append(Field(name, "char", n, n, role="kPayload", big_endian=False))
        return
    if dt.typ_str not in _TYPES:
        raise MavlinkModelError(f"{where}: nieobsługiwany typ '{dt.typ_str}' w polu '{name}'")
    mt, size = _TYPES[dt.typ_str]
    out.append(Field(name, mt, size, role="kPayload", big_endian=(endian == "big")))


def _is_void(dt) -> bool:
    return dt is None or dt.typ_str == "void"


def _check_fields(fields: list[Field], where: str, header_len: int = 0) -> None:
    seen = set()
    for f in fields[header_len:]:
        if f.name in RESERVED_FIELD_NAMES:
            raise MavlinkModelError(
                f"{where}: pole '{f.name}' koliduje z nagłówkiem wiadomości "
                f"({', '.join(sorted(RESERVED_FIELD_NAMES))})")
        if f.name in RESERVED_WORDS:
            raise MavlinkModelError(
                f"{where}: pole '{f.name}' jest słowem zastrzeżonym C/Lua/Python")
    seen = set()
    for f in fields:
        if f.name in seen:
            raise MavlinkModelError(f"{where}: zdublowane pole '{f.name}' po spłaszczeniu")
        seen.add(f.name)


def _desc(element, fallback: str, suffix: str = "") -> str:
    """Opis wiadomości: `desc` z modelu (jeśli podany), inaczej tekst generowany."""
    text = getattr(element, "desc", "")
    return (text + suffix) if isinstance(text, str) and text else fallback


class ElementSpec:
    """Element usługi wraz z wiadomościami MAVLink (None dla wyłączonych)."""

    def __init__(self, el_id: int, is_event: bool, name: str, enabled: bool) -> None:
        self.id = el_id
        self.is_event = is_event
        self.name = name
        self.enabled = enabled
        self.req: Message | None = None   # event: wiadomość eventu, metoda: REQ
        self.resp: Message | None = None
        self.timeout_ms = 1000
        self.max_rate_hz = 0.0


def _build_event(service: Service, ev, svc: str, version: int) -> ElementSpec:
    spec = ElementSpec(ev.id, True, ev.name, not IsExcluded(ev))
    if not spec.enabled:
        return spec
    where = f"{service.name}.{ev.name}"
    _check_element_name(ev.name, where)
    block = _as_block(ev.mavlink, where)
    _check_block_keys(block, EVENT_BLOCK_KEYS, where)
    mid = _get_id(block, "msg_id", where, version)
    if "max_rate_hz" in block:
        r = block["max_rate_hz"]
        if isinstance(r, bool) or not isinstance(r, (int, float)) or not math.isfinite(r) or r <= 0:
            raise MavlinkModelError(f"{where}: mavlink.max_rate_hz musi być liczbą > 0")
        spec.max_rate_hz = float(r)
    # seq + instance w nagłówku także dla eventów (dedupikacja, wiele instancji)
    fields: list[Field] = [Field(n, t, sz) for n, t, sz in EVENT_HEADER]
    if not _is_void(ev.out_parm):
        _flatten(ev.out_parm, "", block, where, fields)
    _check_fields(fields, where, len(EVENT_HEADER))
    spec.req = Message(mid, f"SRP_{svc}_{_upper_snake(ev.name)}",
                       _desc(ev, f"Event {service.name}.{ev.name}"), fields, where)
    return spec


def _build_method(service: Service, me, svc: str, version: int) -> ElementSpec:
    spec = ElementSpec(me.id, False, me.name, not IsExcluded(me))
    if not spec.enabled:
        return spec
    where = f"{service.name}.{me.name}"
    _check_element_name(me.name, where)
    block = _as_block(me.mavlink, where)
    _check_block_keys(block, METHOD_BLOCK_KEYS, where)
    req_id = _get_id(block, "req_id", where, version)
    has_out = not _is_void(me.out_parm)
    req_fields = [Field(n, t, s) for n, t, s in METHOD_HEADER]
    if not _is_void(me.in_parm):
        _flatten(me.in_parm, "", block, where, req_fields)
    _check_fields(req_fields, where, len(METHOD_HEADER))
    base = f"SRP_{svc}_{_upper_snake(me.name)}"
    spec.req = Message(req_id, base + "_REQ",
                       _desc(me, f"Request metody {service.name}.{me.name}"), req_fields, where)
    if has_out:
        resp_id = _get_id(block, "resp_id", where, version)
        t = block.get("timeout_ms", 1000)
        if isinstance(t, bool) or not isinstance(t, int) or t <= 0:
            raise MavlinkModelError(
                f"{where}: mavlink.timeout_ms musi być liczbą całkowitą > 0")
        spec.timeout_ms = t
        resp_fields = [Field(n, tt, s) for n, tt, s in METHOD_HEADER]
        resp_fields.append(Field(*RESP_STATUS))
        _flatten(me.out_parm, "", block, where, resp_fields)
        _check_fields(resp_fields, where, len(METHOD_HEADER) + 1)
        spec.resp = Message(resp_id, base + "_RESP",
                            _desc(me, f"Odpowiedź metody {service.name}.{me.name}", " (odpowiedź)"),
                            resp_fields, where)
    elif "resp_id" in block:
        raise MavlinkModelError(
            f"{where}: metoda bez 'out' (void) nie może mieć mavlink.resp_id")
    return spec


def BuildElements(service: Service, version: int = 2) -> list[ElementSpec]:
    """Wszystkie elementy usługi (eventy, potem metody); wyłączone bez wiadomości."""
    svc = _upper_snake(_short(service.name))
    seen: dict[int, str] = {}
    for el in list(service.events) + list(service.methods):
        # runtime mapuje elementy po SOME/IP id: kolizja = błędne mapowanie (T2)
        if el.id in seen:
            raise MavlinkModelError(
                f"{service.name}.{el.name}: kolizja SOME/IP id {el.id} z elementem "
                f"{service.name}.{seen[el.id]} (id elementów usługi muszą być unikalne)")
        seen[el.id] = el.name
    out = [_build_event(service, ev, svc, version) for ev in service.events]
    out += [_build_method(service, me, svc, version) for me in service.methods]
    return out


def BuildMessages(service: Service, version: int = 2) -> list[Message]:
    """Wiadomości dla elementów usługi, które mają blok `mavlink`.

    Elementy bez bloku albo z `"mavlink": false` są pomijane (poza MAVLinkiem).
    """
    msgs: list[Message] = []
    for spec in BuildElements(service, version):
        for m in (spec.req, spec.resp):
            if m is not None:
                msgs.append(m)
    return msgs


def _crc_accumulate(crc: int, byte: int) -> int:
    tmp = (byte ^ (crc & 0xFF)) & 0xFF
    tmp = (tmp ^ (tmp << 4)) & 0xFF
    return ((crc >> 8) ^ (tmp << 8) ^ (tmp << 3) ^ (tmp >> 4)) & 0xFFFF


def ComputeCrcExtra(msg) -> int:
    """crc_extra wg algorytmu mavgen: nazwa + pola w kolejności przewodowej."""
    def elem_size(f: Field) -> int:
        return f.size // max(1, f.array_len)

    ordered = sorted(msg.fields, key=lambda f: -elem_size(f))  # stabilnie
    crc = 0xFFFF
    for b in (msg.name + " ").encode("ascii"):
        crc = _crc_accumulate(crc, b)
    for f in ordered:
        for b in (f.mav_type + " ").encode("ascii"):
            crc = _crc_accumulate(crc, b)
        for b in (f.name + " ").encode("ascii"):
            crc = _crc_accumulate(crc, b)
        if f.array_len:
            crc = _crc_accumulate(crc, f.array_len)
    return (crc & 0xFF) ^ (crc >> 8)


def IsExcluded(el) -> bool:
    """Brak bloku `mavlink` albo `"mavlink": false` = element poza MAVLinkiem."""
    return el.mavlink is None or el.mavlink is False


def AllExcluded(service: Service) -> bool:
    els = list(service.events) + list(service.methods)
    return bool(els) and all(IsExcluded(e) for e in els)


def ExcludedElements(service: Service) -> list[str]:
    """Nazwy wyłączonych elementów (`Usługa.Element`), zdarzenia przed metodami."""
    return [f"{service.name}.{e.name}"
            for e in list(service.events) + list(service.methods) if IsExcluded(e)]


def Validate(messages: list[Message], version: int = 2) -> None:
    """Kolizje ID/nazw, rozmiar payloadu i ID dla v1 (wraz z minimal.xml)."""
    by_id: dict[int, Message] = {}
    names: dict[str, Message] = {}
    for m in messages:
        if m.msg_id in RESERVED_IDS:
            raise MavlinkModelError(
                f"{m.where}: ID {m.msg_id} zajęte przez {RESERVED_IDS[m.msg_id]} (minimal.xml)")
        if m.msg_id in by_id:
            raise MavlinkModelError(
                f"{m.where}: kolizja ID {m.msg_id} z {by_id[m.msg_id].where}"
                f" ({by_id[m.msg_id].name})")
        by_id[m.msg_id] = m
        if m.name in names:
            raise MavlinkModelError(f"{m.where}: kolizja nazwy wiadomości {m.name}")
        names[m.name] = m
        if len(m.name) > MAX_MSG_NAME:
            raise MavlinkModelError(
                f"{m.where}: nazwa {m.name} dłuższa niż {MAX_MSG_NAME} znaków")
        if version == 1 and m.msg_id > V1_MAX_ID:
            raise MavlinkModelError(f"{m.where}: ID {m.msg_id} > 255 w MAVLink v1")
        if m.payload_size() > MAX_PAYLOAD:
            raise MavlinkModelError(
                f"{m.where}: payload {m.payload_size()} B > {MAX_PAYLOAD} B ({m.name})")


def _dialect_number(dialect: str) -> int:
    # Stabilny numer dialektu wyliczony z nazwy.
    return (sum(ord(c) * (i + 1) for i, c in enumerate(dialect)) % 200) + 100


def RenderXml(dialect: str, messages: list[Message]) -> str:
    """Treść <dialect>.xml; wiadomości posortowane po ID (deterministycznie)."""
    out = ['<?xml version="1.0"?>',
           "<!-- Wygenerowane automatycznie przez model_generator (mavlink). Nie edytować. -->",
           "<mavlink>", "  <include>minimal.xml</include>",
           f"  <dialect>{_dialect_number(dialect)}</dialect>", "  <messages>"]
    for m in sorted(messages, key=lambda x: x.msg_id):
        out.append(f"    <message id={quoteattr(str(m.msg_id))} name={quoteattr(m.name)}>")
        out.append(f"      <description>{escape(m.desc)}</description>")
        for f in m.fields:
            ty = f"{f.mav_type}[{f.array_len}]" if f.array_len else f.mav_type
            out.append(f"      <field type={quoteattr(ty)} name={quoteattr(f.name)}>"
                       f"{escape(f.desc or f.name)}</field>")
        out.append("    </message>")
    out.append("  </messages>")
    out.append("</mavlink>")
    return "\n".join(out) + "\n"
