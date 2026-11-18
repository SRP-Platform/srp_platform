"""Walidacja aplikacji i CPU z `on: mavlink`, `<dialect>_links.md` i model C++ w initialization.cc.

Wpis usługi (`entries`) to krotka (nazwa_w_aplikacji, kierunek, Service, instancja,
MavlinkItem), kierunek: "kOut" (provide) / "kIn" (require). Wpisy całego CPU (wszystkie
aplikacje z `on: mavlink`) mają dodatkowo nazwę aplikacji na początku:
(aplikacja, nazwa, kierunek, Service, instancja, MavlinkItem).
"""
from __future__ import annotations

import sys

from tools.model_generator.ara.mavlink.mavlink_daemon_config import DaemonConfig, DaemonLink
from tools.model_generator.ara.mavlink.mavlink_dialect import (
    BuildElements, BuildMessages, ExcludedElements, IsExcluded, Message, Validate)
from tools.model_generator.ara.mavlink.mavlink_errors import MavlinkModelError
from tools.model_generator.ara.mavlink.mavlink_links import MavlinkItem


def ServiceVersion(item: MavlinkItem, daemon: DaemonConfig | None) -> int:
    """Najniższa wersja protokołu wśród łączy instancji (v1 ogranicza ID do 255).

    Bez configu daemona wersje łączy są nieznane: zakładamy v2 (sprawdza je reguła CPU).
    """
    if daemon is None:
        return 2
    return min(daemon.links[n].version for n in item.links if n in daemon.links)


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


def AppMessages(app_name: str, entries) -> list[Message]:
    """Wiadomości dialektu aplikacji (każda usługa raz, nawet gdy provide+require)."""
    services = {}
    for _name, _dir, service, _inst, _item in entries:
        services.setdefault(service.name, service)
    msgs: list[Message] = []
    for service in services.values():
        msgs.extend(BuildMessages(service, 2))
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


def ValidateApp(app_name: str, entries) -> list[Message]:
    """Walidacja jednej aplikacji (bez configu daemona): instancje i wiadomości usług."""
    if not entries:
        return []
    seen_inst: dict[tuple, str] = {}
    for name, _d, service, inst, _item in entries:
        key = (service.name, inst)
        if key in seen_inst:
            raise MavlinkModelError(
                f"{app_name}.{name}: instancja {inst} usługi {service.name} już użyta we wpisie "
                f"{seen_inst[key]} (instancje muszą się różnić)")
        seen_inst[key] = name
    msgs = AppMessages(app_name, entries)
    excluded = AppExcluded(entries)
    if excluded:
        print(f"INFO mavlink: {app_name}: poza MAVLinkiem {len(excluded)} elementów: "
              + ", ".join(excluded), file=sys.stderr)
    for w in InstanceWarnings(app_name, entries):
        print(f"OSTRZEŻENIE mavlink: {w}", file=sys.stderr)
    return msgs


# ---------------------------------------------------------------- CPU (wszystkie aplikacje)

def CpuEntries(apps) -> list[tuple]:
    """Wpisy wszystkich aplikacji CPU: (aplikacja, nazwa, kierunek, Service, instancja, item)."""
    out = []
    for app in sorted(apps, key=lambda a: a.name):
        for name, d, service, inst, item in app.GetMavlinkEntries():
            out.append((app.name, name, d, service, inst, item))
    return out


def CheckCpu(entries, daemon: DaemonConfig | None) -> None:
    """Konflikty między aplikacjami tego samego daemona + zgodność z configiem daemona.

    Odpowiada odrzuceniom rejestracji w daemonie (kConflictInstance / kUnknownLink), ale
    przy generacji, nie na urządzeniu.
    """
    seen: dict[tuple, str] = {}
    for app, name, d, service, inst, item in entries:
        key = (service.name, inst, d)
        where = f"{app}.{name}"
        if key in seen:
            raise MavlinkModelError(
                f"{where}: konflikt instancji — {'provide' if d == 'kOut' else 'require'} "
                f"{service.name} instancja {inst} jest już zarejestrowane przez {seen[key]} "
                "(jeden daemon na CPU: instancje muszą się różnić)")
        seen[key] = where
        if daemon is None:
            continue
        for ln in item.links:
            if ln not in daemon.links:
                raise MavlinkModelError(
                    f"{where}: nieznane łącze '{ln}' w 'links' (zdefiniowane w configu daemona: "
                    f"{', '.join(daemon.links) or 'brak'})")
        # rev. 4 pkt 1: dedup/replay są wspólne dla łączy instancji -> ten sam tryb podpisu.
        modes = {ln: (daemon.links[ln].signing.enabled, daemon.links[ln].signing.accept_unsigned)
                 for ln in item.links}
        if len(set(modes.values())) > 1:
            desc = ", ".join(f"{ln}: enabled={m[0]} accept_unsigned={m[1]}"
                             for ln, m in modes.items())
            raise MavlinkModelError(
                f"{where}: łącza jednej instancji muszą mieć identyczne signing.enabled i "
                f"signing.accept_unsigned ({desc})")


