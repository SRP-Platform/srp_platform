"""MAVLink model: service instances, dialect messages and CPU-wide checks.

Inputs:
  * `app_config.json` entries with `"on": "mavlink"` -> `MavlinkItem` (instance, `elements`: the
    whitelist element -> radios (links) -> rate limit; an element outside it is not on the radio);
  * `service.json` elements with a `mavlink` block -> `Message` objects of the CPU dialect:
      event  -> SRP_<SERVICE>_<EVENT>
      method -> SRP_<SERVICE>_<METHOD>_REQ (+ _RESP when the method has `out`)
  * the MavlinkDaemon config (`DaemonConfig`): only what the generator needs (link names,
    signing mode, `air_rate_bps`, `max_utilization`, `mtu`). The daemon validates the rest when
    it starts.

Every validation failure is a `MavlinkModelError` naming the application, service and element.
This module imports nothing from the SOME/IP or application generators (only `ModelError`).
"""
from __future__ import annotations

import functools
import json
import keyword
import re
import sys

from tools.model_generator.ara.common.model_error import ModelError

MAX_PAYLOAD = 255
FRAME_OVERHEAD = 12  # MAVLink 2 header 10 + CRC 2
SIGNATURE_SIZE = 13
MAX_FRAME_SIZE = FRAME_OVERHEAD + MAX_PAYLOAD + SIGNATURE_SIZE
DEFAULT_MAX_UTILIZATION = 0.8
RESERVED_IDS = {0: "HEARTBEAT"}  # taken by minimal.xml; the daemon sends no periodic frame of its own

NAME_RE = re.compile(r"^[A-Za-z][A-Za-z0-9_]*$")
_FIELD_NAME_RE = re.compile(r"^[a-z][a-z0-9_]*$")

# Keys of an `on: mavlink` entry in app_config.json; any other key is an error (a typo would
# silently change which elements the radio carries).
MAVLINK_ONLY_ITEM_KEYS = ("elements", "source_system_id", "source_component_id",
                          "target_system_id", "target_component_id")
MAVLINK_ITEM_KEYS = ("name", "on", "instance") + MAVLINK_ONLY_ITEM_KEYS
# Keys of the former format: an error with a hint (nothing is carried by default any more).
REMOVED_ITEM_KEYS = {
    "links": "list the radios per element: \"elements\": {\"<Element>\": {\"<link>\": {}}}",
    "exclude": "an element outside \"elements\" is not carried over MAVLink",
    "max_rate_hz": "the limit is per element and radio: "
                   "\"elements\": {\"<Event>\": {\"<link>\": {\"max_rate_hz\": N}}}",
}
EVENT_BLOCK_KEYS = ("msg_id", "max_len")
METHOD_BLOCK_KEYS = ("req_id", "resp_id", "max_len")
# The method timeout is a property of the radio, not of the service: `method_timeout_ms` of the
# link in the daemon config (a method on several radios waits for the longest one).
REMOVED_METHOD_BLOCK_KEYS = {
    "timeout_ms": "the method timeout is per radio now: set \"method_timeout_ms\" (default 1000) on the link in the daemon config; a method on several radios waits for the longest",
}
LINK_OPTION_KEYS = ("max_rate_hz",)

# Header fields are fixed: events carry seq+instance, methods add target_*, RESP adds status.
EVENT_HEADER = [("seq", "uint16_t", 2), ("instance", "uint16_t", 2)]
METHOD_HEADER = EVENT_HEADER + [("target_system", "uint8_t", 1),
                                ("target_component", "uint8_t", 1)]
RESP_STATUS = ("status", "uint8_t", 1)
HEADER_ROLES = {"seq": "kSeq", "instance": "kInstance", "target_system": "kTargetSystem",
                "target_component": "kTargetComponent", "status": "kStatus"}
HEADER_FIELD_NAMES = set(HEADER_ROLES)

# generator C++ type -> (MAVLink type, size)
_TYPES = {
    "std::uint8_t": ("uint8_t", 1), "std::uint16_t": ("uint16_t", 2),
    "std::uint32_t": ("uint32_t", 4), "std::uint64_t": ("uint64_t", 8),
    "std::int8_t": ("int8_t", 1), "std::int16_t": ("int16_t", 2),
    "std::int32_t": ("int32_t", 4), "std::int64_t": ("int64_t", 8),
    "float": ("float", 4), "double": ("double", 8), "bool": ("uint8_t", 1),
}


