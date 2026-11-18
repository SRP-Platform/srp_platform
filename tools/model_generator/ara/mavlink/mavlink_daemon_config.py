"""Konfiguracja MavlinkDaemona (`mavlink_daemon_config.json`): schemat i walidacja (rev. 10).

Daemon jest jedynym właścicielem łączy: `system_id`, `component_id` (1 na CPU), łącza
(`driver`, `driver_config`, `version`, `heartbeat_hz`, `signing`). Aplikacje `on: mavlink`
znają tylko NAZWY łączy. Walidacja pilnuje tego samego schematu co parser daemona
(`platform/common/mavlink_daemon/code/config`), żeby błąd configu wyszedł przy generacji,
a nie przy starcie na urządzeniu. Moduł zależy tylko od stdlib i `mavlink_errors`.
"""
from __future__ import annotations

import json
import math
import re

from tools.model_generator.ara.mavlink.mavlink_errors import MavlinkModelError

DAEMON_KEYS = ("system_id", "component_id", "socket_path", "socket_mode", "driver_dir",
               "restart_delay_ms", "links")
LINK_KEYS = ("driver", "driver_config", "version", "heartbeat_hz", "signing")
SIGNING_KEYS = ("enabled", "key_file", "link_id", "accept_unsigned", "state_file")
# Pola łącza, które należały do aplikacji (rev. 4-8): w configu daemona to literówka/pomyłka.
OLD_LINK_KEYS = ("transport", "device", "baudrate", "bind", "peer", "max_bytes_per_s")

_NAME_RE = re.compile(r"^[A-Za-z][A-Za-z0-9_]*$")
# jak parser daemona (`daemon_config.cc`): cyfry ósemkowe, wartość <= 0777
_OCTAL_MODE_RE = re.compile(r"0*[0-7]{1,3}")


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


class DaemonLink:
    def __init__(self, name: str) -> None:
        self.name = name
        self.driver = ""
        self.driver_config = ""
        self.version = 2
        self.heartbeat_hz = 1
        self.signing = Signing()


class DaemonConfig:
    def __init__(self, system_id: int, component_id: int, links: dict[str, DaemonLink]) -> None:
        self.system_id = system_id
        self.component_id = component_id
        self.links = links


def _parse_signing(raw, link: DaemonLink, where: str) -> Signing:
    if not isinstance(raw, dict):
        raise MavlinkModelError(f"{where}: 'signing' musi być obiektem")
    for key in raw:
        if key not in SIGNING_KEYS:
            raise MavlinkModelError(
                f"{where}: signing.{key} nieznane (dozwolone: {', '.join(SIGNING_KEYS)})")
    enabled = raw.get("enabled", False)
    if not isinstance(enabled, bool):
        raise MavlinkModelError(f"{where}: signing.enabled musi być bool")
    if enabled and link.version == 1:
        raise MavlinkModelError(f"{where}: signing wymaga MAVLink v2 (łącze ma version=1)")
    key_file = raw.get("key_file", "")
    if enabled and (not isinstance(key_file, str) or not key_file):
        raise MavlinkModelError(f"{where}: signing.enabled wymaga 'key_file'")
    accept = raw.get("accept_unsigned", False)
    if not isinstance(accept, bool):
        raise MavlinkModelError(f"{where}: signing.accept_unsigned musi być bool")
    state_file = raw.get("state_file", "")
    if not isinstance(state_file, str):
        raise MavlinkModelError(f"{where}: signing.state_file musi być stringiem")
    return Signing(enabled, key_file, _int(raw.get("link_id", 0), 0, 255, where, "signing.link_id"),
                   accept, state_file)


