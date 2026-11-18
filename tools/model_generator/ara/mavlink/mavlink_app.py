"""Walidacja całej aplikacji mavlink, `<dialect>_links.md` i model C++ w initialization.cc.

Wpis usługi (`entries`) to krotka (nazwa_w_aplikacji, kierunek, Service, instancja,
MavlinkItem), kierunek: "kOut" (provide) / "kIn" (require).
"""
from __future__ import annotations

import sys

from tools.model_generator.ara.mavlink.mavlink_dialect import (
    AllExcluded, BuildElements, BuildMessages, ExcludedElements, IsExcluded, Message, Validate)
from tools.model_generator.ara.mavlink.mavlink_errors import MavlinkModelError
from tools.model_generator.ara.mavlink.mavlink_links import (
    MavlinkAppConfig, MavlinkLink, MavlinkItem, WarnUnused)


def ServiceVersion(item: MavlinkItem, links: dict[str, MavlinkLink]) -> int:
    """Najniższa wersja protokołu wśród łączy usługi (v1 ogranicza ID do 255)."""
    return min(links[n].version for n in item.links)


def AppExcluded(entries) -> list[str]:
    """Wyłączone elementy usług aplikacji (każda usługa raz)."""
    out: list[str] = []
    seen = set()
    for _name, _dir, service, _inst, _item in entries:
        if service.name in seen:
            continue
        seen.add(service.name)
        out.extend(ExcludedElements(service))
    return sorted(out)


def AppMessages(app_name: str, entries, links: dict[str, MavlinkLink]) -> list[Message]:
    """Wiadomości dialektu całej aplikacji (każda usługa raz, nawet gdy provide+require)."""
    vers: dict[str, int] = {}
    services = {}
    for _name, _dir, service, _inst, item in entries:
        vers[service.name] = min(vers.get(service.name, 2), ServiceVersion(item, links))
        services.setdefault(service.name, service)
    msgs: list[Message] = []
    for sname, service in services.items():
        msgs.extend(BuildMessages(service, vers[sname]))
    Validate(msgs, 2)
    return msgs


def AllExcludedInInstance(service, item: MavlinkItem) -> bool:
    els = list(service.events) + list(service.methods)
    return bool(els) and all(IsExcluded(e) or e.name in item.exclude for e in els)


def InstanceWarnings(app_name: str, entries) -> list[str]:
    """W_ALL_EXCLUDED: instancja bez żadnego elementu na MAVLinku (nazwa usługi i instancji)."""
    out = []
    for name, _d, service, inst, item in entries:
        if AllExcludedInInstance(service, item):
            out.append(f"W_ALL_EXCLUDED: {app_name}.{name} (usługa {service.name}, instancja "
                       f"{inst}): wszystkie elementy poza MAVLinkiem (\"mavlink\": false, brak bloku "
                       "albo exclude) — instancja niczego nie nadaje")
    return out


def ValidateApp(app_name: str, cfg: MavlinkAppConfig | None,
                links: dict[str, MavlinkLink] | None, entries) -> list[Message]:
    if not entries:
        return []
    if cfg is None:
        raise MavlinkModelError(
            f"{app_name}: usługa z on=mavlink wymaga bloku 'mavlink' (system_id, dialect)")
    if not links:
        raise MavlinkModelError(f"{app_name}: usługa z on=mavlink wymaga 'mavlink_links'")
    seen_inst: dict[tuple, str] = {}
    for name, _d, service, inst, _item in entries:
        key = (service.name, inst)
        if key in seen_inst:
            raise MavlinkModelError(
                f"{app_name}.{name}: instancja {inst} usługi {service.name} już użyta we wpisie "
                f"{seen_inst[key]} (instancje muszą się różnić)")
        seen_inst[key] = name
    msgs = AppMessages(app_name, entries, links)
    excluded = AppExcluded(entries)
    if excluded:
        print(f"INFO mavlink: {app_name}: poza MAVLinkiem {len(excluded)} elementów: "
              + ", ".join(excluded), file=sys.stderr)
    for w in InstanceWarnings(app_name, entries):
        print(f"OSTRZEŻENIE mavlink: {w}", file=sys.stderr)
    WarnUnused(app_name, links, [e[4] for e in entries])
    return msgs


def EffectiveRate(element, item: MavlinkItem) -> float | None:
    """Limit Hz eventu w instancji: nadpisanie z wpisu > service.json; None = bez limitu."""
    if element.name in item.max_rate_hz:
        v = item.max_rate_hz[element.name]
    else:
        v = (element.mavlink or {}).get("max_rate_hz") if isinstance(element.mavlink, dict) else None
    return None if not v else float(v)