class MavlinkModelError(ModelError):
    """Model error; the message names the application, service and element."""


@functools.cache
def PythonReservedWords() -> frozenset[str]:
    """Field names that break the generated pymavlink module (a TypeError or SyntaxError)."""
    from pymavlink.dialects.v20.minimal import MAVLink_message
    return frozenset(keyword.kwlist) | frozenset(dir(MAVLink_message))


def _int(v, lo: int, hi: int, where: str, what: str) -> int:
    if isinstance(v, bool) or not isinstance(v, int) or v < lo or v > hi:
        raise MavlinkModelError(f"{where}: {what}={v!r} is outside {lo}..{hi}")
    return v


def _snake(name: str) -> str:
    s = re.sub(r"(.)([A-Z][a-z]+)", r"\1_\2", name)
    s = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", s)
    return re.sub(r"_+", "_", s).lower()


# ---------------------------------------------------------------- daemon config view

class DaemonLink:
    def __init__(self, signing_enabled: bool, accept_unsigned: bool, air_rate_bps: int = 0,
                 max_utilization: float = DEFAULT_MAX_UTILIZATION, mtu: int = 0) -> None:
        self.signing_enabled = signing_enabled
        self.accept_unsigned = accept_unsigned
        self.air_rate_bps = air_rate_bps  # 0 = no budget
        self.max_utilization = max_utilization
        self.mtu = mtu  # 0 = no limit


class DaemonConfig:
    """`system_id` / `component_id` are None when the config has none (the daemon refuses such a
    config at start; the single-CPU generator does not need them, the system generator does)."""

    def __init__(self, links: dict[str, DaemonLink], system_id: int | None = None,
                 component_id: int | None = None) -> None:
        self.links = links
        self.system_id = system_id
        self.component_id = component_id


def _link_number(link: dict, key: str, lo, hi, name: str, integer: bool):
    v = link[key]
    ok = isinstance(v, int) if integer else isinstance(v, (int, float))
    if isinstance(v, bool) or not ok or not lo <= v <= hi:
        raise ValueError(f"link '{name}': {key}={v!r} must be "
                         f"{'an integer' if integer else 'a number'} in {lo}..{hi}")
    return v


def LoadDaemonConfig(path: str) -> DaemonConfig:
    """Reads system_id, component_id, link names, signing mode and the bandwidth fields from mavlink_daemon_config.json."""
    try:
        with open(path, encoding="utf-8") as f:
            raw = json.load(f)
        links = {}
        for name, link in raw["links"].items():
            signing = link.get("signing", {})
            air = (_link_number(link, "air_rate_bps", 1, 2**32 - 1, name, True)
                   if "air_rate_bps" in link else 0)
            util = (_link_number(link, "max_utilization", 1e-9, 1.0, name, False)
                    if "max_utilization" in link else DEFAULT_MAX_UTILIZATION)
            mtu = (_link_number(link, "mtu", FRAME_OVERHEAD, MAX_FRAME_SIZE, name, True)
                   if "mtu" in link else 0)
            links[name] = DaemonLink(bool(signing.get("enabled", False)),
                                     bool(signing.get("accept_unsigned", False)),
                                     air, float(util), mtu)
    except (OSError, ValueError, KeyError, AttributeError) as e:
        raise MavlinkModelError(f"{path}: cannot read the daemon config ({e!r})")
    ids = []
    for key in ("system_id", "component_id"):
        v = raw.get(key)
        if v is not None and (isinstance(v, bool) or not isinstance(v, int) or not 1 <= v <= 255):
            raise MavlinkModelError(f"{path}: {key}={v!r} must be an integer in 1..255")
        ids.append(v)
    return DaemonConfig(links, *ids)


# ---------------------------------------------------------------- app_config.json instances

class MavlinkItem:
    """A service provided or required on `on: mavlink`.

    `elements` is the whitelist element -> {link: max_rate_hz} (0 = no limit; methods always 0);
    an element outside it does not go over a radio. `links` is the sum of the links of all
    elements, in order of first appearance.
    """

    def __init__(self, elements: dict[str, dict[str, float]], target_system_id: int,
                 target_component_id: int, source_system_id: int, source_component_id: int,
                 warnings: list[str]) -> None:
        self.elements = elements
        self.links = list(dict.fromkeys(ln for per in elements.values() for ln in per))
        self.target_system_id = target_system_id
        self.target_component_id = target_component_id
        self.source_system_id = source_system_id  # require only; 0 = any
        self.source_component_id = source_component_id
        self.warnings = warnings  # printed by the generator, not by the parser


