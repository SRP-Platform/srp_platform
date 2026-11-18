"""Instancje usług na `on: mavlink` w app_config.json aplikacji (rev. 10).

Aplikacja opisuje tylko instancje: `links` (NAZWY łączy z configu MavlinkDaemona), `exclude`,
`max_rate_hz` (+ opcjonalnie target/source/dedup). Łącza, system_id/component_id, podpis i
HEARTBEAT są w configu daemona (`mavlink_daemon_config.py`). Moduł zależy tylko od stdlib.
"""
from __future__ import annotations

import math
import re
import sys

from tools.model_generator.ara.mavlink.mavlink_dialect import IsExcluded
from tools.model_generator.ara.mavlink.mavlink_errors import MavlinkModelError

# Klucze wpisu on=mavlink, które nie mają sensu przy ipc/udp.
MAVLINK_ONLY_ITEM_KEYS = ("links", "exclude", "max_rate_hz", "source_system_id",
                          "source_component_id", "target_system_id", "target_component_id",
                          "dedup_ttl_ms")
# Bloki aplikacji z rev. 4-9: konfiguracja łączy przeniosła się do MavlinkDaemona.
REMOVED_APP_BLOCKS = ("mavlink", "mavlink_links")
DEFAULT_DEDUP_TTL_MS = 10000  # rev. 5, R2
# Wszystkie klucze dozwolone we wpisie provide/require z on=mavlink; inne = błąd
# (literówka "exlude" cicho zmieniałaby zakres radia).
MAVLINK_ITEM_KEYS = ("name", "on", "instance") + MAVLINK_ONLY_ITEM_KEYS

_NAME_RE = re.compile(r"^[A-Za-z][A-Za-z0-9_]*$")


def _int(v, lo: int, hi: int, where: str, what: str) -> int:
    if isinstance(v, bool) or not isinstance(v, int) or v < lo or v > hi:
        raise MavlinkModelError(f"{where}: {what}={v!r} poza zakresem {lo}..{hi}")
    return v


class MavlinkItem:
    """Usługa wystawiona/wymagana na `on: mavlink` (wybór łączy daemona per instancja)."""

    def __init__(self, links: list[str], target_system_id: int,
                 target_component_id: int, exclude: list[str] | None = None,
                 max_rate_hz: dict[str, float] | None = None,
                 source_system_id: int = 0, source_component_id: int = 0,
                 dedup_ttl_ms: int = DEFAULT_DEDUP_TTL_MS) -> None:
        self.links = links
        self.target_system_id = target_system_id
        self.target_component_id = target_component_id
        self.exclude = exclude or []  # elementy wyłączone w tej instancji
        self.max_rate_hz = max_rate_hz or {}  # nadpisania limitów eventów (0 = bez limitu)
        self.source_system_id = source_system_id  # tylko require; 0 = dowolny
        self.source_component_id = source_component_id
        self.dedup_ttl_ms = dedup_ttl_ms  # okno dedup (kopie z wielu łączy)


def RejectAppBlocks(app_json: dict, app_name: str) -> None:
    """Bloki `mavlink` / `mavlink_links` w aplikacji = błąd z podpowiedzią (rev. 10)."""
    for key in REMOVED_APP_BLOCKS:
        if key in app_json:
            raise MavlinkModelError(
                f"{app_name}: blok '{key}' nie jest już dozwolony w aplikacji — konfiguracja "
                "łączy jest w MavlinkDaemon (mavlink_daemon_config.json: system_id, "
                "component_id, links); aplikacja ma tylko instancje on=mavlink "
                "(links, exclude, max_rate_hz)")


def RejectMavlinkKeys(item: dict, app_name: str) -> None:
    """Pola specyficzne dla mavlink przy `on: ipc|udp` = błąd generatora (rev. 4, G)."""
    for key in MAVLINK_ONLY_ITEM_KEYS:
        if key in item:
            raise MavlinkModelError(
                f"{app_name}.{item.get('name', '?')}: pole '{key}' jest dozwolone tylko przy "
                f"on=mavlink (wpis ma on={item.get('on')!r})")


