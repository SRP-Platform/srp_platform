"""Model łączy MAVLink aplikacji: parsowanie i walidacja (rev. 4 specyfikacji).

Aplikacja definiuje łącza raz (`mavlink_links`), a usługi wybierają je listą
`links`. Moduł nie zależy od reszty generatora (tylko stdlib).
"""
from __future__ import annotations

import math
import re
import sys

from tools.model_generator.ara.mavlink.mavlink_dialect import IsExcluded
from tools.model_generator.ara.mavlink.mavlink_errors import MavlinkModelError

# Pola łącza (rev. 8): tylko version, heartbeat_hz, signing. Transport łącza bindujesz w kodzie.
LINK_KEYS = ("version", "heartbeat_hz", "signing")
# Dawne pola transportu: jawny błąd z podpowiedzią zamiast ogólnego „nieznane pole”.
REMOVED_LINK_KEYS = ("transport", "device", "baudrate", "bind", "peer", "max_bytes_per_s")
SIGNING_KEYS = ("enabled", "key_file", "link_id", "accept_unsigned", "state_file")
# Klucze wpisu on=mavlink, które nie mają sensu przy ipc/udp.
MAVLINK_ONLY_ITEM_KEYS = ("links", "exclude", "max_rate_hz", "source_system_id",
                          "source_component_id", "target_system_id", "target_component_id",
                          "dedup_ttl_ms")
APP_MAVLINK_KEYS = ("system_id", "component_id", "dialect")
DEFAULT_DEDUP_TTL_MS = 10000  # rev. 5, R2
# Wszystkie klucze dozwolone we wpisie provide/require z on=mavlink; inne = błąd
# (literówka "exlude" cicho zmieniałaby zakres radia).
MAVLINK_ITEM_KEYS = ("name", "on", "instance") + MAVLINK_ONLY_ITEM_KEYS

_NAME_RE = re.compile(r"^[A-Za-z][A-Za-z0-9_]*$")


def _int(v, lo: int, hi: int, where: str, what: str) -> int:
    if isinstance(v, bool) or not isinstance(v, int) or v < lo or v > hi:
        raise MavlinkModelError(f"{where}: {what}={v!r} poza zakresem {lo}..{hi}")
    return v


class Signing:
    def __init__(self, enabled=False, key_file="", link_id=0, accept_unsigned=False,
                 state_file=""):
        self.enabled = enabled
        self.key_file = key_file
        self.link_id = link_id
        self.accept_unsigned = accept_unsigned
        self.state_file = state_file


class MavlinkLink:
    def __init__(self, name: str) -> None:
        self.name = name
        self.version = 2
        self.heartbeat_hz = 1
        self.signing = Signing()


class MavlinkAppConfig:
    def __init__(self, system_id: int, component_id: int, dialect: str) -> None:
        self.system_id = system_id
        self.component_id = component_id
        self.dialect = dialect


class MavlinkItem:
    """Usługa wystawiona/wymagana na `on: mavlink` (wybór łączy per usługa)."""

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


def ParseLink(name: str, raw, app_name: str) -> MavlinkLink:
    where = f"{app_name}.mavlink_links.{name}"
    if not _NAME_RE.match(name):
        raise MavlinkModelError(f"{where}: nazwa łącza musi pasować do [A-Za-z][A-Za-z0-9_]*")
    if not isinstance(raw, dict):
        raise MavlinkModelError(f"{where}: łącze musi być obiektem")
    link = MavlinkLink(name)
    for key in raw:
        if key in REMOVED_LINK_KEYS:
            raise MavlinkModelError(
                f"{where}: pole '{key}' nie jest już dozwolone — łącze bindujesz w kodzie "
                f"przez RegisterLinkTransport (dozwolone: {', '.join(LINK_KEYS)})")
        if key not in LINK_KEYS:
            hint = (" (modem należy do aplikacji/srp: zarejestruj transport przez "
                    "RegisterLinkTransport)" if key in ("radio", "gpio") else "")
            raise MavlinkModelError(
                f"{where}: nieznane pole '{key}' (dozwolone: {', '.join(LINK_KEYS)}){hint}")
    link.version = raw.get("version", 2)
    if link.version not in (1, 2) or isinstance(link.version, bool):
        raise MavlinkModelError(f"{where}: version={link.version!r} (dozwolone: 1, 2)")
    hb = raw.get("heartbeat_hz", 1)
    if (isinstance(hb, bool) or not isinstance(hb, (int, float)) or not math.isfinite(hb)
            or hb < 0 or hb > 100):
        raise MavlinkModelError(f"{where}: heartbeat_hz={hb!r} poza zakresem 0..100")
    link.heartbeat_hz = hb
    sig = raw.get("signing")
    if sig is not None:
        if not isinstance(sig, dict):
            raise MavlinkModelError(f"{where}: 'signing' musi być obiektem")
        for key in sig:
            if key not in SIGNING_KEYS:
                raise MavlinkModelError(
                    f"{where}: signing.{key} nieznane (dozwolone: {', '.join(SIGNING_KEYS)})")
        enabled = sig.get("enabled", False)
        if not isinstance(enabled, bool):
            raise MavlinkModelError(f"{where}: signing.enabled musi być bool")
        if enabled and link.version == 1:
            raise MavlinkModelError(
                f"{where}: signing wymaga MAVLink v2 (łącze ma version=1)")
        key_file = sig.get("key_file", "")
        if enabled and (not isinstance(key_file, str) or not key_file):
            raise MavlinkModelError(f"{where}: signing.enabled wymaga 'key_file'")
        accept = sig.get("accept_unsigned", False)
        if not isinstance(accept, bool):
            raise MavlinkModelError(f"{where}: signing.accept_unsigned musi być bool")
        state_file = sig.get("state_file", f"/var/lib/srp/mavlink/{name}.ts")
        if not isinstance(state_file, str) or not state_file:
            raise MavlinkModelError(f"{where}: signing.state_file musi być niepustym stringiem")
        link.signing = Signing(enabled, key_file,
                               _int(sig.get("link_id", 0), 0, 255, where, "signing.link_id"),
                               accept, state_file)
    return link