def RejectMavlinkKeys(item: dict, app_name: str) -> None:
    """MAVLink-only keys on an `ipc` or `udp` entry are an error."""
    for key in MAVLINK_ONLY_ITEM_KEYS:
        if key in item:
            raise MavlinkModelError(
                f"{app_name}.{item.get('name', '?')}: '{key}' is only allowed with "
                f"on=mavlink (the entry has on={item.get('on')!r})")


APP_LEVEL_MAVLINK_KEYS = ("mavlink", "mavlink_links")


def RejectAppMavlinkBlocks(app_cfg: dict, app_name: str) -> None:
    """`mavlink` / `mavlink_links` blocks of the application are an error: its app_config.json
    holds only `on: mavlink` entries; links and radios are the MavlinkDaemon's."""
    for key in APP_LEVEL_MAVLINK_KEYS:
        if key in app_cfg:
            raise MavlinkModelError(f"{app_name}: unknown key '{key}' in the application")


def _parse_elements(item: dict, where: str, service, direction: str,
                    warnings: list[str]) -> dict[str, dict[str, float]]:
    """The `elements` whitelist: element -> radio -> {max_rate_hz}."""
    elements = {e.name: e for e in list(service.events) + list(service.methods)}
    events = {e.name for e in service.events}
    raw = item.get("elements")
    if not isinstance(raw, dict) or not raw:
        raise MavlinkModelError(
            f"{where}: on=mavlink needs a non-empty 'elements' object "
            "{\"<Element>\": {\"<link>\": {\"max_rate_hz\": N}}}: nothing goes over a radio "
            "unless it is listed")
    out: dict[str, dict[str, float]] = {}
    for name, per_link in raw.items():
        here = f"{where}: elements.{name}"
        if name not in elements:
            raise MavlinkModelError(
                f"{here}: unknown element of service {service.name} "
                f"(available: {', '.join(elements) or 'none'})")
        if IsExcluded(elements[name]):
            raise MavlinkModelError(
                f"{here}: the element has no 'mavlink' block in service.json of "
                f"{service.name} (msg_id / req_id), it cannot go over a radio")
        if not isinstance(per_link, dict) or not per_link:
            raise MavlinkModelError(
                f"{here}: must be an object with at least one link "
                "{\"<link>\": {\"max_rate_hz\": N}}")
        rates: dict[str, float] = {}
        for ln, opts in per_link.items():
            if not isinstance(ln, str) or not NAME_RE.match(ln):
                raise MavlinkModelError(f"{here}: link name {ln!r} must match "
                                        "[A-Za-z][A-Za-z0-9_]*")
            if not isinstance(opts, dict):
                raise MavlinkModelError(f"{here}.{ln}: must be an object (\"{{}}\" or "
                                        "{\"max_rate_hz\": N})")
            for k in opts:
                if k not in LINK_OPTION_KEYS:
                    raise MavlinkModelError(
                        f"{here}.{ln}: unknown key '{k}' (allowed: {', '.join(LINK_OPTION_KEYS)})")
            rate = 0.0
            if "max_rate_hz" in opts:
                v = opts["max_rate_hz"]
                if name not in events:
                    raise MavlinkModelError(
                        f"{here}.{ln}: max_rate_hz applies to events only, '{name}' is a method")
                if isinstance(v, bool) or not isinstance(v, (int, float)) or v < 0:
                    raise MavlinkModelError(f"{here}.{ln}: max_rate_hz={v!r} must be a number >= 0")
                if direction == "kIn":
                    warnings.append(f"{here}.{ln}: max_rate_hz in a require entry has no effect "
                                    "(the limit applies to the sender)")
                else:
                    rate = float(v)
            rates[ln] = rate
        if name not in events and len(rates) > 1:
            warnings.append(
                f"{here}: method on {len(rates)} radios ({', '.join(rates)}): a REQ sent over "
                f"all of them is executed {len(rates)} times (once per radio)")
        out[name] = rates
    return out