def _fmt_hz(v: float) -> str:
    return f"{v:g} Hz"


def RenderMatrix(entries, links: dict[str, MavlinkLink]) -> list[str]:
    """Macierz: wiersz = element usługi, kolumna = łącze, komórka = instancja(e) + limit."""
    o = ["## Macierz element × łącze", "",
         "Komórka: `instancja` (+ limit Hz eventu); `-` = element nie jest nadawany tym łączem.", "",
         "| element | " + " | ".join(links) + " |", "|---|" + "---|" * len(links)]
    seen = set()
    for _n, _d, service, _i, _it in entries:
        if service.name in seen:
            continue
        seen.add(service.name)
        elements = [("event", e) for e in service.events] + [("method", m) for m in service.methods]
        for kind, el in elements:
            cells = []
            for ln in links:
                parts = []
                for _name, d, svc, inst, item in entries:
                    if svc.name != service.name or ln not in item.links:
                        continue
                    if IsExcluded(el) or el.name in item.exclude:
                        continue
                    txt = str(inst) + ("" if d == "kOut" else " (proxy)")
                    if kind == "event":
                        rate = EffectiveRate(el, item)
                        if rate is not None:
                            txt += f" @ {_fmt_hz(rate)}"
                    parts.append(txt)
                cells.append(", ".join(parts) or "-")
            o.append(f"| {service.name}.{el.name} ({kind}) | " + " | ".join(cells) + " |")
    o.append("")
    return o


def RenderLinksMd(app_name: str, cfg: MavlinkAppConfig, links: dict[str, MavlinkLink],
                  entries, msgs: list[Message]) -> str:
    """Parametry łączy, które GS musi mieć zgodne (bez zawartości kluczy)."""
    o = [f"# Łącza MAVLink — {app_name} (dialekt `{cfg.dialect}`)", "",
         f"system_id={cfg.system_id}, component_id={cfg.component_id}", "",
         "## Łącza", "",
         "| łącze | MAVLink | HEARTBEAT [Hz] | podpis |",
         "|---|---|---|---|"]
    for n, l in links.items():
        sign = (f"tak (link_id={l.signing.link_id}, klucz: `{l.signing.key_file}`, "
                f"accept_unsigned={str(l.signing.accept_unsigned).lower()}, "
                f"stan: `{l.signing.state_file}`)"
                if l.signing.enabled else "nie")
        o.append(f"| {n} | v{l.version} | {l.heartbeat_hz} | {sign} |")
    o.append("")
    o += ["## Usługi (instancje)", "",
          "| usługa | kierunek | instancja | łącza | target sys/comp | source sys/comp "
          "| dedup TTL [ms] | exclude | max_rate_hz |", "|---|---|---|---|---|---|---|---|---|"]
    for name, d, service, inst, item in entries:
        o.append(f"| {name} ({service.name}) | {'provide' if d == 'kOut' else 'require'} "
                 f"| {inst} | {', '.join(item.links)} | {item.target_system_id}/"
                 f"{item.target_component_id} | {item.source_system_id}/"
                 f"{item.source_component_id} | {item.dedup_ttl_ms} | {', '.join(item.exclude) or '-'} | "
                 f"{', '.join(f'{k}={v}' for k, v in sorted(item.max_rate_hz.items())) or '-'} |")
    o.append("")
    o += RenderMatrix(entries, links)
    excluded = AppExcluded(entries)
    o += ["## Elementy poza MAVLinkiem (`\"mavlink\": false` albo brak bloku)", ""]
    o += [f"- {x}" for x in excluded] if excluded else ["(brak)"]
    per_inst = sorted(((n, inst, it) for n, _d, svc, inst, it in entries if it.exclude),
                      key=lambda t: (t[0], t[1]))
    o += ["", "## Wykluczenia per instancja (`exclude`)", ""]
    o += [f"- {n} (instancja {inst}): {', '.join(it.exclude)}" for n, inst, it in per_inst] \
        if per_inst else ["(brak)"]
    o += ["", "## Wiadomości", "", "| ID | nazwa | payload [B] |", "|---|---|---|"]
    for m in sorted(msgs, key=lambda x: x.msg_id):
        o.append(f"| {m.msg_id} | {m.name} | {m.payload_size()} |")
    return "\n".join(o) + "\n"


def _q(s: str) -> str:
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


_CPP_TYPES = {
    "uint8_t": "kU8", "int8_t": "kI8", "uint16_t": "kU16", "int16_t": "kI16",
    "uint32_t": "kU32", "int32_t": "kI32", "uint64_t": "kU64", "int64_t": "kI64",
    "float": "kF32", "double": "kF64", "char": "kChar"}