def ParseAppConfig(raw, app_name: str) -> MavlinkAppConfig:
    where = f"{app_name}.mavlink"
    if not isinstance(raw, dict):
        raise MavlinkModelError(f"{where}: brak/niepoprawny blok 'mavlink' aplikacji")
    for key in raw:
        if key not in APP_MAVLINK_KEYS:
            raise MavlinkModelError(
                f"{where}: nieznane pole '{key}' (dozwolone: {', '.join(APP_MAVLINK_KEYS)})")
    dialect = raw.get("dialect")
    if not isinstance(dialect, str) or not _NAME_RE.match(dialect):
        raise MavlinkModelError(f"{where}: 'dialect' musi pasować do [A-Za-z][A-Za-z0-9_]*")
    return MavlinkAppConfig(_int(raw.get("system_id", 1), 1, 255, where, "system_id"),
                            _int(raw.get("component_id", 1), 1, 255, where, "component_id"),
                            dialect)


def ParseLinks(raw, app_name: str) -> dict[str, MavlinkLink]:
    if not isinstance(raw, dict) or not raw:
        raise MavlinkModelError(f"{app_name}: 'mavlink_links' musi być niepustym obiektem")
    links = {n: ParseLink(n, r, app_name) for n, r in raw.items()}
    ids: dict[int, str] = {}
    for n, link in links.items():
        if link.signing.enabled:
            if link.signing.link_id in ids:
                raise MavlinkModelError(
                    f"{app_name}.mavlink_links.{n}: signing.link_id={link.signing.link_id} "
                    f"powtórzony (łącze {ids[link.signing.link_id]}); link_id musi być "
                    "różny dla każdego łącza")
            ids[link.signing.link_id] = n
    return links


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


def ParseServiceItem(item: dict, name: str, links: dict[str, MavlinkLink],
                     app_name: str, service=None, direction: str = "kOut") -> MavlinkItem:
    where = f"{app_name}.{name}"
    for key in item:
        if key not in MAVLINK_ITEM_KEYS:
            raise MavlinkModelError(
                f"{where}: nieznane pole '{key}' we wpisie on=mavlink "
                f"(dozwolone: {', '.join(MAVLINK_ITEM_KEYS)})")
    sel = item.get("links")
    if not isinstance(sel, list) or not sel or not all(isinstance(x, str) for x in sel):
        raise MavlinkModelError(f"{where}: on=mavlink wymaga niepustej listy 'links'")
    if len(set(sel)) != len(sel):
        raise MavlinkModelError(
            f"{where}: powtórzone łącze w 'links': {sorted({x for x in sel if sel.count(x) > 1})}")
    for ln in sel:
        if ln not in links:
            raise MavlinkModelError(
                f"{where}: nieznane łącze '{ln}' w 'links' (zdefiniowane: "
                f"{', '.join(links) or 'brak'})")
    # rev. 4 pkt 1: dedup/replay są wspólne dla łączy instancji -> ten sam tryb podpisu.
    modes = {ln: (links[ln].signing.enabled, links[ln].signing.accept_unsigned) for ln in sel}
    if len(set(modes.values())) > 1:
        desc = ", ".join(f"{ln}: enabled={m[0]} accept_unsigned={m[1]}"
                         for ln, m in modes.items())
        raise MavlinkModelError(
            f"{where}: łącza jednej instancji muszą mieć identyczne signing.enabled i "
            f"signing.accept_unsigned ({desc})")
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


def UnusedLinks(links: dict[str, MavlinkLink], items: list[MavlinkItem]) -> list[str]:
    used = {ln for it in items for ln in it.links}
    return [n for n in links if n not in used]


def WarnUnused(app_name: str, links, items) -> None:
    for n in UnusedLinks(links, items):
        print(f"OSTRZEŻENIE mavlink: {app_name}: łącze '{n}' nie jest używane przez "
              "żadną usługę", file=sys.stderr)