def UnusedLinkWarnings(entries, daemon: DaemonConfig | None) -> list[str]:
    """Łącza z configu daemona, których nie używa żadna instancja żadnej aplikacji CPU."""
    if daemon is None:
        return []
    used = {ln for e in entries for ln in e[5].links}
    return [f"łącze '{n}' z configu daemona nie jest używane przez żadną usługę CPU"
            for n in daemon.links if n not in used]


def CpuMessages(entries, daemon: DaemonConfig | None) -> tuple[list[Message], list[int]]:
    """Wiadomości dialektu CPU (każda usługa raz) + wersje MAVLink użytych łączy.

    Kolizja msg_id między różnymi usługami (także z różnych aplikacji) = błąd generacji
    z nazwami aplikacji; daemon odrzuciłby taką rejestrację (kConflictMsgId).
    """
    versions: dict[str, int] = {}
    services: dict[str, object] = {}
    owners: dict[str, list[str]] = {}
    for app, _name, _d, service, _inst, item in entries:
        versions[service.name] = min(versions.get(service.name, 2), ServiceVersion(item, daemon))
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
                    f"kolizja msg_id {m.msg_id}: {m.where} (aplikacje: {', '.join(owners[sname])}) "
                    f"i {prior[0].where} (aplikacje: {prior[1]}) — msg_id musi być unikalny "
                    "w obrębie CPU (jeden daemon)")
            by_id[m.msg_id] = (m, ", ".join(owners[sname]))
            msgs.append(m)
    Validate(msgs, 2)  # limit ID dla v1 pilnuje BuildMessages per usługa (wg wersji jej łączy)
    used = sorted({daemon.links[n].version for e in entries for n in e[5].links
                   if daemon is not None and n in daemon.links} or {2})
    return msgs, used


def EffectiveRate(element, item: MavlinkItem) -> float | None:
    """Limit Hz eventu w instancji: nadpisanie z wpisu > service.json; None = bez limitu."""
    if element.name in item.max_rate_hz:
        v = item.max_rate_hz[element.name]
    else:
        v = (element.mavlink or {}).get("max_rate_hz") if isinstance(element.mavlink, dict) else None
    return None if not v else float(v)


def _fmt_hz(v: float) -> str:
    return f"{v:g} Hz"


def _labels(entries) -> dict[str, str]:
    """Nazwa aplikacji w tabelach: ostatni człon (bez pakietu), o ile jest jednoznaczny."""
    apps = sorted({e[0] for e in entries})
    shorts = [a.rsplit(".", 1)[-1] for a in apps]
    return {a: (a.rsplit(".", 1)[-1] if shorts.count(a.rsplit(".", 1)[-1]) == 1 else a)
            for a in apps}


def _link_names(entries, daemon: DaemonConfig | None) -> list[str]:
    if daemon is not None:
        return list(daemon.links)
    names: list[str] = []
    for e in entries:
        for ln in e[5].links:
            if ln not in names:
                names.append(ln)
    return names


def RenderMatrix(entries, links: list[str]) -> list[str]:
    """Macierz CPU: wiersz = element usługi, kolumna = łącze, komórka = aplikacja.instancja."""
    o = ["## Macierz element × łącze", "",
         "Komórka: `aplikacja.instancja` (+ limit Hz eventu); `-` = element nie jest nadawany "
         "tym łączem.", "",
         "| element | " + " | ".join(links) + " |", "|---|" + "---|" * len(links)]
    label = _labels(entries)
    seen = set()
    for _a, _n, _d, service, _i, _it in entries:
        if service.name in seen:
            continue
        seen.add(service.name)
        elements = [("event", e) for e in service.events] + [("method", m) for m in service.methods]
        for kind, el in elements:
            cells = []
            for ln in links:
                parts = []
                for app, _name, d, svc, inst, item in entries:
                    if svc.name != service.name or ln not in item.links:
                        continue
                    if IsExcluded(el) or el.name in item.exclude:
                        continue
                    txt = f"{label[app]}.{inst}" + ("" if d == "kOut" else " (proxy)")
                    if kind == "event":
                        rate = EffectiveRate(el, item)
                        if rate is not None:
                            txt += f" @ {_fmt_hz(rate)}"
                    parts.append(txt)
                cells.append(", ".join(parts) or "-")
            o.append(f"| {service.name}.{el.name} ({kind}) | " + " | ".join(cells) + " |")
    o.append("")
    return o


def _signing_text(l: DaemonLink) -> str:
    if not l.signing.enabled:
        return "nie"
    return (f"tak (link_id={l.signing.link_id}, klucz: `{l.signing.key_file}`, "
            f"accept_unsigned={str(l.signing.accept_unsigned).lower()}, "
            f"stan: `{l.signing.state_file or '(domyślny daemona)'}`)")