def _parse_exclude_rate(item: dict, where: str, service) -> tuple[list[str], dict]:
    els = {e.name: e for e in list(service.events) + list(service.methods)}
    events = {e.name for e in service.events}
    exclude = item.get("exclude", [])
    if not isinstance(exclude, list) or not all(isinstance(x, str) for x in exclude):
        raise MavlinkModelError(f"{where}: 'exclude' musi być listą nazw elementów")
    if len(set(exclude)) != len(exclude):
        raise MavlinkModelError(f"{where}: powtórzona nazwa w 'exclude'")
    for n in exclude:
        if n not in els:
            raise MavlinkModelError(
                f"{where}: exclude: nieznany element '{n}' usługi {service.name} "
                f"(dostępne: {', '.join(els) or 'brak'})")
    rates = item.get("max_rate_hz", {})
    if not isinstance(rates, dict):
        raise MavlinkModelError(f"{where}: 'max_rate_hz' musi być obiektem {{Event: Hz}}")
    out: dict[str, float] = {}
    for n, v in rates.items():
        if n not in els:
            raise MavlinkModelError(
                f"{where}: max_rate_hz: nieznany element '{n}' usługi {service.name}")
        if n not in events:
            raise MavlinkModelError(f"{where}: max_rate_hz dotyczy tylko eventów, '{n}' jest metodą")
        if (isinstance(v, bool) or not isinstance(v, (int, float)) or not math.isfinite(v)
                or v < 0):
            raise MavlinkModelError(f"{where}: max_rate_hz.{n}={v!r} musi być skończoną liczbą >= 0")
        if IsExcluded(els[n]) or n in exclude:
            print(f"OSTRZEŻENIE mavlink: {where}: max_rate_hz.{n} dla elementu wyłączonego "
                  "z MAVLinka (ignorowane)", file=sys.stderr)
        out[n] = v
    return sorted(exclude), out


def ParseServiceItem(item: dict, name: str, app_name: str, service=None,
                     direction: str = "kOut") -> MavlinkItem:
    where = f"{app_name}.{name}"
    for key in item:
        if key not in MAVLINK_ITEM_KEYS:
            raise MavlinkModelError(
                f"{where}: nieznane pole '{key}' we wpisie on=mavlink "
                f"(dozwolone: {', '.join(MAVLINK_ITEM_KEYS)})")
    sel = item.get("links")
    if not isinstance(sel, list) or not sel or not all(isinstance(x, str) for x in sel):
        raise MavlinkModelError(
            f"{where}: on=mavlink wymaga niepustej listy 'links' (nazwy łączy z configu "
            "MavlinkDaemona)")
    for ln in sel:
        if not _NAME_RE.match(ln):
            raise MavlinkModelError(
                f"{where}: nazwa łącza {ln!r} musi pasować do [A-Za-z][A-Za-z0-9_]*")
    if len(set(sel)) != len(sel):
        raise MavlinkModelError(
            f"{where}: powtórzone łącze w 'links': {sorted({x for x in sel if sel.count(x) > 1})}")
    if "instance" not in item:
        raise MavlinkModelError(f"{where}: brak 'instance'")
    exclude, rates = _parse_exclude_rate(item, where, service) if service is not None else ([], {})
    if direction == "kIn" and rates:
        print(f"OSTRZEŻENIE mavlink: {where}: max_rate_hz we wpisie require nie ma efektu "
              "(limit dotyczy nadawcy) — ignorowane", file=sys.stderr)
        rates = {}
    inst = item["instance"]
    if isinstance(inst, bool) or not isinstance(inst, int) or not 0 <= inst <= 65535:
        raise MavlinkModelError(f"{where}: instance={inst!r} poza zakresem 0..65535")
    src_sys = src_comp = 0
    for key in ("source_system_id", "source_component_id"):
        if key in item and direction != "kIn":
            raise MavlinkModelError(f"{where}: '{key}' dotyczy tylko wpisów require")
    if direction == "kIn":
        src_sys = _int(item.get("source_system_id", 0), 0, 255, where, "source_system_id")
        src_comp = _int(item.get("source_component_id", 0), 0, 255, where, "source_component_id")
    ttl = _int(item.get("dedup_ttl_ms", DEFAULT_DEDUP_TTL_MS), 1, 3_600_000, where,
               "dedup_ttl_ms")
    return MavlinkItem(list(sel),
                       _int(item.get("target_system_id", 0), 0, 255, where, "target_system_id"),
                       _int(item.get("target_component_id", 0), 0, 255, where,
                            "target_component_id"), exclude, rates, src_sys, src_comp, ttl)
