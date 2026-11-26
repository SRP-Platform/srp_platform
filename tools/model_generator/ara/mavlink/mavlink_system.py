"""System generator: the dialects of several CPUs -> ONE dialect for the ground station.

A rocket with several computers has one MavlinkDaemon (one radio) per CPU. Each CPU has its own
dialect (`ara_mavlink_dialect`); the station needs one that parses the frames of all of them.
This module merges the dialect XML files of the CPUs:

  * the same `msg_id` with the same definition (name and fields) on several CPUs is the same
    service and is emitted once; the same `msg_id` with different definitions is an error that
    names both CPUs and both messages; the same message name with different ids is an error too;
  * the (`system_id`, `component_id`) pairs of the daemon configs must be unique: the station
    tells the CPUs apart by `compid` of a frame and addresses them by `target_component`.
    Different `system_id`s are allowed but warned about (one rocket = one system_id).

Usage: mavlink_system.py <out_dir> --dialect NAME --cpu NAME DIALECT_XML LINKS_MD DAEMON_CONFIG MONITOR_JSON ...
Writes <out_dir>/<dialect>.xml, <dialect>_links.md, <dialect>.lua, <dialect>.py, <dialect>_monitor.py
and <dialect>_monitor.json; the monitor of the system tells the CPUs apart by (system_id, component_id).
"""
from __future__ import annotations

import argparse
import json
import os
import re
import sys
import xml.etree.ElementTree as ET
from typing import NamedTuple

from tools.model_generator.ara.common.model_error import ModelError
from tools.model_generator.ara.mavlink.mavlink_gen import RenderXml, RunMavgen
from tools.model_generator.ara.mavlink.monitor_meta import (
    BuildMeta, RenderMetaJson, RenderMonitorPy)
from tools.model_generator.ara.mavlink.mavlink_model import (
    BudgetInfoLine, DaemonConfig, LoadDaemonConfig, MavlinkModelError)


class CpuInput(NamedTuple):
    name: str
    xml: str  # <dialect>.xml of the CPU
    links_md: str  # <dialect>_links.md of the CPU
    daemon_config: str  # mavlink_daemon_config.json of the CPU
    monitor_meta: str  # <dialect>_monitor.json of the CPU


class _Field(NamedTuple):
    name: str
    mav_type: str
    array_len: int

    def Text(self) -> str:
        return f"{self.mav_type}[{self.array_len}] {self.name}" if self.array_len else (
            f"{self.mav_type} {self.name}")


class _Message(NamedTuple):
    """What `RenderXml` needs; `cpu` is only for error messages."""
    msg_id: int
    name: str
    desc: str
    fields: tuple
    cpu: str

    def Signature(self) -> tuple:
        """The wire definition: the description does not change a frame."""
        return (self.name, tuple((f.name, f.mav_type, f.array_len) for f in self.fields))

    def Describe(self) -> str:
        return f"{self.name} ({', '.join(f.Text() for f in self.fields)})"


def ParseDialect(cpu: str, path: str) -> list[_Message]:
    """The messages of a CPU dialect XML written by `RenderXml`."""
    try:
        root = ET.parse(path).getroot()
    except (OSError, ET.ParseError) as e:
        raise MavlinkModelError(f"CPU '{cpu}': cannot read the dialect {path} ({e!r})")
    out = []
    for m in root.iter("message"):
        fields = []
        for f in m.iter("field"):
            ty, _, rest = f.get("type", "").partition("[")
            fields.append(_Field(f.get("name", ""), ty, int(rest.rstrip("]")) if rest else 0))
        desc = m.findtext("description") or ""
        out.append(_Message(int(m.get("id", "-1")), m.get("name", ""), desc, tuple(fields), cpu))
    return out


def CheckIdentities(cpus: list[CpuInput]) -> tuple[dict[str, DaemonConfig], list[str]]:
    """Loads the daemon configs; every (system_id, component_id) must be unique.

    Returns the configs by CPU name and the warnings."""
    configs: dict[str, DaemonConfig] = {}
    owner: dict[tuple[int, int], str] = {}
    warnings: list[str] = []
    for c in cpus:
        cfg = LoadDaemonConfig(c.daemon_config)
        if cfg.system_id is None or cfg.component_id is None:
            raise MavlinkModelError(
                f"CPU '{c.name}': {c.daemon_config} has no system_id/component_id; the daemon "
                f"needs both and the system generator tells the CPUs apart by them")
        ident = (cfg.system_id, cfg.component_id)
        if ident in owner:
            raise MavlinkModelError(
                f"CPU '{c.name}' and CPU '{owner[ident]}' have the same system_id={ident[0]} "
                f"component_id={ident[1]} in their daemon configs; the station could not tell "
                f"their frames apart or address a request to one of them (use one system_id "
                f"per rocket and a separate component_id per CPU)")
        owner[ident] = c.name
        configs[c.name] = cfg
    if len({cfg.system_id for cfg in configs.values()}) > 1:
        desc = ", ".join(f"{n}: {cfg.system_id}" for n, cfg in configs.items())
        warnings.append(f"the CPUs have different system_id ({desc}); the convention is one "
                        f"system_id per rocket and a separate component_id per CPU")
    return configs, warnings


