"""The CPU of the soak test on this host (`soak_run --local`): the daemon with the UDP driver and the
two example applications, on the config of `//tests/mavlink/udp_cpu` with the loopback as the PC.

Writes `local.env` into the work directory (the ports and the PID of the daemon, to be sourced by
`soak_run`) and runs until SIGTERM, then stops everything. Exit 1 when the daemon dies.
"""
# pylint: disable=import-error,too-many-locals,consider-using-with,missing-function-docstring
# pylint: disable=invalid-name,too-few-public-methods,too-many-branches,too-many-arguments
# pylint: disable=too-many-positional-arguments,missing-class-docstring
from __future__ import annotations

import argparse
import json
import os
import signal
import socket
import subprocess
import sys
import time
from pathlib import Path

import soak_station as ss

LINKS = ("radio_a", "radio_b")


def FreePorts(n: int) -> list[int]:
    socks = [socket.socket(socket.AF_INET, socket.SOCK_DGRAM) for _ in range(n)]
    try:
        for s in socks:
            s.bind(("127.0.0.1", 0))
        return [s.getsockname()[1] for s in socks]
    finally:
        for s in socks:
            s.close()


def Config(work: Path, base: str, ports: dict[str, tuple[int, int]]) -> Path:
    """The daemon config of the package with the driver files pointing at the loopback."""
    with open(base, encoding="utf-8") as f:
        cfg = json.load(f)
    cfg["socket_path"] = str(work / "daemon.sock")
    for link in LINKS:
        gs_port, drv_port = ports[link]
        drv = work / f"{link}.json"
        drv.write_text(json.dumps({"bind_port": drv_port, "peer_host": "127.0.0.1",
                                   "peer_port": gs_port}), encoding="utf-8")
        cfg["links"][link]["driver_config"] = str(drv)
    path = work / "mavlink_daemon_config.json"
    path.write_text(json.dumps(cfg), encoding="utf-8")
    return path


def Parse(argv) -> argparse.Namespace:
    ap = argparse.ArgumentParser(description=__doc__.split("\n", maxsplit=1)[0])
    ap.add_argument("--workdir", required=True)
    ap.add_argument("--daemon", help="example_udp_daemon_host (default: of the runfiles)")
    ap.add_argument("--app1", help="example_app_no_em (default: of the runfiles)")
    ap.add_argument("--app2", help="example_app2_no_em (default: of the runfiles)")
    ap.add_argument("--config", help="mavlink_daemon_config.json of the package (default: runfiles)")
    return ap.parse_args(argv)


def main(argv=None) -> int:
    args = Parse(argv)
    work = Path(args.workdir)
    work.mkdir(parents=True, exist_ok=True)
    daemon = args.daemon or ss.FindData("tests/mavlink/example_udp_daemon_host")
    app1 = args.app1 or ss.FindData("tests/mavlink/example_app_no_em")
    app2 = args.app2 or ss.FindData("tests/mavlink/example_app2_no_em")
    base = args.config or ss.FindData("tests/mavlink/udp_cpu/mavlink_daemon_config.json")
    if not all((daemon, app1, app2, base)):
        print("soak_local: daemon, apps or config not found", file=sys.stderr)
        return 2
    free = FreePorts(4)
    ports = {"radio_a": (free[0], free[1]), "radio_b": (free[2], free[3])}  # (station, driver)
    cfg = Config(work, base, ports)
    procs: list[subprocess.Popen] = []
    stop = []
    for sig in (signal.SIGTERM, signal.SIGINT):
        signal.signal(sig, lambda *_: stop.append(1))
    code = 0
    try:
        # the test daemon is driven by JSON lines on stdin and ends at its EOF: keep the pipe open
        d = subprocess.Popen([daemon, str(cfg)], stdin=subprocess.PIPE,
                             stdout=open(work / "daemon.out", "wb"),
                             stderr=open(work / "daemon.err", "wb"), start_new_session=True)
        procs.append(d)
        for _ in range(300):  # the daemon prints {"ev":"ready"} when the applications may connect
            if '"ready"' in (work / "daemon.out").read_text(encoding="utf-8") or d.poll() is not None:
                break
            time.sleep(0.05)
        env = dict(os.environ, SRP_MAVLINK_DAEMON_SOCKET=str(work / "daemon.sock"))
        for name, app in (("app1", app1), ("app2", app2)):
            procs.append(subprocess.Popen([app], stdout=open(work / f"{name}.out", "wb"),
                                          stderr=subprocess.STDOUT, env=env, start_new_session=True))
        (work / "daemon.pid").write_text(str(d.pid), encoding="utf-8")
        (work / "local.env").write_text(
            f"DAEMON_PID={d.pid}\nDAEMON_CONFIG={cfg}\n"
            f"STATION_PORT_A={ports['radio_a'][0]}\nPEER_PORT_A={ports['radio_a'][1]}\n"
            f"STATION_PORT_B={ports['radio_b'][0]}\nPEER_PORT_B={ports['radio_b'][1]}\n",
            encoding="utf-8")
        while not stop and not code:
            for p, name in zip(procs, ("the daemon", "app1", "app2")):
                if p.poll() is not None:
                    print(f"soak_local: {name} exited with {p.returncode}", file=sys.stderr)
                    code = 1
            time.sleep(0.2)
    finally:
        for p in reversed(procs):
            if p.poll() is None:
                p.terminate()
        for p in procs:
            try:
                p.wait(timeout=5)
            except subprocess.TimeoutExpired:
                p.kill()
    return code


if __name__ == "__main__":
    sys.exit(main())
