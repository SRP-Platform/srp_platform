"""CLI: model -> <dialect>.xml (+ <dialect>_links.md).

Dwa tryby wejścia:
  * model aplikacji (adaptive_application z usługami on=mavlink): dialekt, wersje i
    łącza pochodzą z aplikacji; --dialect (jeśli podany) musi się zgadzać;
  * same service.json: wymagane --dialect, opcjonalnie --version (domyślnie 2).

Użycie: mavlink_gen.py <out.xml> [--links-md <out.md>] [--dialect N] [--version 1|2]
        [--allowed-versions 1,2] model.json...
Błąd modelu = kod wyjścia 1 i komunikat z nazwą aplikacji/usługi/elementu na stderr.
"""
from __future__ import annotations

import argparse
import sys

from tools.model_generator.ara.app.adaptive_application_db import AdaptiveApplicationDb
from tools.model_generator.ara.common.common_parser import CommonParser
from tools.model_generator.ara.mavlink.mavlink_app import AppMessages, RenderLinksMd
from tools.model_generator.ara.mavlink.mavlink_collisions import (
    CollisionWarnings, PymavlinkIdIndex)
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


def GenerateAll(model_paths: list[str], dialect: str | None = None,
                version: int = 2) -> Result:
    for p in model_paths:
        CommonParser.LoadJson(p)
    apps = [a for a in AdaptiveApplicationDb().app_list.values() if a.GetMavlinkEntries()]
    if len(apps) > 1:
        raise MavlinkModelError("model zawiera kilka aplikacji z on=mavlink: "
                                + ", ".join(a.name for a in apps))
    if apps:
        app = apps[0]
        if dialect is not None and dialect != app.mavlink_cfg.dialect:
            raise MavlinkModelError(
                f"{app.name}: dialect aplikacji '{app.mavlink_cfg.dialect}' != "
                f"zażądany '{dialect}'")
        entries = app.GetMavlinkEntries()
        msgs = AppMessages(app.name, entries, app.mavlink_links)
        used = sorted({app.mavlink_links[n].version for e in entries for n in e[4].links})
        _warn_collisions(msgs)
        md = RenderLinksMd(app.name, app.mavlink_cfg, app.mavlink_links, entries, msgs)
        return Result(RenderXml(app.mavlink_cfg.dialect, msgs), md, used)
    if dialect is None:
        raise MavlinkModelError("brak aplikacji z on=mavlink: podaj --dialect")
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
    ap.add_argument("--version", type=int, choices=(1, 2), default=2)
    ap.add_argument("models", nargs="+")
    args = ap.parse_args(argv)
    allowed = {int(x) for x in args.allowed_versions.split(",")}
    try:
        res = GenerateAll(args.models, args.dialect, args.version)
        extra = set(res.versions) - allowed
        if extra:
            raise MavlinkModelError(
                f"łącza używają MAVLink v{sorted(extra)}, a reguła bazela generuje "
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