def MergeMessages(per_cpu: dict[str, list[_Message]]) -> list[_Message]:
    """One list of messages: equal definitions merge, different ones are an error."""
    by_id: dict[int, _Message] = {}
    by_name: dict[str, _Message] = {}
    out: list[_Message] = []
    for cpu, msgs in per_cpu.items():
        for m in msgs:
            prior = by_id.get(m.msg_id)
            if prior is not None:
                if prior.Signature() != m.Signature():
                    raise MavlinkModelError(
                        f"msg_id {m.msg_id} collision between CPUs: CPU '{prior.cpu}' has "
                        f"{prior.Describe()}, CPU '{cpu}' has {m.Describe()}; the same msg_id "
                        f"must mean the same message on every CPU of the system")
                continue
            named = by_name.get(m.name)
            if named is not None:
                raise MavlinkModelError(
                    f"message name {m.name} collision between CPUs: msg_id {named.msg_id} on CPU "
                    f"'{named.cpu}' and msg_id {m.msg_id} on CPU '{cpu}'")
            by_id[m.msg_id] = by_name[m.name] = m
            out.append(m)
    return out


def RenderSystemLinksMd(dialect: str, cpus: list[CpuInput],
                        configs: dict[str, DaemonConfig]) -> str:
    """The addressing table of the system, then the links document of every CPU."""
    o = [f"# MAVLink system: dialect `{dialect}`", "",
         "## CPUs and addressing", "",
         "The station tells the CPUs apart by `compid` of a frame and addresses a request to one "
         "CPU with `target_system` / `target_component` (a CPU ignores a request addressed to "
         "another one).", "",
         "| CPU | system_id | component_id | links |", "|---|---|---|---|"]
    for c in cpus:
        cfg = configs[c.name]
        o.append(f"| {c.name} | {cfg.system_id} | {cfg.component_id} | "
                 f"{', '.join(cfg.links) or '-'} |")
    for c in cpus:
        with open(c.links_md, encoding="utf-8") as f:
            text = f.read().rstrip("\n").splitlines()
        o += ["", f"## CPU `{c.name}`", ""]
        for line in text:
            if line.startswith("# "):
                continue  # the title of the CPU document
            o.append("#" + line if line.startswith("#") else line)
    return "\n".join(o) + "\n"


def CpuBudgetInfo(cpu: CpuInput, cfg: DaemonConfig) -> str:
    """One INFO line per CPU: the `% of air_rate_bps` of every link, read from the `sum of events`
    row of the CPU's `_links.md` (the same numbers as the generator of the CPU prints)."""
    with open(cpu.links_md, encoding="utf-8") as f:
        text = f.read()
    shares: dict[str, tuple[float | None, float]] = {}
    for name, link in cfg.links.items():
        util = None
        if link.air_rate_bps:
            sec = text.split(f"### Link `{name}`:", 1)
            row = re.search(r"^\| \*\*sum of events\*\* .*\| ([0-9.]+)% \|$",
                            sec[1].split("\n### ", 1)[0] if len(sec) > 1 else "", re.M)
            util = float(row.group(1)) / 100 if row else 0.0
        shares[name] = (util, link.max_utilization)
    return BudgetInfoLine(shares).replace("budget: ", f"budget: CPU {cpu.name}: ", 1)


class SystemResult:
    def __init__(self, xml: str, links_md: str, warnings: list[str],
                 infos: list[str] | None = None, monitor_meta: dict | None = None) -> None:
        self.xml = xml
        self.links_md = links_md
        self.monitor_meta = monitor_meta or {}
        self.warnings = warnings
        self.infos = infos or []


def _cpu_meta(cpu: CpuInput) -> dict:
    """The monitor metadata of one CPU, named as the system names it."""
    try:
        with open(cpu.monitor_meta, encoding="utf-8") as f:
            cpus = json.load(f)["cpus"]
    except (OSError, ValueError, KeyError) as e:
        raise MavlinkModelError(f"CPU '{cpu.name}': cannot read the monitor metadata "
                                f"{cpu.monitor_meta} ({e!r})")
    return dict(cpus[0], name=cpu.name)


def GenerateSystem(dialect: str, cpus: list[CpuInput]) -> SystemResult:
    if len({c.name for c in cpus}) != len(cpus):
        raise MavlinkModelError("two CPUs of the system have the same name")
    configs, warnings = CheckIdentities(cpus)
    merged = MergeMessages({c.name: ParseDialect(c.name, c.xml) for c in cpus})
    infos = [CpuBudgetInfo(c, configs[c.name]) for c in cpus if configs[c.name].links]
    meta = BuildMeta(dialect, [_cpu_meta(c) for c in cpus])
    return SystemResult(RenderXml(merged), RenderSystemLinksMd(dialect, cpus, configs), warnings,
                        infos, meta)


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("out_dir")
    ap.add_argument("--dialect", required=True)
    ap.add_argument("--cpu", nargs=5, action="append", required=True,
                    metavar=("NAME", "DIALECT_XML", "LINKS_MD", "DAEMON_CONFIG", "MONITOR_JSON"))
    args = ap.parse_args(argv)
    try:
        res = GenerateSystem(args.dialect, [CpuInput(*c) for c in args.cpu])
        for i in res.infos:
            print(i, file=sys.stderr)
        for w in res.warnings:
            print(f"WARNING mavlink: {w}", file=sys.stderr)
        base = os.path.join(args.out_dir, args.dialect)
        for suffix, text in ((".xml", res.xml), ("_links.md", res.links_md),
                             ("_monitor.json", RenderMetaJson(res.monitor_meta)),
                             ("_monitor.py", RenderMonitorPy(res.monitor_meta))):
            with open(base + suffix, "w", encoding="utf-8") as f:
                f.write(text)
        RunMavgen(base + ".xml", base + ".lua", base + ".py")
    except ModelError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
