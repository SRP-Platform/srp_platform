"""Verdict of the soak test of `//tests/mavlink/udp_cpu`: the station summary + the CSV of the CPU.

Takes the JSON of `soak_station --summary` (events, seq, requests, channel), the CSV of
`soak_cpu.sh` (the daemon on the CPU: PID, restarts, RSS, the load, temperature) and the logs
(`--log FILE`: the DLT log of the daemon and `srp_start.log`, a line with the word ERROR is a
failure), checks the thresholds and prints the verdict (exit code 0 = PASS).

    soak_verdict --summary station.json --csv soak.csv --phases baseline:600,load:10800,cooldown:600
"""
# pylint: disable=import-error,too-many-locals,missing-function-docstring
# pylint: disable=invalid-name,too-few-public-methods,too-many-branches,too-many-arguments
# pylint: disable=too-many-positional-arguments,missing-class-docstring
from __future__ import annotations

import argparse
import csv
import json
import re
import statistics
import sys

import soak_station as ss

ERROR_LINE = re.compile(r"\bERROR\b")
DONE = "SOAK_CPU_DONE"


def ReadCsv(path: str) -> tuple[list[dict], bool]:
    """The numeric rows of the CSV of `soak_cpu.sh` and whether it ends with SOAK_CPU_DONE."""
    rows, done = [], False
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line == DONE:
                done = True
            elif line[:1].isdigit():
                rows.append(line)
    header = "t_s,epoch,hogs,pid,restarts,rss_kb,daemon_cpu,cpu_total,load1,temp_mc,freq_khz"
    out = []
    for rec in csv.DictReader([header] + rows):
        out.append({k: float(v) for k, v in rec.items()})
    return out, done


def PhaseRows(rows: list[dict], phases: list[tuple[str, float]], name: str,
              skip_s: float = 0.0) -> list[dict]:
    """The samples of a phase, without the first `skip_s` seconds of it."""
    start = 0.0
    for ph, secs in phases:
        if ph == name:
            return [r for r in rows if start + skip_s <= r["t_s"] < start + secs]
        start += secs
    return []


def Evaluate(summary: dict, rows: list[dict], done: bool, phases: list[tuple[str, float]],
             args, logs: dict[str, int | None]) -> dict:
    checks = list(summary["checks"])

    def Check(name: str, ok: bool, value, limit) -> None:
        checks.append({"name": name, "ok": bool(ok), "value": value, "limit": limit})

    Check("the sampler on the CPU finished (SOAK_CPU_DONE)", done and bool(rows), len(rows),
          "SOAK_CPU_DONE")
    report: dict = {}
    if rows:
        down = sum(1 for r in rows if r["pid"] == 0)
        restarts = int(rows[-1]["restarts"])
        Check("the daemon PID stays the same (restarts)", restarts <= args.max_restarts and not down,
              f"{restarts} restarts, {down} samples without the daemon", f"<= {args.max_restarts}")
        base_rows = PhaseRows(rows, phases, "baseline") or rows[:3]
        base = statistics.median(r["rss_kb"] for r in base_rows)
        tail = rows[-max(1, min(5, len(rows) // 2)):]
        end = statistics.median(r["rss_kb"] for r in tail)
        grow = end - base
        pct = grow / base * 100.0 if base else 0.0
        report["rss_kb"] = {"baseline": base, "end": end, "max": max(r["rss_kb"] for r in rows)}
        Check("RSS growth of the daemon", grow <= args.max_rss_growth_kb and pct <= args.max_rss_growth_pct,
              f"{grow:+.0f} kB ({pct:+.1f} %)",
              f"<= {args.max_rss_growth_kb:g} kB and <= {args.max_rss_growth_pct:g} %")
        load = PhaseRows(rows, phases, "load", skip_s=args.interval_skip)
        if load:
            mean = statistics.mean(r["cpu_total"] for r in load)
            report["cpu_total_load_mean"] = round(mean, 1)
            Check("CPU busy under the load (all cores)", mean >= args.min_cpu_total and
                  all(r["hogs"] > 0 for r in load), f"{mean:.1f} %", f">= {args.min_cpu_total:g} %")
            report["daemon_cpu_load_mean"] = round(statistics.mean(r["daemon_cpu"] for r in load), 1)
        else:
            Check("CPU busy under the load (all cores)", False, "no samples in the load phase",
                  f">= {args.min_cpu_total:g} %")
        peak = max(r["temp_mc"] for r in rows) / 1000.0
        report["temp_max_c"] = peak
        Check("temperature", peak < args.max_temp_c, f"{peak:.1f} C", f"< {args.max_temp_c:g} C")
        freqs = [r["freq_khz"] for r in rows if r["freq_khz"] > 0]
        if freqs:
            report["freq_khz"] = {"min": min(freqs), "max": max(freqs)}
            report["throttling_note"] = min(freqs) < 0.9 * max(freqs)
    for name, count in logs.items():
        if count is None:
            Check(f"no ERROR line in {name}", True, "SKIP: no such log", "0")
        else:
            Check(f"no ERROR line in {name}", count == 0, count, "0")
    return {"checks": checks, "cpu": report,
            "verdict": "PASS" if all(c["ok"] for c in checks) else "FAIL"}


def CountErrors(path: str) -> int | None:
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            return sum(1 for line in f if ERROR_LINE.search(line))
    except OSError:
        return None


def Parse(argv) -> argparse.Namespace:
    ap = argparse.ArgumentParser(description=__doc__.split("\n", maxsplit=1)[0])
    ap.add_argument("--summary", required=True, help="JSON of soak_station --summary")
    ap.add_argument("--csv", required=True, help="CSV of soak_cpu.sh")
    ap.add_argument("--phases", required=True, help="name:seconds,... as given to soak_station")
    ap.add_argument("--log", action="append", default=[], help="a log to scan for ERROR (repeatable)")
    ap.add_argument("--out", help="the verdict as JSON")
    ap.add_argument("--max-restarts", type=int, default=0)
    ap.add_argument("--max-rss-growth-kb", type=float, default=5120.0)
    ap.add_argument("--max-rss-growth-pct", type=float, default=10.0)
    ap.add_argument("--min-cpu-total", type=float, default=95.0, help="mean busy %% of all cores")
    ap.add_argument("--max-temp-c", type=float, default=80.0)
    ap.add_argument("--interval-skip", type=float, default=60.0,
                    help="seconds of the load phase before the load is counted")
    return ap.parse_args(argv)


def main(argv=None) -> int:
    args = Parse(argv)
    with open(args.summary, encoding="utf-8") as f:
        summary = json.load(f)
    rows, done = ReadCsv(args.csv)
    result = Evaluate(summary, rows, done, ss.ParsePhases(args.phases), args,
                      {path: CountErrors(path) for path in args.log})
    for c in result["checks"]:
        print(f"{'ok  ' if c['ok'] else 'FAIL'} {c['name']}: {c['value']} (limit {c['limit']})")
    print(f"cpu: {json.dumps(result['cpu'])}")
    print(result["verdict"])
    if args.out:
        with open(args.out, "w", encoding="utf-8") as f:
            json.dump(result | {"station": summary}, f, indent=1)
    return 0 if result["verdict"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