def ParseServiceItem(item: dict, name: str, app_name: str, service,
                     direction: str) -> MavlinkItem:
    """Validates one `on: mavlink` entry; direction is "kOut" (provide) or "kIn" (require)."""
    where = f"{app_name}.{name}"
    for key in item:
        if key in REMOVED_ITEM_KEYS:
            raise MavlinkModelError(
                f"{where}: the key '{key}' was removed from on=mavlink entries "
                f"({REMOVED_ITEM_KEYS[key]})")
        if key not in MAVLINK_ITEM_KEYS:
            raise MavlinkModelError(
                f"{where}: unknown key '{key}' in an on=mavlink entry "
                f"(allowed: {', '.join(MAVLINK_ITEM_KEYS)})")
    if "instance" not in item:
        raise MavlinkModelError(f"{where}: missing 'instance'")
    _int(item["instance"], 0, 65535, where, "instance")
    warnings: list[str] = []
    elements = _parse_elements(item, where, service, direction, warnings)
    src_sys = src_comp = 0
    for key in ("source_system_id", "source_component_id"):
        if key in item and direction != "kIn":
            raise MavlinkModelError(f"{where}: '{key}' applies to require entries only")
    if direction == "kIn":
        src_sys = _int(item.get("source_system_id", 0), 0, 255, where, "source_system_id")
        src_comp = _int(item.get("source_component_id", 0), 0, 255, where, "source_component_id")
    return MavlinkItem(
        elements,
        _int(item.get("target_system_id", 0), 0, 255, where, "target_system_id"),
        _int(item.get("target_component_id", 0), 0, 255, where, "target_component_id"),
        src_sys, src_comp, warnings)


# ---------------------------------------------------------------- messages from service.json

class Field:
    def __init__(self, name: str, mav_type: str, size: int, array_len: int = 0,
                 role: str = "", big_endian: bool = False) -> None:
        self.name = name
        self.mav_type = mav_type
        self.size = size  # total size in bytes
        self.array_len = array_len
        self.role = role or HEADER_ROLES.get(name, "kPayload")
        self.big_endian = big_endian  # SOME/IP representation of the field


def _crc_accumulate(crc: int, byte: int) -> int:
    tmp = (byte ^ (crc & 0xFF)) & 0xFF
    tmp = (tmp ^ (tmp << 4)) & 0xFF
    return ((crc >> 8) ^ (tmp << 8) ^ (tmp << 3) ^ (tmp >> 4)) & 0xFFFF


