"""End-to-end test of a rocket with two CPUs (P8): two MavlinkDaemons on one machine.

Each "CPU" is a `daemon_host` (own config with its own `component_id`, own unix socket) with
`example_app` registered at it and two radio links on ptys (own ptys per CPU); a pymavlink ground
station sits at the far end of `radio_a` and `radio_b` of each CPU. Both stations use ONE dialect:
the output of `ara_mavlink_system` (//test_cpu/cpu/ec:system) built from the dialects of both CPUs.

Checks: an event of every CPU reaches the station with the `compid` of its CPU; a request
addressed (`target_component`) to CPU 2 is not executed by CPU 1 and the other way round.
"""
import json
import os
import signal
import sys
import subprocess

import pytest

import harness as hn

APP1 = os.environ["EXAMPLE_APP1"]
DAEMON_HOST = os.environ["MAVLINK_DAEMON_HOST"]
CONFIGS = {1: os.environ["EXAMPLE_DAEMON_CONFIG"], 2: os.environ["EXAMPLE_DAEMON_CONFIG_CPU2"]}
LINKS = ("radio_a", "radio_b")
GS_SYSTEM = 2


class Cpu:
    """One CPU: a daemon, `example_app` and a station at the far end of each link."""

    def __init__(self, tmp, scn, comp, mod):
        self.comp, self.tmp, self.mod = comp, tmp, mod
        self.link = {n: scn.add(hn.Link(baud=921600, seed=10 * comp + i))
                     for i, n in enumerate(LINKS)}
        self.gs = {n: scn.add(hn.GS(self.link[n].gs_dev, mod, sysid=GS_SYSTEM, compid=1))
                   for n in LINKS}
        self.socket = str(tmp / f"cpu{comp}.sock")
        cfg = json.load(open(CONFIGS[comp], encoding="utf-8"))
        assert cfg["component_id"] == comp
        cfg["socket_path"] = self.socket
        for n in LINKS:
            drv = str(tmp / f"cpu{comp}_{n}.json")
            json.dump({"device": self.link[n].daemon_dev, "baudrate": 921600}, open(drv, "w"))
            cfg["links"][n]["driver_config"] = drv
        self.cfg_path = str(tmp / f"cpu{comp}_config.json")
        json.dump(cfg, open(self.cfg_path, "w"))
        self.daemon = hn.JsonProc([DAEMON_HOST, self.cfg_path],
                                  err_path=str(tmp / f"cpu{comp}_daemon.err"))
        self.daemon.wait_event("ready")
        self.out_path = tmp / f"cpu{comp}_app1.out"
        self.app = subprocess.Popen(
            [APP1], stdout=open(self.out_path, "wb"), stderr=subprocess.STDOUT,
            env=dict(os.environ, SRP_MAVLINK_DAEMON_SOCKET=self.socket), start_new_session=True)
        scn.add(self, "shutdown")

    def shutdown(self):
        if self.app.poll() is None:
            os.killpg(self.app.pid, signal.SIGKILL)
            self.app.wait()
        self.daemon.terminate()

    def app_lines(self, prefix):
        with open(self.out_path, "rb") as f:
            return [l for l in f.read().decode("utf-8", "replace").splitlines()
                    if l.startswith(prefix)]

    def events(self, link="radio_a"):
        return self.gs[link].of("SRP_ENGINE_SERVICE_CURRENT_MODE")

    def request_debug_dump(self, link, seq, target_component):
        cls = self.mod.MAVLink_srp_engine_service_debug_dump_req_message
        return self.gs[link].send(cls(seq=seq, instance=2, target_system=1,
                                      target_component=target_component))


@pytest.fixture
def scn():
    s = hn.Scenario()
    yield s
    s.close()


@pytest.fixture
def cpus(tmp_path, scn):
    mod = hn.load_dialect_module()  # the dialect of the whole system
    c = {i: Cpu(tmp_path, scn, i, mod) for i in (1, 2)}
    for cpu in c.values():
        assert hn.wait_until(lambda cpu=cpu: cpu.events(), 15.0, step=0.1), (
            f"CPU {cpu.comp}: no event; daemon: {cpu.daemon.stderr_tail(1500)}")
    return c


def test_an_event_of_each_cpu_reaches_the_station_with_the_compid_of_that_cpu(cpus):
    for comp, cpu in cpus.items():
        for link in LINKS:
            assert hn.wait_until(lambda: cpu.events(link), 8.0), (comp, link)
            assert {(m["sysid"], m["compid"]) for m in cpu.gs[link].messages} == {(1, comp)}, (
                comp, link)
        assert all(g.bad == 0 for g in cpu.gs.values())


def test_a_request_for_cpu2_is_not_served_by_cpu1_and_the_other_way_round(cpus):
    cpu1, cpu2 = cpus[1], cpus[2]
    # control: a request addressed to the CPU that receives it is served
    cpu1.request_debug_dump("radio_a", 11, target_component=1)
    cpu2.request_debug_dump("radio_a", 21, target_component=2)
    assert hn.wait_until(lambda: cpu1.app_lines("REQ DebugDump instance=2"), 5.0)
    assert hn.wait_until(lambda: cpu2.app_lines("REQ DebugDump instance=2"), 5.0)
    served1, served2 = len(cpu1.app_lines("REQ DebugDump")), len(cpu2.app_lines("REQ DebugDump"))
    # the station addresses CPU 2 over the radio of CPU 1 and CPU 1 over the radio of CPU 2
    cpu1.request_debug_dump("radio_a", 12, target_component=2)
    cpu2.request_debug_dump("radio_a", 22, target_component=1)
    # a request addressed to the receiver again: the daemon is alive and answers after the wrong ones
    cpu1.request_debug_dump("radio_a", 13, target_component=1)
    cpu2.request_debug_dump("radio_a", 23, target_component=2)
    assert hn.wait_until(lambda: len(cpu1.app_lines("REQ DebugDump")) > served1, 5.0)
    assert hn.wait_until(lambda: len(cpu2.app_lines("REQ DebugDump")) > served2, 5.0)
    hn.wait_until(lambda: False, 1.0)  # time for a wrong-addressed request to be (wrongly) served
    assert len(cpu1.app_lines("REQ DebugDump")) == served1 + 1, "CPU 1 served a request for CPU 2"
    assert len(cpu2.app_lines("REQ DebugDump")) == served2 + 1, "CPU 2 served a request for CPU 1"


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v", "-p", "no:cacheprovider"]))
