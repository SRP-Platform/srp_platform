"""Ground station for the hardware test of `//tests/mavlink/udp_cpu` (the PC side of the UDP link).

Listens on the UDP port the CPU sends to (default 14550), decodes every datagram with the pymavlink
module of the dialect, counts the events and, with `--set-mode N`, sends N SetMode requests to the
CPU (default 192.168.7.2:14560) and waits for the matching responses. Exit 0 = every request was
answered with status 0 and the events of the model were seen; 1 otherwise. The generated monitor
(`bazel run //test_cpu/cpu/ec:dialect_monitor -- --udp 14550`) listens on the same port: run one
of them at a time.

    bazel run //tests/mavlink:udp_cpu_station -- --set-mode 5
    bazel run //tests/mavlink:udp_cpu_station -- --target 192.168.10.51 --debug-dump
"""
# pylint: disable=import-error,too-many-locals,too-many-statements,missing-function-docstring
# pylint: disable=too-few-public-methods
import argparse
import collections
import importlib.util
import socket
import sys
import time

SYSTEM_ID, COMPONENT_ID = 2, 1  # the station: the remote system named in the example models
CPU = (1, 1)


class _Sink:
    def __init__(self):
        self.buf = bytearray()

    def write(self, data):
        self.buf += data


def load(path):
    spec = importlib.util.spec_from_file_location("cpu_dialect", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n", maxsplit=1)[0])
    ap.add_argument("--dialect-py", required=True, help="generated pymavlink module")
    ap.add_argument("--target", default="192.168.7.2", help="address of the CPU")
    ap.add_argument("--peer-port", type=int, default=14560, help="bind_port of the driver (CPU)")
    ap.add_argument("--listen-port", type=int, default=14550, help="peer_port of the driver (PC)")
    ap.add_argument("--listen-host", default="0.0.0.0")
    ap.add_argument("--set-mode", type=int, default=0, metavar="N", help="send N SetMode requests")
    ap.add_argument("--debug-dump", action="store_true", help="send one DebugDump request")
    ap.add_argument("--duration", type=float, default=10.0, help="seconds to listen")
    args = ap.parse_args(argv)

    mod = load(args.dialect_py)
    sink = _Sink()
    tx = mod.MAVLink(sink, srcSystem=SYSTEM_ID, srcComponent=COMPONENT_ID)
    rx = mod.MAVLink(None, srcSystem=SYSTEM_ID, srcComponent=COMPONENT_ID)
    rx.robust_parsing = True
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((args.listen_host, args.listen_port))
    sock.settimeout(0.05)

    def send(msg):
        sink.buf.clear()
        tx.send(msg)
        sock.sendto(bytes(sink.buf), (args.target, args.peer_port))

    events, resp = collections.Counter(), {}
    sent = {}
    end = time.monotonic() + args.duration
    next_req = time.monotonic() + 1.0
    seq = 0
    if args.debug_dump:
        send(mod.MAVLink_srp_engine_service_debug_dump_req_message(
            seq=0, instance=2, target_system=CPU[0], target_component=CPU[1]))
    while time.monotonic() < end:
        if seq < args.set_mode and time.monotonic() >= next_req:
            seq += 1
            sent[seq] = time.monotonic()
            send(mod.MAVLink_srp_engine_service_set_mode_req_message(
                seq=seq, instance=2, target_system=CPU[0], target_component=CPU[1], mode=3))
            next_req += 0.5
        try:
            data, _ = sock.recvfrom(4096)
        except OSError:
            continue
        for m in rx.parse_buffer(data) or []:
            name = m.get_type()
            if name == "BAD_DATA":
                events["BAD_DATA"] += 1
            elif name == "SRP_ENGINE_SERVICE_SET_MODE_RESP":
                d = m.to_dict()
                resp[d["seq"]] = d
            else:
                events[f"{name}#{m.to_dict().get('instance')}"] += 1
    print(f"events in {args.duration:.0f} s:")
    for name, count in sorted(events.items()):
        print(f"  {name:48s} {count:5d}  {count / args.duration:6.2f} Hz")
    bad = [s for s in sent if s not in resp or resp[s].get("status") != 0]
    print(f"SetMode: sent {len(sent)}, answered {len(resp)}, not answered with status 0: {bad}")
    ok = bool(events) and not events["BAD_DATA"] and not bad and len(sent) == args.set_mode
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