def _message_cpp(var: str, m: Message) -> list[str]:
    o = [f"        {var}.msg_id = {m.msg_id}U;",
         f"        {var}.crc_extra = {m.crc_extra}U;",
         f"        {var}.name = {_q(m.name)};"]
    fields = ", ".join(
        f"MF{{{_q(f.name)}, MT::{_CPP_TYPES[f.mav_type]}, {f.array_len}U, MR::{f.role}, "
        f"{str(f.big_endian).lower()}}}" for f in m.fields)
    o.append(f"        {var}.fields = {{{fields}}};")
    return o


def _elements_cpp(service, version: int) -> list[str]:
    """Opisy elementów usługi (id, wiadomości, pola, limity) dla runtime."""
    o = ["        using MF = ara::core::model::ModelMavlinkField;",
         "        using MT = MF::Type;", "        using MR = MF::Role;"]
    for i, spec in enumerate(BuildElements(service, version)):
        v = f"el{i}"
        o.append(f"        ara::core::model::ModelMavlinkElement {v};")
        o.append(f"        {v}.id = {spec.id}U;")
        o.append(f"        {v}.is_event = {str(spec.is_event).lower()};")
        o.append(f"        {v}.enabled = {str(spec.enabled).lower()};")
        o.append(f"        {v}.name = {_q(spec.name)};")
        if spec.enabled:
            o += _message_cpp(f"{v}.req", spec.req)
            if spec.resp is not None:
                o.append(f"        {v}.has_resp = true;")
                o += _message_cpp(f"{v}.resp", spec.resp)
            if spec.is_event and spec.max_rate_hz:
                o.append(f"        {v}.max_rate_hz = {float(spec.max_rate_hz)!r};")
            if not spec.is_event:
                o.append(f"        {v}.timeout_ms = {spec.timeout_ms}U;")
        o.append(f"        opts.elements.push_back({v});")
    return o


def CreateItemCpp(app_name: str, key: str, direction: str, service, instance: int,
                  cfg: MavlinkAppConfig, links: dict[str, MavlinkLink],
                  item: MavlinkItem) -> str:
    """Blok C++ dodający ModelMavlink do ModelDataBase (wewnątrz InitializeComDb)."""
    ns = "ara::core::model::"
    o = ["    {"]
    names = []
    for i, ln in enumerate(item.links):
        l = links[ln]
        v = f"link{i}"
        names.append(v)
        o.append(f"        {ns}ModelMavlinkLink {v};")
        o.append(f"        {v}.name = {_q(l.name)};")
        o.append(f"        {v}.version = {l.version};")
        o.append(f"        {v}.heartbeat_hz = {float(l.heartbeat_hz)!r};")
        s = l.signing
        o.append(f"        {v}.signing_enabled = {str(s.enabled).lower()};")
        if s.enabled:
            o.append(f"        {v}.signing_key_file = {_q(s.key_file)};")
            o.append(f"        {v}.signing_link_id = {s.link_id}U;")
            o.append(f"        {v}.accept_unsigned = {str(s.accept_unsigned).lower()};")
            o.append(f"        {v}.signing_state_file = {_q(s.state_file)};")
    o.append(f"        {ns}ModelMavlinkOptions opts;")
    if item.exclude:
        o.append(f"        opts.exclude = {{{', '.join(_q(x) for x in item.exclude)}}};")
    o.append(f"        opts.dedup_ttl_ms = {item.dedup_ttl_ms}U;")
    if item.source_system_id:
        o.append(f"        opts.source_system_id = {item.source_system_id}U;")
    if item.source_component_id:
        o.append(f"        opts.source_component_id = {item.source_component_id}U;")
    for k, v in sorted(item.max_rate_hz.items()):
        o.append(f"        opts.max_rate_hz[{_q(k)}] = {float(v)!r};")
    o += _elements_cpp(service, ServiceVersion(item, links))
    fn = "CreatSkeletonItem" if direction == "kOut" else "CreatProxyItem"
    com_id = f"someip_mavlink_{service.id}_{instance}"
    o.append(f"        const auto res = {ns}ModelMavlink::{fn}({_q(com_id)}, {instance}U, "
             f"{service.major}U, {service.minor}U, {cfg.system_id}U, {cfg.component_id}U, "
             f"{item.target_system_id}U, {item.target_component_id}U, {_q(cfg.dialect)}, "
             f"{{{', '.join(names)}}}, opts);")
    o.append(f"        std::ignore = db_.AddNewItem({_q(app_name + '/' + key)}, "
             "{res.Value()});")
    o.append("    }")
    return "\n".join(o) + "\n"
