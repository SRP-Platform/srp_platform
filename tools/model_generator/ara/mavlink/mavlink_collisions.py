"""Ostrzeżenia o kolizjach ID wiadomości z dialektami dostarczanymi z pymavlink.

ID wybiera użytkownik (rev. 4, pkt 14): kolizja z `common`, `ardupilotmega` itp. nie jest
błędem (własny dialekt jest osobny), ale GS ładujący kilka dialektów naraz zobaczy konflikt,
więc generator ostrzega. Bez pymavlink (np. goły unittest) indeks jest pusty.
"""
from __future__ import annotations

import functools
import glob
import os
import xml.etree.ElementTree as ET

from tools.model_generator.ara.mavlink.mavlink_dialect import Message


@functools.lru_cache(maxsize=1)
def PymavlinkIdIndex() -> dict[int, list[str]]:
    try:
        import pymavlink
    except ImportError:
        return {}
    base = os.path.dirname(pymavlink.__file__)
    dirs = [os.path.join(base, "message_definitions", "v1.0"),
            os.path.join(os.path.dirname(base), "message_definitions", "v1.0")]
    index: dict[int, list[str]] = {}
    for d in dirs:
        for path in sorted(glob.glob(os.path.join(d, "*.xml"))):
            dialect = os.path.splitext(os.path.basename(path))[0]
            if dialect == "minimal":
                continue  # HEARTBEAT=0 jest błędem walidacji, nie ostrzeżeniem
            try:
                root = ET.parse(path).getroot()
            except ET.ParseError:
                continue
            for m in root.iter("message"):
                try:
                    mid = int(m.get("id"))
                except (TypeError, ValueError):
                    continue
                index.setdefault(mid, []).append(f"{dialect}:{m.get('name')}")
        if index:
            break
    return index


def CollisionWarnings(msgs: list[Message], index: dict[int, list[str]]) -> list[str]:
    out = []
    for m in sorted(msgs, key=lambda x: x.msg_id):
        hits = index.get(m.msg_id)
        if hits:
            shown = ", ".join(hits[:4]) + (" ..." if len(hits) > 4 else "")
            out.append(f"{m.where}: ID {m.msg_id} ({m.name}) koliduje z dialektami pymavlink: "
                       f"{shown}")
    return out