def RenderLinksMd(dialect: str, daemon: DaemonConfig | None, entries,
                  msgs: list[Message]) -> str:
    """Parametry CPU, które GS musi mieć zgodne (bez zawartości kluczy), macierz łączy CPU."""
    apps = sorted({e[0] for e in entries})
    label = _labels(entries)
    links = _link_names(entries, daemon)
    o = [f"# Łącza MAVLink CPU — dialekt `{dialect}`", "",
         "Aplikacje: " + ", ".join(apps), ""]
    if daemon is not None:
        o += [f"system_id={daemon.system_id}, component_id={daemon.component_id}", "",
              "## Łącza (config MavlinkDaemona)", "",
              "| łącze | driver | plik drivera | MAVLink | HEARTBEAT [Hz] | podpis |",
              "|---|---|---|---|---|---|"]
        for n, l in daemon.links.items():
            o.append(f"| {n} | {l.driver} | `{l.driver_config or '-'}` | v{l.version} | "
                     f"{l.heartbeat_hz} | {_signing_text(l)} |")
        o.append("")
    else:
        o += ["Łącza (nazwy z `links` aplikacji; parametry w configu MavlinkDaemona): "
              + ", ".join(links), ""]
    o += ["## Usługi (instancje)", "",
          "| aplikacja | usługa | kierunek | instancja | łącza | target sys/comp | source sys/comp "
          "| dedup TTL [ms] | exclude | max_rate_hz |", "|---|---|---|---|---|---|---|---|---|---|"]
    for app, name, d, service, inst, item in entries:
        o.append(f"| {label[app]} | {name} ({service.name}) | {'provide' if d == 'kOut' else 'require'} "
                 f"| {inst} | {', '.join(item.links)} | {item.target_system_id}/"
                 f"{item.target_component_id} | {item.source_system_id}/"
                 f"{item.source_component_id} | {item.dedup_ttl_ms} | {', '.join(item.exclude) or '-'} | "
                 f"{', '.join(f'{k}={v}' for k, v in sorted(item.max_rate_hz.items())) or '-'} |")
    o.append("")
    o += RenderMatrix(entries, links)
    excluded = sorted({x for _a, _n, _d, service, _i, _it in entries for x in ExcludedElements(service)})
    o += ["## Elementy poza MAVLinkiem (`\"mavlink\": false` albo brak bloku)", ""]
    o += [f"- {x}" for x in excluded] if excluded else ["(brak)"]
    per_inst = sorted(((a, n, inst, it) for a, n, _d, svc, inst, it in entries if it.exclude),
                      key=lambda t: (t[0], t[1], t[2]))
    o += ["", "## Wykluczenia per instancja (`exclude`)", ""]
    o += [f"- {label[a]}.{n} (instancja {inst}): {', '.join(it.exclude)}"
          for a, n, inst, it in per_inst] \
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


def _elements_cpp(service) -> list[str]:
    """Opisy elementów usługi (id, wiadomości, pola, limity) dla runtime."""
    o = ["        using MF = ara::core::model::ModelMavlinkField;",
         "        using MT = MF::Type;", "        using MR = MF::Role;"]
    for i, spec in enumerate(BuildElements(service, 2)):
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
                  item: MavlinkItem) -> str:
    """Blok C++ dodający ModelMavlink do ModelDataBase (wewnątrz InitializeComDb)."""
    ns = "ara::core::model::"
    o = ["    {", f"        {ns}ModelMavlinkOptions opts;"]
    if item.exclude:
        o.append(f"        opts.exclude = {{{', '.join(_q(x) for x in item.exclude)}}};")
    o.append(f"        opts.service_name = {_q(service.name)};")
    o.append(f"        opts.dedup_ttl_ms = {item.dedup_ttl_ms}U;")
    if item.source_system_id:
        o.append(f"        opts.source_system_id = {item.source_system_id}U;")
    if item.source_component_id:
        o.append(f"        opts.source_component_id = {item.source_component_id}U;")
    for k, v in sorted(item.max_rate_hz.items()):
        o.append(f"        opts.max_rate_hz[{_q(k)}] = {float(v)!r};")
    o += _elements_cpp(service)
    fn = "CreatSkeletonItem" if direction == "kOut" else "CreatProxyItem"
    com_id = f"someip_mavlink_{service.id}_{instance}"
    links = ", ".join(_q(x) for x in item.links)
    o.append(f"        const auto res = {ns}ModelMavlink::{fn}({_q(com_id)}, {instance}U, "
             f"{service.major}U, {service.minor}U, {item.target_system_id}U, "
             f"{item.target_component_id}U, {{{links}}}, opts);")
    o.append(f"        std::ignore = db_.AddNewItem({_q(app_name + '/' + key)}, "
             "{res.Value()});")
    o.append("    }")
    return "\n".join(o) + "\n"
