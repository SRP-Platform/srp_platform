"""CLI: modele aplikacji CPU -> <dialect>.xml (+ <dialect>_links.md).

Dialekt, XML, Lua i pymavlink są generowane per CPU: wejściem są modele WSZYSTKICH aplikacji
z `on: mavlink` jednego CPU (jeden MavlinkDaemon). Kolizja msg_id między aplikacjami = błąd.
Opcjonalny `--daemon-config` (mavlink_daemon_config.json) włącza walidację nazw łączy,
wersji MAVLink (v1: ID <= 255) i trybu podpisu oraz parametry łączy w `<dialect>_links.md`.

Dwa tryby wejścia:
  * modele aplikacji (adaptive_application z usługami on=mavlink): wymagane --dialect;
  * same service.json: wymagane --dialect, opcjonalnie --version (domyślnie 2).

Użycie: mavlink_gen.py <out.xml> --dialect N [--links-md <out.md>] [--daemon-config cfg.json]
        [--version 1|2] [--allowed-versions 1,2] model.json...
Błąd modelu = kod wyjścia 1 i komunikat z nazwą aplikacji/usługi/elementu na stderr.
"""
from __future__ import annotations

import argparse
import sys

from tools.model_generator.ara.app.adaptive_application_db import AdaptiveApplicationDb
from tools.model_generator.ara.common.common_parser import CommonParser
from tools.model_generator.ara.mavlink.mavlink_app import (
    CheckCpu, CpuEntries, CpuMessages, RenderLinksMd, UnusedLinkWarnings)
from tools.model_generator.ara.mavlink.mavlink_collisions import (
    CollisionWarnings, PymavlinkIdIndex)
from tools.model_generator.ara.mavlink.mavlink_daemon_config import (
    DaemonConfig, LoadDaemonConfig)
from tools.model_generator.ara.mavlink.mavlink_dialect import (
    BuildMessages, RenderXml, Validate)
from tools.model_generator.ara.mavlink.mavlink_errors import MavlinkModelError
from tools.model_generator.ara.someip.lib.someip_db import SomeIpDb


class Result:
    def __init__(self, xml: str, links_md: str, versions: list[int]) -> None:
        self.xml = xml
        self.links_md = links_md
        self.versions = versions  # wersje MAVLink użyte przez łącza usług


def _warn_collisions(msgs) -> None:
    for w in CollisionWarnings(msgs, PymavlinkIdIndex()):
        print(f"OSTRZEŻENIE mavlink: {w}", file=sys.stderr)


def GenerateAll(model_paths: list[str], dialect: str | None = None, version: int = 2,
                daemon_config: "str | DaemonConfig | None" = None) -> Result:
    loaded: set = set()  # wspólne include kilku aplikacji CPU są wczytywane raz
    for p in model_paths:
        CommonParser.LoadJson(p, loaded)
    daemon = LoadDaemonConfig(daemon_config) if isinstance(daemon_config, str) else daemon_config
    apps = [a for a in AdaptiveApplicationDb().app_list.values() if a.GetMavlinkEntries()]
    if dialect is None:
        raise MavlinkModelError("podaj --dialect (nazwa dialektu CPU)")
    if apps:
        entries = CpuEntries(apps)
        CheckCpu(entries, daemon)
        for w in UnusedLinkWarnings(entries, daemon):
            print(f"OSTRZEŻENIE mavlink: {w}", file=sys.stderr)
        msgs, used = CpuMessages(entries, daemon)
        _warn_collisions(msgs)
        md = RenderLinksMd(dialect, daemon, entries, msgs)
        return Result(RenderXml(dialect, msgs), md, used)
    messages = []
    for service in SomeIpDb().service_list.values():
        messages.extend(BuildMessages(service, version))
    if not messages:
        raise MavlinkModelError("w modelu nie ma żadnego elementu z blokiem 'mavlink'")
    Validate(messages, version)
    _warn_collisions(messages)
    return Result(RenderXml(dialect, messages), "", [version])


def Generate(model_paths: list[str], dialect: str | None = None, version: int = 2) -> str:
    return GenerateAll(model_paths, dialect, version).xml


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--links-md")
    ap.add_argument("--allowed-versions", default="1,2")
    ap.add_argument("--dialect")
    ap.add_argument("--daemon-config")
    ap.add_argument("--version", type=int, choices=(1, 2), default=2)
    ap.add_argument("models", nargs="+")
    args = ap.parse_args(argv)
    allowed = {int(x) for x in args.allowed_versions.split(",")}
    try:
        res = GenerateAll(args.models, args.dialect, args.version, args.daemon_config)
        extra = set(res.versions) - allowed
        if extra:
            raise MavlinkModelError(
                f"łącza daemona używają MAVLink v{sorted(extra)}, a reguła bazela generuje "
                f"tylko v{sorted(allowed)} (atrybut 'versions')")
    except MavlinkModelError as e:
        print(f"BŁĄD mavlink: {e}", file=sys.stderr)
        return 1
    with open(args.out, "w", encoding="utf-8") as f:
        f.write(res.xml)
    if args.links_md:
        with open(args.links_md, "w", encoding="utf-8") as f:
            f.write(res.links_md or "# Łącza MAVLink\n\n(brak aplikacji w modelu)\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