def ParseLink(name: str, raw, where: str) -> DaemonLink:
    where = f"{where}.links.{name}"
    if not _NAME_RE.match(name):
        raise MavlinkModelError(f"{where}: nazwa łącza musi pasować do [A-Za-z][A-Za-z0-9_]*")
    if not isinstance(raw, dict):
        raise MavlinkModelError(f"{where}: łącze musi być obiektem")
    link = DaemonLink(name)
    for key in raw:
        if key in OLD_LINK_KEYS:
            raise MavlinkModelError(
                f"{where}: pole '{key}' nie należy do łącza — urządzenie i jego parametry "
                "są w pliku drivera (`driver_config`)")
        if key not in LINK_KEYS:
            raise MavlinkModelError(
                f"{where}: nieznane pole '{key}' (dozwolone: {', '.join(LINK_KEYS)})")
    driver = raw.get("driver")
    if not isinstance(driver, str) or not _NAME_RE.match(driver):
        raise MavlinkModelError(f"{where}: 'driver' (id drivera) musi pasować do "
                                "[A-Za-z][A-Za-z0-9_]*")
    link.driver = driver
    dc = raw.get("driver_config", "")
    if not isinstance(dc, str):
        raise MavlinkModelError(f"{where}: 'driver_config' musi być ścieżką (string)")
    link.driver_config = dc
    link.version = raw.get("version", 2)
    if isinstance(link.version, bool) or link.version not in (1, 2):
        raise MavlinkModelError(f"{where}: version={link.version!r} (dozwolone: 1, 2)")
    hb = raw.get("heartbeat_hz", 1)
    if (isinstance(hb, bool) or not isinstance(hb, (int, float)) or not math.isfinite(hb)
            or hb < 0 or hb > 100):
        raise MavlinkModelError(f"{where}: heartbeat_hz={hb!r} poza zakresem 0..100")
    link.heartbeat_hz = hb
    if "signing" in raw:
        link.signing = _parse_signing(raw["signing"], link, where)
    return link


def ParseDaemonConfig(raw, where: str = "mavlink_daemon") -> DaemonConfig:
    if not isinstance(raw, dict):
        raise MavlinkModelError(f"{where}: konfiguracja daemona musi być obiektem")
    for key in raw:
        if key not in DAEMON_KEYS:
            raise MavlinkModelError(
                f"{where}: nieznane pole '{key}' (dozwolone: {', '.join(DAEMON_KEYS)})")
    for key in ("system_id", "component_id"):
        if key not in raw:
            raise MavlinkModelError(f"{where}: brak pola '{key}'")
    sys_id = _int(raw["system_id"], 1, 255, where, "system_id")
    comp_id = _int(raw["component_id"], 1, 255, where, "component_id")
    for key in ("socket_path", "socket_mode", "driver_dir"):
        if key in raw and not isinstance(raw[key], str):
            raise MavlinkModelError(f"{where}: '{key}' musi być stringiem")
    if "socket_mode" in raw and not _OCTAL_MODE_RE.fullmatch(raw["socket_mode"]):
        raise MavlinkModelError(
            f"{where}: socket_mode '{raw['socket_mode']}' nie jest prawami ósemkowymi (np. \"0660\")")
    if "restart_delay_ms" in raw:
        _int(raw["restart_delay_ms"], 0, 3_600_000, where, "restart_delay_ms")
    links_raw = raw.get("links")
    if not isinstance(links_raw, dict):
        raise MavlinkModelError(f"{where}: 'links' musi być obiektem (nazwa łącza -> opis)")
    links = {n: ParseLink(n, r, where) for n, r in links_raw.items()}
    ids: dict[int, str] = {}
    for n, link in links.items():
        if link.signing.enabled:
            if link.signing.link_id in ids:
                raise MavlinkModelError(
                    f"{where}.links.{n}: signing.link_id={link.signing.link_id} powtórzony "
                    f"(łącze {ids[link.signing.link_id]}); link_id musi być różny dla każdego łącza")
            ids[link.signing.link_id] = n
    return DaemonConfig(sys_id, comp_id, links)


def LoadDaemonConfig(path: str) -> DaemonConfig:
    try:
        with open(path, encoding="utf-8") as f:
            raw = json.load(f)
    except (OSError, ValueError) as e:
        raise MavlinkModelError(f"{path}: nie można wczytać configu daemona ({e})")
    return ParseDaemonConfig(raw, path)