def ComputeCrcExtra(name: str, fields: list[Field]) -> int:
    """crc_extra as mavgen computes it: message name, then fields in wire order."""
    ordered = sorted(fields, key=lambda f: -(f.size // max(1, f.array_len)))  # stable
    crc = 0xFFFF
    for b in (name + " ").encode("ascii"):
        crc = _crc_accumulate(crc, b)
    for f in ordered:
        for b in (f.mav_type + " ").encode("ascii"):
            crc = _crc_accumulate(crc, b)
        for b in (f.name + " ").encode("ascii"):
            crc = _crc_accumulate(crc, b)
        if f.array_len:
            crc = _crc_accumulate(crc, f.array_len)
    return (crc & 0xFF) ^ (crc >> 8)


class Message:
    def __init__(self, msg_id: int, name: str, desc: str, fields: list[Field],
                 where: str) -> None:
        self.msg_id = msg_id
        self.name = name
        self.desc = desc
        self.fields = fields
        self.where = where  # "Service.Element" for error messages
        self.crc_extra = ComputeCrcExtra(name, fields)

    def payload_size(self) -> int:
        return sum(f.size for f in self.fields)


class ElementSpec:
    """A service element with its MAVLink messages (none when it is outside `elements`)."""

    def __init__(self, el_id: int, is_event: bool, name: str, enabled: bool) -> None:
        self.id = el_id
        self.is_event = is_event
        self.name = name
        self.enabled = enabled
        self.req: Message | None = None  # event: the event message, method: REQ
        self.resp: Message | None = None


def IsExcluded(el) -> bool:
    """No `mavlink` block or `"mavlink": false`: the element cannot be carried over MAVLink."""
    return el.mavlink is None or el.mavlink is False


def _as_block(block, allowed: tuple[str, ...], where: str,
              removed: dict[str, str] | None = None) -> dict:
    if not isinstance(block, dict):
        raise MavlinkModelError(f"{where}: the mavlink block must be an object")
    for k in block:
        if removed and k in removed:
            raise MavlinkModelError(f"{where}: mavlink.{k} is removed ({removed[k]})")
        if k not in allowed:
            raise MavlinkModelError(
                f"{where}: unknown key mavlink.{k} (allowed: {', '.join(allowed)})")
    return block


def _get_id(block: dict, key: str, where: str) -> int:
    if key not in block:
        raise MavlinkModelError(f"{where}: missing mavlink.{key}")
    return _int(block[key], 0, 0xFFFFFF, where, f"mavlink.{key}")


def _max_len(block: dict, field_name: str, where: str) -> int:
    ml = block.get("max_len")
    if isinstance(ml, dict):
        ml = ml.get(field_name)
    if ml is None:
        raise MavlinkModelError(f"{where}: string field '{field_name}' needs mavlink.max_len")
    return _int(ml, 1, MAX_PAYLOAD, where, "mavlink.max_len")


def _flatten(dt, prefix: str, block: dict, where: str, out: list[Field],
             endian: str = "little") -> None:
    """Flattens a model type into MAVLink fields (struct members become `a_b`).

    `endian` is the SOME/IP endianness: struct members inherit the struct's, a top-level
    primitive is little-endian.
    """
    if dt.typ_str == "struct":
        for vname, vtype in dt.variable_list.items():
            _flatten(vtype, (prefix + "_" if prefix else "") + _snake(vname), block,
                     where, out, dt.endian)
        return
    name = prefix or "value"
    if not _FIELD_NAME_RE.match(name):
        raise MavlinkModelError(
            f"{where}: field name '{name}' is not allowed in MAVLink after flattening "
            "(allowed: ASCII [a-z][a-z0-9_]*)")
    if dt.typ_str == "std::string":
        n = _max_len(block, name, where)
        out.append(Field(name, "char", n, n))
        return
    if dt.typ_str not in _TYPES:
        raise MavlinkModelError(f"{where}: unsupported type '{dt.typ_str}' in field '{name}'")
    mt, size = _TYPES[dt.typ_str]
    out.append(Field(name, mt, size, big_endian=(endian == "big")))


def _is_void(dt) -> bool:
    return dt is None or dt.typ_str == "void"


def _check_fields(fields: list[Field], where: str, header_len: int) -> None:
    seen: set[str] = set()
    for f in fields[header_len:]:
        if f.name in HEADER_FIELD_NAMES:
            raise MavlinkModelError(
                f"{where}: field '{f.name}' collides with the message header "
                f"({', '.join(sorted(HEADER_FIELD_NAMES))})")
        if f.name in PythonReservedWords():
            raise MavlinkModelError(f"{where}: field '{f.name}' is a reserved Python name")
    for f in fields:
        if f.name in seen:
            raise MavlinkModelError(f"{where}: duplicate field '{f.name}' after flattening")
        seen.add(f.name)


def _desc(element, fallback: str) -> str:
    text = getattr(element, "desc", "")
    return text if isinstance(text, str) and text else fallback


def _header(table: list[tuple[str, str, int]]) -> list[Field]:
    return [Field(n, t, s) for n, t, s in table]


def _is_enabled(el, enabled: set[str] | None) -> bool:
    """An element is on the radio when it has a block and (if given) is in the whitelist."""
    return not IsExcluded(el) and (enabled is None or el.name in enabled)


def _build_event(service, ev, svc: str, enabled: set[str] | None) -> ElementSpec:
    spec = ElementSpec(ev.id, True, ev.name, _is_enabled(ev, enabled))
    if not spec.enabled:
        return spec
    where = f"{service.name}.{ev.name}"
    if not NAME_RE.match(ev.name):
        raise MavlinkModelError(f"{where}: element name is not allowed in MAVLink "
                                "(allowed: ASCII [A-Za-z][A-Za-z0-9_]*)")
    if isinstance(ev.mavlink, dict) and "max_rate_hz" in ev.mavlink:
        raise MavlinkModelError(
            f"{where}: mavlink.max_rate_hz was removed from service.json: the limit is per "
            "radio, set it in app.json: \"elements\": {\"<Event>\": {\"<link>\": "
            "{\"max_rate_hz\": N}}}")
    block = _as_block(ev.mavlink, EVENT_BLOCK_KEYS, where)
    mid = _get_id(block, "msg_id", where)
    fields = _header(EVENT_HEADER)
    if not _is_void(ev.out_parm):
        _flatten(ev.out_parm, "", block, where, fields)
    _check_fields(fields, where, len(EVENT_HEADER))
    spec.req = Message(mid, f"SRP_{svc}_{_snake(ev.name).upper()}",
                       _desc(ev, f"Event {where}"), fields, where)
    return spec


def _build_method(service, me, svc: str, enabled: set[str] | None) -> ElementSpec:
    spec = ElementSpec(me.id, False, me.name, _is_enabled(me, enabled))
    if not spec.enabled:
        return spec
    where = f"{service.name}.{me.name}"
    if not NAME_RE.match(me.name):
        raise MavlinkModelError(f"{where}: element name is not allowed in MAVLink "
                                "(allowed: ASCII [A-Za-z][A-Za-z0-9_]*)")
    block = _as_block(me.mavlink, METHOD_BLOCK_KEYS, where, REMOVED_METHOD_BLOCK_KEYS)
    req_id = _get_id(block, "req_id", where)
    req_fields = _header(METHOD_HEADER)
    if not _is_void(me.in_parm):
        _flatten(me.in_parm, "", block, where, req_fields)
    _check_fields(req_fields, where, len(METHOD_HEADER))
    base = f"SRP_{svc}_{_snake(me.name).upper()}"
    spec.req = Message(req_id, base + "_REQ", _desc(me, f"Request of method {where}"),
                       req_fields, where)
    if _is_void(me.out_parm):
        if "resp_id" in block:
            raise MavlinkModelError(f"{where}: a method without 'out' cannot have mavlink.resp_id")
        return spec
    resp_id = _get_id(block, "resp_id", where)
    resp_fields = _header(METHOD_HEADER) + [Field(*RESP_STATUS)]
    _flatten(me.out_parm, "", block, where, resp_fields)
    _check_fields(resp_fields, where, len(METHOD_HEADER) + 1)
    spec.resp = Message(resp_id, base + "_RESP", _desc(me, f"Response of method {where}"),
                        resp_fields, where)
    return spec


def BuildElements(service, enabled: set[str] | None = None) -> list[ElementSpec]:
    """All elements of a service (events, then methods); only `enabled` ones (names; None = all
    with a mavlink block) carry messages."""
    svc = _snake(service.name.split(".")[-1]).upper()
    seen: dict[int, str] = {}
    for el in list(service.events) + list(service.methods):
        # the runtime maps elements by SOME/IP id, so a duplicate id maps to the wrong element
        if el.id in seen:
            raise MavlinkModelError(
                f"{service.name}.{el.name}: SOME/IP id {el.id} is also used by "
                f"{service.name}.{seen[el.id]}")
        seen[el.id] = el.name
    return ([_build_event(service, ev, svc, enabled) for ev in service.events]
            + [_build_method(service, me, svc, enabled) for me in service.methods])


def BuildMessages(service, enabled: set[str] | None = None) -> list[Message]:
    return [m for spec in BuildElements(service, enabled) for m in (spec.req, spec.resp)
            if m is not None]


def Validate(messages: list[Message]) -> None:
    """Message id and name collisions (including id 0 from minimal.xml), payload size."""
    by_id: dict[int, Message] = {}
    names: set[str] = set()
    for m in messages:
        if m.msg_id in RESERVED_IDS:
            raise MavlinkModelError(
                f"{m.where}: ID {m.msg_id} is taken by {RESERVED_IDS[m.msg_id]} (minimal.xml); a periodic "
                f"heartbeat is an ordinary event of the application with its own msg_id")
        if m.msg_id in by_id:
            raise MavlinkModelError(
                f"{m.where}: ID {m.msg_id} collides with {by_id[m.msg_id].where} "
                f"({by_id[m.msg_id].name})")
        by_id[m.msg_id] = m
        if m.name in names:
            raise MavlinkModelError(f"{m.where}: duplicate message name {m.name}")
        names.add(m.name)
        if m.payload_size() > MAX_PAYLOAD:
            raise MavlinkModelError(
                f"{m.where}: payload {m.payload_size()} B > {MAX_PAYLOAD} B ({m.name})")


# ---------------------------------------------------------------- application and CPU checks
# An entry is (name_in_app, direction, Service, instance, MavlinkItem); CPU entries start with
# the application name. Direction: "kOut" = provide, "kIn" = require.

def CpuEntries(apps) -> list[tuple]:
    out = []
    for app in sorted(apps, key=lambda a: a.name):
        for name, d, service, inst, item in app.GetMavlinkEntries():
            out.append((app.name, name, d, service, inst, item))
    return out


def ValidateApp(app_name: str, entries) -> None:
    """Prints the WARNING lines for one application (called by the generator only)."""
    for _name, _d, _service, _inst, item in entries:
        for w in item.warnings:
            print(f"WARNING mavlink: {w}", file=sys.stderr)


def CheckCpu(entries, daemon: DaemonConfig | None) -> None:
    """Conflicts between applications of one daemon and agreement with the daemon config.

    The daemon rejects the same things at registration; here they fail at generation time.
    The duplicate-instance rule is the daemon's: one (service, instance, role) per CPU.
    """
    seen: dict[tuple, str] = {}
    for app, name, d, service, inst, item in entries:
        key = (service.name, inst, d)
        where = f"{app}.{name}"
        if key in seen:
            raise MavlinkModelError(
                f"{where}: {'provide' if d == 'kOut' else 'require'} of {service.name} "
                f"instance {inst} is already registered by {seen[key]}")
        seen[key] = where
        if daemon is None:
            continue
        for el_name, per_link in item.elements.items():
            for ln in per_link:
                if ln not in daemon.links:
                    raise MavlinkModelError(
                        f"{where}: elements.{el_name}: unknown link '{ln}' (daemon config "
                        f"links: {', '.join(daemon.links) or 'none'})")
        # replay state is shared by the links of an instance
        modes = {ln: (daemon.links[ln].signing_enabled, daemon.links[ln].accept_unsigned)
                 for ln in item.links}
        if len(set(modes.values())) > 1:
            desc = ", ".join(f"{ln}: enabled={m[0]} accept_unsigned={m[1]}"
                             for ln, m in modes.items())
            raise MavlinkModelError(
                f"{where}: the links of one instance need the same signing.enabled and "
                f"signing.accept_unsigned ({desc})")


def CpuMessages(entries, daemon: DaemonConfig | None) -> list[Message]:
    """Dialect messages of the CPU (each service once); msg_id collisions name the apps."""
    services: dict[str, object] = {}
    owners: dict[str, list[str]] = {}
    used: dict[str, set[str]] = {}  # elements of a service listed by some instance
    for app, _name, _d, service, _inst, item in entries:
        services.setdefault(service.name, service)
        used.setdefault(service.name, set()).update(item.elements)
        if app not in owners.setdefault(service.name, []):
            owners[service.name].append(app)
    msgs: list[Message] = []
    by_id: dict[int, tuple[Message, str]] = {}
    for sname, service in services.items():
        for m in BuildMessages(service, used[sname]):
            prior = by_id.get(m.msg_id)
            if prior is not None:
                raise MavlinkModelError(
                    f"msg_id {m.msg_id} collision: {m.where} (apps: {', '.join(owners[sname])}) "
                    f"and {prior[0].where} (apps: {prior[1]}); msg_id is unique per CPU")
            by_id[m.msg_id] = (m, ", ".join(owners[sname]))
            msgs.append(m)
    Validate(msgs)
    return msgs


# ---------------------------------------------------------------- bandwidth budget and mtu

def FrameSize(msg: Message, signed: bool) -> int:
    """Bytes on the air for one frame: header 10 + CRC 2 (+ signature 13) + the FULL payload
    (the worst case: the v2 truncation of trailing zero bytes is not counted as a saving)."""
    return FRAME_OVERHEAD + msg.payload_size() + (SIGNATURE_SIZE if signed else 0)


class BudgetRow:
    """One (element, link) of an instance as the CPU transmits it."""

    def __init__(self, element: str, app: str, inst: int, kind: str, req_size: int, resp_size: int,
                 rate_hz: float) -> None:
        self.element = element  # service.element
        self.app = app          # full application name
        self.inst = inst
        self.kind = kind        # "event" | "method"
        self.req_size = req_size
        self.resp_size = resp_size  # 0 = no RESP
        self.rate_hz = rate_hz  # events only; 0 = unlimited

    @property
    def bytes_per_s(self) -> float:
        return self.req_size * self.rate_hz if self.kind == "event" else 0.0


class LinkBudget:
    def __init__(self, name: str, link: DaemonLink) -> None:
        self.name = name
        self.link = link
        self.rows: list[BudgetRow] = []

    @property
    def bytes_per_s(self) -> float:
        return sum(r.bytes_per_s for r in self.rows)

    @property
    def utilization(self) -> float | None:
        """Fraction of `air_rate_bps` used by the events; None = no budget on the link."""
        if not self.link.air_rate_bps:
            return None
        return self.bytes_per_s * 8 / self.link.air_rate_bps


def CpuBudget(entries, daemon: DaemonConfig | None) -> tuple[dict[str, LinkBudget], list[str]]:
    """Per-link frame sizes, event load and the mtu / air_rate_bps checks of the CPU.

    Counted: the events the CPU provides (kOut) x `max_rate_hz`; methods are listed without a sum
    (no frequency). Errors: an event without `max_rate_hz` on a link with `air_rate_bps`; a frame
    larger than the `mtu` of the link (any element of the link, both directions); a load > 100%.
    Returns (budgets by link, warnings: a load above `max_utilization`). Without a daemon config
    there is nothing to compute.
    """
    if daemon is None:
        return {}, []
    budgets = {n: LinkBudget(n, link) for n, link in daemon.links.items()}
    specs: dict[str, dict[str, ElementSpec]] = {}
    for app, name, d, service, inst, item in entries:
        if service.name not in specs:
            used: set[str] = set()
            for e in entries:
                if e[3].name == service.name:
                    used.update(e[5].elements)
            specs[service.name] = {s.name: s for s in BuildElements(service, used) if s.enabled}
        for el_name, per_link in item.elements.items():
            spec = specs[service.name][el_name]
            where = f"{app}.{name}: elements.{el_name}"
            for ln, rate in per_link.items():
                link = daemon.links[ln]
                for m in (spec.req, spec.resp):
                    if m is None:
                        continue
                    size = FrameSize(m, link.signing_enabled)
                    if link.mtu and size > link.mtu:
                        sig = f" + signature {SIGNATURE_SIZE}" if link.signing_enabled else ""
                        raise MavlinkModelError(
                            f"{where}: link '{ln}': the frame of {m.name} is {size} B (header + "
                            f"CRC {FRAME_OVERHEAD}{sig} + payload {m.payload_size()} B), above "
                            f"mtu {link.mtu} B of the link")
                if spec.is_event and d != "kOut":
                    continue  # received, not transmitted by this CPU
                if spec.is_event and link.air_rate_bps and not rate:
                    raise MavlinkModelError(
                        f"{where}: link '{ln}' has air_rate_bps={link.air_rate_bps}: the event "
                        "needs max_rate_hz > 0, otherwise its load cannot be computed")
                resp = FrameSize(spec.resp, link.signing_enabled) if spec.resp is not None else 0
                budgets[ln].rows.append(BudgetRow(
                    f"{service.name}.{el_name}", app, inst,
                    "event" if spec.is_event else "method",
                    FrameSize(spec.req, link.signing_enabled), resp,
                    rate if spec.is_event else 0.0))
    warnings: list[str] = []
    for ln, b in budgets.items():
        u = b.utilization
        if u is None:
            continue
        text = (f"link '{ln}': events use {b.bytes_per_s:g} B/s = {b.bytes_per_s * 8:g} bit/s = "
                f"{u * 100:.1f}% of air_rate_bps={b.link.air_rate_bps}")
        if u > 1.0:
            raise MavlinkModelError(text + " (> 100%: the link cannot carry it; lower max_rate_hz "
                                    "or raise air_rate_bps)")
        if u > b.link.max_utilization:
            warnings.append(text + f" (> max_utilization {b.link.max_utilization * 100:g}%)")
    return budgets, warnings


def BudgetInfoLine(shares: dict[str, tuple[float | None, float]]) -> str:
    """`INFO mavlink budget: radio_a 37.2% (max 80%), radio_b -` from {link: (utilization,
    max_utilization)}; a link without `air_rate_bps` has no budget (utilization None): `-`."""
    parts = [f"{ln} {u * 100:.1f}% (max {mx * 100:g}%)" if u is not None else f"{ln} -"
             for ln, (u, mx) in shares.items()]
    return "INFO mavlink budget: " + ", ".join(parts)
