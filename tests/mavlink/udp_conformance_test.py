"""Conformance of the daemon's wire traffic (over the UDP example radio) to the model.

The daemon (`example_udp_daemon_host`) and the example applications run; on the other side of the
UDP links a ground station decodes every datagram ONLY with the pymavlink module that the generator
produced for the CPU dialect (no hand written parser) and compares what it saw with what the models
allow: the `elements` of the applications (`example_app.json`, `example_app2.json`), the daemon
config (link names, ids, `air_rate_bps`, `method_timeout_ms`, signing) and the known behaviour of the
applications (`engine_service.h`: Telemetry is updated at 100 Hz with rpm = tick, CurrentMode at
10 Hz with mode = tick / 10).

Checks (every failure names the link, the element and the numbers):
  1 completeness  every event of `elements` appears; a frame of anything else is a failure
  2 frequency     frames in the window <= max_rate_hz * window + 1, and not far below the expected rate
  3 values        decoded fields (temp, rpm, mode, instance) follow the known sequence
  4 seq           payload `seq` per (element, instance) and the header `seq` per link have no gaps
  5 methods       REQ -> RESP with the same seq, status 0 / 2; a request for another system is silent;
                  a silent peer makes the caller fail after `method_timeout_ms`
  6 sanity        no BAD_DATA / unknown msgid / v1 frame / HEARTBEAT, sysid and compid as configured,
                  signed links: every frame signed, signature timestamps rising
  7 budget        measured B/s <= air_rate_bps / 8 * max_utilization (+ tolerance)
A table (expected / received / rejected per element) is printed to the test log.
"""
# pylint: disable=missing-function-docstring,redefined-outer-name,import-error,too-many-locals
# pylint: disable=too-many-arguments,too-many-branches,too-many-statements,consider-using-with
import json
import os
import re
import sys
import time

import pytest

import harness as hn
import mav_helpers as mh
import udp_harness as uh

DAEMON_HOST = os.environ["UDP_DAEMON_HOST"]
DAEMON_CFG = os.environ["EXAMPLE_DAEMON_CONFIG"]
APP1 = os.environ["EXAMPLE_APP1"]
APP2 = os.environ["EXAMPLE_APP2"]
WINDOW_S = 6.0
# What the example application does (example_app/.../engine_service.h, Tick() every 10 ms):
SOURCE_HZ = {"Telemetry": 100.0, "CurrentMode": 10.0}
K_PEER_UNREACHABLE = 5  # ara::com::ComErrc::kPeerIsUnreachable (method timeout)


def snake(name):
    return re.sub(r"(?<!^)(?=[A-Z])", "_", name).upper()


# --------------------------------------------------------------------------- the model

class Model:
    """What the models and the daemon config say about the traffic on each link."""

    def __init__(self, mod):
        self.mod = mod
        self.cfg = json.load(open(DAEMON_CFG, encoding="utf-8"))
        self.events, self.methods = {}, {}   # service -> element names
        for path in os.environ["MODEL_SERVICES"].split():
            if path.endswith("engine_service.json"):
                self._find_service(json.load(open(path, encoding="utf-8")))
        self.provided = {n: {} for n in self.cfg["links"]}   # link -> {(element, instance): max_rate_hz}
        self.required = {n: set() for n in self.cfg["links"]}  # link -> {(element, instance)} the CPU calls
        for path, section in ((os.environ["MODEL_APP1"], "provide"), (os.environ["MODEL_APP2"], "require")):
            app = next(iter(json.load(open(path, encoding="utf-8"))["adaptive_application"].values()))
            for entry in app.get(section, []):
                if entry.get("on") != "mavlink":
                    continue
                for element, links in entry["elements"].items():
                    for link, opts in links.items():
                        if section == "provide":
                            self.provided[link][(element, entry["instance"])] = opts.get("max_rate_hz")
                        else:
                            self.required[link].add((element, entry["instance"]))
        names = {c.msgname for c in mod.mavlink_map.values()}
        self.msgnames = names

    def _find_service(self, node):
        if isinstance(node, dict):
            if "events" in node or "methods" in node:
                self.events = set(node.get("events", {}))
                self.methods = set(node.get("methods", {}))
                return
            for v in node.values():
                self._find_service(v)

    def msg(self, element, kind=""):
        """The dialect message name of an element of EngineService (kind: '', '_REQ', '_RESP')."""
        want = "ENGINE_SERVICE_" + snake(element) + kind
        found = [n for n in self.msgnames if n.endswith(want)]
        if not found and kind:
            return None  # e.g. a void method has a REQ only
        assert len(found) == 1, f"dialect has no unique message for {element}{kind}: {found}"
        return found[0]

    def allowed(self, link):
        """{msgname: set of instances} that may appear on the link (events, RESPs of served methods,
        REQs of the proxies)."""
        out = {}
        for (element, instance) in self.provided[link]:
            kind = "_RESP" if element in self.methods else ""
            name = self.msg(element, kind)
            if name:
                out.setdefault(name, set()).add(instance)
        for (element, instance) in self.required[link]:
            out.setdefault(self.msg(element, "_REQ"), set()).add(instance)
        out.pop(None, None)
        return out


