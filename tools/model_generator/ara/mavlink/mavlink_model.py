"""MAVLink model: service instances, dialect messages and CPU-wide checks.

Inputs:
  * `app_config.json` entries with `"on": "mavlink"` -> `MavlinkItem` (instance, links, options);
  * `service.json` elements with a `mavlink` block -> `Message` objects of the CPU dialect:
      event  -> SRP_<SERVICE>_<EVENT>
      method -> SRP_<SERVICE>_<METHOD>_REQ (+ _RESP when the method has `out`)
  * the MavlinkDaemon config (`DaemonConfig`): only what the generator needs (link names,
    versions, signing mode). The daemon validates the rest when it starts.

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

V1_MAX_ID = 255
MAX_PAYLOAD = 255
RESERVED_IDS = {0: "HEARTBEAT"}  # taken by minimal.xml
DEFAULT_DEDUP_TTL_MS = 10000

NAME_RE = re.compile(r"^[A-Za-z][A-Za-z0-9_]*$")
_FIELD_NAME_RE = re.compile(r"^[a-z][a-z0-9_]*$")

# Keys of an `on: mavlink` entry in app_config.json; any other key is an error (a typo such as
# "exlude" would silently change which elements the radio carries).
MAVLINK_ONLY_ITEM_KEYS = ("links", "exclude", "max_rate_hz", "source_system_id",
                          "source_component_id", "target_system_id", "target_component_id",
                          "dedup_ttl_ms")
MAVLINK_ITEM_KEYS = ("name", "on", "instance") + MAVLINK_ONLY_ITEM_KEYS
EVENT_BLOCK_KEYS = ("msg_id", "max_rate_hz", "max_len")
METHOD_BLOCK_KEYS = ("req_id", "resp_id", "timeout_ms", "max_len")

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
    def __init__(self, version: int, signing_enabled: bool, accept_unsigned: bool) -> None:
        self.version = version
        self.signing_enabled = signing_enabled
        self.accept_unsigned = accept_unsigned


class DaemonConfig:
    def __init__(self, links: dict[str, DaemonLink]) -> None:
        self.links = links


def LoadDaemonConfig(path: str) -> DaemonConfig:
    """Reads link names, versions and signing mode from mavlink_daemon_config.json."""
    try:
        with open(path, encoding="utf-8") as f:
            raw = json.load(f)
        links = {}
        for name, link in raw["links"].items():
            signing = link.get("signing", {})
            links[name] = DaemonLink(link.get("version", 2), bool(signing.get("enabled", False)),
                                     bool(signing.get("accept_unsigned", False)))
    except (OSError, ValueError, KeyError, AttributeError) as e:
        raise MavlinkModelError(f"{path}: cannot read the daemon config ({e!r})")
    return DaemonConfig(links)


# ---------------------------------------------------------------- app_config.json instances

class MavlinkItem:
    """A service provided or required on `on: mavlink` (links of the daemon, per instance)."""

    def __init__(self, links: list[str], target_system_id: int, target_component_id: int,
                 exclude: list[str], max_rate_hz: dict[str, float], source_system_id: int,
                 source_component_id: int, dedup_ttl_ms: int, warnings: list[str]) -> None:
        self.links = links
        self.target_system_id = target_system_id
        self.target_component_id = target_component_id
        self.exclude = exclude  # elements disabled in this instance
        self.max_rate_hz = max_rate_hz  # event rate overrides (0 = unlimited)
        self.source_system_id = source_system_id  # require only; 0 = any
        self.source_component_id = source_component_id
        self.dedup_ttl_ms = dedup_ttl_ms
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


def _parse_exclude_rate(item: dict, where: str, service,
                        warnings: list[str]) -> tuple[list[str], dict[str, float]]:
    elements = {e.name: e for e in list(service.events) + list(service.methods)}
    events = {e.name for e in service.events}
    exclude = item.get("exclude", [])
    if not isinstance(exclude, list) or not all(isinstance(x, str) for x in exclude):
        raise MavlinkModelError(f"{where}: 'exclude' must be a list of element names")
    if len(set(exclude)) != len(exclude):
        raise MavlinkModelError(f"{where}: duplicate name in 'exclude'")
    for n in exclude:
        if n not in elements:
            raise MavlinkModelError(
                f"{where}: exclude: unknown element '{n}' of service {service.name} "
                f"(available: {', '.join(elements) or 'none'})")
    rates = item.get("max_rate_hz", {})
    if not isinstance(rates, dict):
        raise MavlinkModelError(f"{where}: 'max_rate_hz' must be an object {{Event: Hz}}")
    out: dict[str, float] = {}
    for n, v in rates.items():
        if n not in elements:
            raise MavlinkModelError(
                f"{where}: max_rate_hz: unknown element '{n}' of service {service.name}")
        if n not in events:
            raise MavlinkModelError(f"{where}: max_rate_hz applies to events only, '{n}' is a method")
        if isinstance(v, bool) or not isinstance(v, (int, float)) or v < 0:
            raise MavlinkModelError(f"{where}: max_rate_hz.{n}={v!r} must be a number >= 0")
        if IsExcluded(elements[n]) or n in exclude:
            warnings.append(f"{where}: max_rate_hz.{n} ignored, the element is excluded "
                            "from MAVLink")
            continue
        out[n] = v
    return sorted(exclude), out


def ParseServiceItem(item: dict, name: str, app_name: str, service,
                     direction: str) -> MavlinkItem:
    """Validates one `on: mavlink` entry; direction is "kOut" (provide) or "kIn" (require)."""
    where = f"{app_name}.{name}"
    for key in item:
        if key not in MAVLINK_ITEM_KEYS:
            raise MavlinkModelError(
                f"{where}: unknown key '{key}' in an on=mavlink entry "
                f"(allowed: {', '.join(MAVLINK_ITEM_KEYS)})")
    links = item.get("links")
    if not isinstance(links, list) or not links or not all(isinstance(x, str) for x in links):
        raise MavlinkModelError(
            f"{where}: on=mavlink needs a non-empty 'links' list (link names from the "
            "MavlinkDaemon config)")
    for ln in links:
        if not NAME_RE.match(ln):
            raise MavlinkModelError(f"{where}: link name {ln!r} must match [A-Za-z][A-Za-z0-9_]*")
    if len(set(links)) != len(links):
        raise MavlinkModelError(f"{where}: duplicate link in 'links'")
    if "instance" not in item:
        raise MavlinkModelError(f"{where}: missing 'instance'")
    _int(item["instance"], 0, 65535, where, "instance")
    warnings: list[str] = []
    exclude, rates = _parse_exclude_rate(item, where, service, warnings)
    if direction == "kIn" and rates:
        warnings.append(f"{where}: max_rate_hz in a require entry has no effect "
                        "(the limit applies to the sender)")
        rates = {}
    src_sys = src_comp = 0
    for key in ("source_system_id", "source_component_id"):
        if key in item and direction != "kIn":
            raise MavlinkModelError(f"{where}: '{key}' applies to require entries only")
    if direction == "kIn":
        src_sys = _int(item.get("source_system_id", 0), 0, 255, where, "source_system_id")
        src_comp = _int(item.get("source_component_id", 0), 0, 255, where, "source_component_id")
    return MavlinkItem(
        list(links),
        _int(item.get("target_system_id", 0), 0, 255, where, "target_system_id"),
        _int(item.get("target_component_id", 0), 0, 255, where, "target_component_id"),
        exclude, rates, src_sys, src_comp,
        _int(item.get("dedup_ttl_ms", DEFAULT_DEDUP_TTL_MS), 1, 2**32 - 1, where, "dedup_ttl_ms"),
        warnings)


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
    """A service element with its MAVLink messages (None for excluded elements)."""

    def __init__(self, el_id: int, is_event: bool, name: str, enabled: bool) -> None:
        self.id = el_id
        self.is_event = is_event
        self.name = name
        self.enabled = enabled
        self.req: Message | None = None  # event: the event message, method: REQ
        self.resp: Message | None = None
        self.timeout_ms = 1000
        self.max_rate_hz = 0.0


def IsExcluded(el) -> bool:
    """No `mavlink` block or `"mavlink": false`: the element is not carried over MAVLink."""
    return el.mavlink is None or el.mavlink is False


def ExcludedElements(service) -> list[str]:
    return [f"{service.name}.{e.name}"
            for e in list(service.events) + list(service.methods) if IsExcluded(e)]


def _as_block(block, allowed: tuple[str, ...], where: str) -> dict:
    if not isinstance(block, dict):
        raise MavlinkModelError(f"{where}: the mavlink block must be an object")
    for k in block:
        if k not in allowed:
            raise MavlinkModelError(
                f"{where}: unknown key mavlink.{k} (allowed: {', '.join(allowed)})")
    return block


def _get_id(block: dict, key: str, where: str, version: int) -> int:
    if key not in block:
        raise MavlinkModelError(f"{where}: missing mavlink.{key}")
    v = _int(block[key], 0, 0xFFFFFF, where, f"mavlink.{key}")
    if version == 1 and v > V1_MAX_ID:
        raise MavlinkModelError(
            f"{where}: mavlink.{key}={v} > {V1_MAX_ID} (MAVLink v1 allows IDs <= 255)")
    return v


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


def _build_event(service, ev, svc: str, version: int) -> ElementSpec:
    spec = ElementSpec(ev.id, True, ev.name, not IsExcluded(ev))
    if not spec.enabled:
        return spec
    where = f"{service.name}.{ev.name}"
    if not NAME_RE.match(ev.name):
        raise MavlinkModelError(f"{where}: element name is not allowed in MAVLink "
                                "(allowed: ASCII [A-Za-z][A-Za-z0-9_]*)")
    block = _as_block(ev.mavlink, EVENT_BLOCK_KEYS, where)
    mid = _get_id(block, "msg_id", where, version)
    if "max_rate_hz" in block:
        r = block["max_rate_hz"]
        if isinstance(r, bool) or not isinstance(r, (int, float)) or r <= 0:
            raise MavlinkModelError(f"{where}: mavlink.max_rate_hz must be a number > 0")
        spec.max_rate_hz = float(r)
    fields = _header(EVENT_HEADER)
    if not _is_void(ev.out_parm):
        _flatten(ev.out_parm, "", block, where, fields)
    _check_fields(fields, where, len(EVENT_HEADER))
    spec.req = Message(mid, f"SRP_{svc}_{_snake(ev.name).upper()}",
                       _desc(ev, f"Event {where}"), fields, where)
    return spec


def _build_method(service, me, svc: str, version: int) -> ElementSpec:
    spec = ElementSpec(me.id, False, me.name, not IsExcluded(me))
    if not spec.enabled:
        return spec
    where = f"{service.name}.{me.name}"
    if not NAME_RE.match(me.name):
        raise MavlinkModelError(f"{where}: element name is not allowed in MAVLink "
                                "(allowed: ASCII [A-Za-z][A-Za-z0-9_]*)")
    block = _as_block(me.mavlink, METHOD_BLOCK_KEYS, where)
    req_id = _get_id(block, "req_id", where, version)
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
    resp_id = _get_id(block, "resp_id", where, version)
    spec.timeout_ms = _int(block.get("timeout_ms", 1000), 1, 2**32 - 1, where,
                           "mavlink.timeout_ms")
    resp_fields = _header(METHOD_HEADER) + [Field(*RESP_STATUS)]
    _flatten(me.out_parm, "", block, where, resp_fields)
    _check_fields(resp_fields, where, len(METHOD_HEADER) + 1)
    spec.resp = Message(resp_id, base + "_RESP", _desc(me, f"Response of method {where}"),
                        resp_fields, where)
    return spec


def BuildElements(service, version: int = 2) -> list[ElementSpec]:
    """All elements of a service (events, then methods); excluded ones carry no messages."""
    svc = _snake(service.name.split(".")[-1]).upper()
    seen: dict[int, str] = {}
    for el in list(service.events) + list(service.methods):
        # the runtime maps elements by SOME/IP id, so a duplicate id maps to the wrong element
        if el.id in seen:
            raise MavlinkModelError(
                f"{service.name}.{el.name}: SOME/IP id {el.id} is also used by "
                f"{service.name}.{seen[el.id]}")
        seen[el.id] = el.name
    return ([_build_event(service, ev, svc, version) for ev in service.events]
            + [_build_method(service, me, svc, version) for me in service.methods])


def BuildMessages(service, version: int = 2) -> list[Message]:
    return [m for spec in BuildElements(service, version) for m in (spec.req, spec.resp)
            if m is not None]


def Validate(messages: list[Message]) -> None:
    """Message id and name collisions (including HEARTBEAT from minimal.xml), payload size."""
    by_id: dict[int, Message] = {}
    names: set[str] = set()
    for m in messages:
        if m.msg_id in RESERVED_IDS:
            raise MavlinkModelError(
                f"{m.where}: ID {m.msg_id} is taken by {RESERVED_IDS[m.msg_id]} (minimal.xml)")
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
    """Prints the INFO and WARNING lines for one application (called by the generator only)."""
    seen: set[str] = set()
    excluded: list[str] = []
    for name, _d, service, inst, item in entries:
        for w in item.warnings:
            print(f"WARNING mavlink: {w}", file=sys.stderr)
        if service.name not in seen:
            seen.add(service.name)
            excluded += ExcludedElements(service)
        els = list(service.events) + list(service.methods)
        if els and all(IsExcluded(e) or e.name in item.exclude for e in els):
            print(f"WARNING mavlink: {app_name}.{name} (service {service.name}, instance {inst}): "
                  "every element is excluded from MAVLink, the instance sends nothing",
                  file=sys.stderr)
    if excluded:
        print(f"INFO mavlink: {app_name}: {len(excluded)} elements outside MAVLink: "
              + ", ".join(sorted(excluded)), file=sys.stderr)


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
        for ln in item.links:
            if ln not in daemon.links:
                raise MavlinkModelError(
                    f"{where}: unknown link '{ln}' (daemon config links: "
                    f"{', '.join(daemon.links) or 'none'})")
        # dedup and replay state are shared by the links of an instance
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
    versions: dict[str, int] = {}
    services: dict[str, object] = {}
    owners: dict[str, list[str]] = {}
    for app, _name, _d, service, _inst, item in entries:
        v = 2 if daemon is None else min(daemon.links[n].version for n in item.links)
        versions[service.name] = min(versions.get(service.name, 2), v)
        services.setdefault(service.name, service)
        if app not in owners.setdefault(service.name, []):
            owners[service.name].append(app)
    msgs: list[Message] = []
    by_id: dict[int, tuple[Message, str]] = {}
    for sname, service in services.items():
        for m in BuildMessages(service, versions[sname]):
            prior = by_id.get(m.msg_id)
            if prior is not None:
                raise MavlinkModelError(
                    f"msg_id {m.msg_id} collision: {m.where} (apps: {', '.join(owners[sname])}) "
                    f"and {prior[0].where} (apps: {prior[1]}); msg_id is unique per CPU")
            by_id[m.msg_id] = (m, ", ".join(owners[sname]))
            msgs.append(m)
    Validate(msgs)
    return msgs