# --------------------------------------------------------------------------- the checks

class Report:
    def __init__(self):
        self.failures, self.rows = [], []

    def fail(self, link, what):
        self.failures.append(f"[{link}] {what}")

    def table(self):
        lines = ["", f"{'link':8} {'element':44} {'inst':>4} {'expected':>14} {'received':>8} {'rejected':>8}"]
        lines += [f"{l:8} {e:44} {i!s:>4} {x:>14} {r!s:>8} {j!s:>8}" for l, e, i, x, r, j in self.rows]
        return "\n".join(lines)


def in_window(recs, t0, t1):
    return [m for m in recs if t0 <= m["t"] < t1]


def check_link(model, report, link, gs, t0, t1, signed):
    mod, cfg = model.mod, model.cfg["links"][link]
    allowed = model.allowed(link)
    window = t1 - t0
    msgs = list(gs.messages)

    # 6 sanity: decoding, protocol version, ids, heartbeat
    if gs.bad:
        report.fail(link, f"{len(gs.bad)} datagram(s) did not decode to one valid frame (BAD_DATA / CRC / unknown msgid): "
                          f"first {gs.bad[0][1].hex()}")
    for _, d in gs.datagrams:
        if d[0] != 0xFD:
            report.fail(link, f"frame is not MAVLink 2 (magic {d[0]:#04x})")
            break
        msgid = d[7] | d[8] << 8 | d[9] << 16
        if msgid == 0:
            report.fail(link, "daemon sent a HEARTBEAT (msgid 0)")
            break
        if msgid not in mod.mavlink_map:
            report.fail(link, f"unknown msgid {msgid}")
            break
        if signed != bool(d[2] & 1):
            report.fail(link, f"frame {msgid}: signed={bool(d[2] & 1)}, the link is {'signed' if signed else 'unsigned'}")
            break
    for m in msgs:
        if (m["sysid"], m["compid"]) != (model.cfg["system_id"], model.cfg["component_id"]):
            report.fail(link, f"{m['type']}: sysid/compid {m['sysid']}/{m['compid']} != config "
                              f"{model.cfg['system_id']}/{model.cfg['component_id']}")
            break
    if gs.rejected:
        report.fail(link, f"{len(gs.rejected)} frame(s) refused by the signature verifier: "
                          f"{sorted({m['verdict'] for m in gs.rejected})}")
    if signed:
        stamps = [int.from_bytes(m["raw"][-12:-6], "little") for m in msgs]
        if any(b <= a for a, b in zip(stamps, stamps[1:])):
            report.fail(link, "signature timestamps do not rise strictly")

    # 1 completeness: nothing outside `elements`
    for m in msgs:
        inst = m["d"].get("instance")
        if m["type"] not in allowed or inst not in allowed[m["type"]]:
            report.fail(link, f"{m['type']} instance {inst} is not in `elements` of this link "
                              f"(allowed: {sorted((str(k), sorted(v)) for k, v in allowed.items())})")
            break

    # 2, 3, 4 per element
    for (element, instance), rate in sorted(model.provided[link].items()):
        if element in model.methods:
            continue
        name = model.msg(element)
        frames = [m for m in msgs if m["type"] == name and m["d"]["instance"] == instance]
        win = in_window(frames, t0, t1)
        source = SOURCE_HZ[element]
        expected = min(source, rate) if rate else source
        lo, hi = int(expected * window * 0.6) - 1, int(expected * window * 1.2) + 2
        if rate:
            hi = min(hi, int(rate * window) + 1)
        report.rows.append((link, name, instance, f"{lo}..{hi}", len(win),
                            len([m for m in gs.rejected if m["type"] == name])))
        if not frames:
            report.fail(link, f"{name} instance {instance}: never received (listed in `elements`)")
            continue
        if not lo <= len(win) <= hi:
            report.fail(link, f"{name} instance {instance}: {len(win)} frames in {window:.0f} s, expected {lo}..{hi} "
                              f"(max_rate_hz={rate}, source {source} Hz)")
        # 4 payload seq: the application counts its Update() calls, so between two frames the seq grows
        # by what was updated in between (source Hz x time); no duplicates, no going back
        seqs = [m["d"]["seq"] for m in frames]
        for a, b in zip(frames, frames[1:]):
            step, dt = (b["d"]["seq"] - a["d"]["seq"]) % 65536, b["t"] - a["t"]
            if step == 0 or abs(step - source * dt) > 0.3 * source * dt + 2:
                report.fail(link, f"{name} instance {instance}: payload seq {a['d']['seq']} -> {b['d']['seq']} after "
                                  f"{dt * 1000:.0f} ms (expected about {source * dt:.0f} updates); seqs {seqs[:10]}")
                break
        # 3 values
        if element == "Telemetry":
            temps = {m["d"]["temp"] for m in frames}
            if temps != {21.5}:
                report.fail(link, f"{name} instance {instance}: temp {sorted(temps)} != [21.5]")
            series = [m["d"]["rpm"] for m in frames]
        else:
            series = [m["d"]["mode"] for m in frames]
        wrap = 65536 if element == "Telemetry" else 256  # uint16 rpm, uint8 mode
        if any(not (0 < (b - a) % wrap < wrap // 2 or (b == a and element == "CurrentMode"))
               for a, b in zip(series, series[1:])):
            report.fail(link, f"{name} instance {instance}: value does not grow in order: {series[:12]}")
        dt = frames[-1]["t"] - frames[0]["t"]
        if dt >= 2.0:
            grow = ((series[-1] - series[0]) % wrap) / dt
            want = source
            if not (want * 0.8 <= grow <= want * 1.05 + 1.5 / dt):
                report.fail(link, f"{name} instance {instance}: value grows {grow:.1f}/s, the application "
                                  f"updates it at {want:.0f}/s")

    # 4 header seq per link (all frames of the link, in order of arrival)
    hs = [d[4] for _, d in gs.datagrams]
    gaps = [(a, b) for a, b in zip(hs, hs[1:]) if (b - a) % 256 != 1]
    if gaps:
        report.fail(link, f"header seq has gaps/duplicates, e.g. {gaps[:3]} (UDP on loopback must not lose frames)")

    # 7 budget
    air = cfg.get("air_rate_bps")
    nbytes = sum(len(d) for t, d in gs.datagrams if t0 <= t < t1)
    if air:
        limit = air / 8 * cfg.get("max_utilization", 0.8)
        measured = nbytes / window
        report.rows.append((link, "bandwidth [B/s] (limit)", "-", f"<= {limit * 1.1:.0f}", f"{measured:.0f}", 0))
        if measured > limit * 1.1 + 25:  # 25 B/s = one RESP/REQ burst in the window
            report.fail(link, f"measured {measured:.0f} B/s exceeds air_rate_bps {air} x {cfg.get('max_utilization', 0.8)} = "
                              f"{limit:.0f} B/s")
    else:
        report.rows.append((link, "bandwidth [B/s] (no air_rate_bps)", "-", "-", f"{nbytes / window:.0f}", 0))


def check_methods(stack, report):
    """5: REQ from the ground station -> RESP with the same seq."""
    mod, g = stack.mod, stack.gs["radio_a"]
    sys_id, comp_id = stack.cfg["system_id"], stack.cfg["component_id"]

    def req(seq, instance, target=(sys_id, comp_id), mode=3):
        g.send(mod.MAVLink_srp_engine_service_set_mode_req_message(
            seq=seq, instance=instance, target_system=target[0], target_component=target[1], mode=mode))

    def resp(seq, timeout=3.0):
        hn.wait_until(lambda: any(m["d"]["seq"] == seq for m in g.of("SRP_ENGINE_SERVICE_SET_MODE_RESP")), timeout)
        found = [m for m in g.of("SRP_ENGINE_SERVICE_SET_MODE_RESP") if m["d"]["seq"] == seq]
        return found[0]["d"] if found else None

    req(201, 2, mode=7)
    r = resp(201)
    if r is None:
        report.fail("radio_a", "SetMode REQ for instance 2 got no RESP")
    elif (r["status"], r["mode"], r["instance"]) != (0, 7, 2):
        report.fail("radio_a", f"SetMode RESP for instance 2: {r} (expected status 0, mode 7, instance 2)")
    for seq, instance in ((202, 3), (203, 9)):  # instance 3 does not list SetMode; 9 does not exist
        req(seq, instance)
        r = resp(seq)
        if r is None or r["status"] != 2:
            report.fail("radio_a", f"SetMode REQ for instance {instance} (not in `elements`): {r}, expected status 2")
    req(204, 2, target=(sys_id + 5, comp_id))
    if resp(204, timeout=1.0) is not None:
        report.fail("radio_a", "a REQ addressed to another system was answered")


def observe(stack):
    stack.wait_registered()
    t0 = time.monotonic()
    time.sleep(WINDOW_S)
    return t0, time.monotonic()


# --------------------------------------------------------------------------- tests

@pytest.fixture
def scn():
    s = hn.Scenario()
    yield s
    s.close()


def run_conformance(tmp_path, scn, key):
    stack = uh.UdpStack(tmp_path, scn, DAEMON_HOST, DAEMON_CFG, APP1, APP2, key=key)
    model = Model(stack.mod)
    t0, t1 = observe(stack)
    report = Report()
    for link in uh.LINKS:
        check_link(model, report, link, stack.gs[link], t0, t1, key is not None)
    check_methods(stack, report)
    if not stack.lines("app2", "CALL SetMode ok"):
        report.fail("radio_a", "the proxy application never completed a SetMode call (RESP did not reach it)")
    print(report.table())
    assert not report.failures, "CONFORMANCE FAILED:\n" + "\n".join(report.failures) + "\n" + report.table()


def test_unsigned_udp_traffic_conforms_to_the_model(tmp_path, scn):
    run_conformance(tmp_path, scn, None)


def test_signed_udp_traffic_conforms_to_the_model(tmp_path, scn):
    run_conformance(tmp_path, scn, mh.make_key())


def test_a_silent_peer_makes_the_caller_fail_after_method_timeout_ms(tmp_path, scn):
    stack = uh.UdpStack(tmp_path, scn, DAEMON_HOST, DAEMON_CFG, APP1, APP2, answer=False)
    model = Model(stack.mod)
    stack.wait_registered()
    first = stack.gs["radio_a"].of("SRP_ENGINE_SERVICE_SET_MODE_REQ")[0]
    errors = len(stack.lines("app2", "CALL SetMode err="))
    timeout_ms = max(model.cfg["links"][l].get("method_timeout_ms", 1000)
                     for l in uh.LINKS if any(e == "SetMode" for e, _ in model.required[l]))
    assert hn.wait_until(lambda: len(stack.lines("app2", "CALL SetMode err=")) > errors, 8.0, step=0.01), \
        "the call never failed although nobody answered"
    elapsed = time.monotonic() - first["t"]
    assert f"err={K_PEER_UNREACHABLE}" in stack.lines("app2", "CALL SetMode err=")[errors], stack.out("app2")
    assert timeout_ms / 1000 * 0.9 <= elapsed <= timeout_ms / 1000 + 1.0, \
        f"the call failed {elapsed * 1000:.0f} ms after its REQ, method_timeout_ms = {timeout_ms}"


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v", "-s", "-p", "no:cacheprovider"]))
