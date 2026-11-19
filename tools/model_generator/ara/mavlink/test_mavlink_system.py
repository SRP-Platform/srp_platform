"""System generator tests: the dialects of two CPUs merged into one dialect for the station.

CPU `ec` is the fixture of the other tests (service Radio, msg_id 200..214, component_id 1);
CPU `fc` is `testdata/cpu2` (service Beacon, msg_id 201, component_id 2).
"""
from __future__ import annotations

import contextlib
import importlib.util
import io
import json
import os
import tempfile
import unittest

from tools.model_generator.ara.mavlink import mavlink_gen, mavlink_system
from tools.model_generator.ara.mavlink.mavlink_model import MavlinkModelError
from tools.model_generator.ara.mavlink import monitor_meta
from tools.model_generator.ara.mavlink.mavlink_system import CpuInput, GenerateSystem
from tools.model_generator.ara.mavlink.mavlink_testlib import HERE, Reset, Run


def _load(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def _write(path, text):
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)
    return path


class SystemTestCase(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.d = self.tmp.name

    def ec(self, comp=1, system=1, name="ec"):
        """A CPU with the fixture dialect (service Radio) and its daemon config ids."""
        res, _ = Run()
        return self._cpu(name, res, self._daemon(name, comp, system, None))

    def _daemon(self, name, comp, system, base):
        cfg = base if base is not None else _load(os.path.join(HERE, "testdata", "app",
                                                              "daemon_config.json"))
        cfg["component_id"], cfg["system_id"] = comp, system
        return _write(os.path.join(self.d, name + "_daemon.json"), json.dumps(cfg))

    def _cpu(self, name, res, daemon_path):
        return CpuInput(name, _write(os.path.join(self.d, name + ".xml"), res.xml),
                        _write(os.path.join(self.d, name + "_links.md"), res.links_md), daemon_path,
                        _write(os.path.join(self.d, name + "_monitor.json"),
                               json.dumps(res.monitor_meta)))

    def fc(self, comp=2, system=1, msg_id=201, svc_mut=None):
        """CPU `fc`: testdata/cpu2 with its own dialect (generated the way the Bazel rule does)."""
        Reset()
        src = os.path.join(HERE, "testdata", "cpu2")
        svc = _load(os.path.join(src, "service.json"))
        svc["someip"]["Beacon"]["events"]["Ping"]["mavlink"]["msg_id"] = msg_id
        if svc_mut:
            svc_mut(svc["someip"]["Beacon"])
        app = _load(os.path.join(src, "app.json"))
        svc_path = os.path.join(self.d, "fc_service.json")
        app["include"] = [svc_path]
        _write(svc_path, json.dumps(svc))
        app_path = _write(os.path.join(self.d, "fc_app.json"), json.dumps(app))
        daemon = self._daemon("fc", comp, system, _load(os.path.join(src, "daemon_config.json")))
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            res = mavlink_gen.GenerateAll([app_path], "srp_beacon", daemon)
        return self._cpu("fc", res, daemon)

    def fails(self, cpus, *needles):
        with self.assertRaises(MavlinkModelError) as cm:
            GenerateSystem("rocket", cpus)
        for n in needles:
            self.assertIn(n, str(cm.exception))
        return str(cm.exception)


class CollisionTest(SystemTestCase):
    def test_same_msg_id_for_different_services_names_both_cpus(self):
        ec, fc = self.ec(), self.fc(msg_id=200)  # Radio.Position is 200 on `ec`
        text = self.fails([ec, fc], "msg_id 200", "CPU 'ec'", "CPU 'fc'", "SRP_RADIO_POSITION",
                          "SRP_BEACON_PING")
        self.assertNotIn("msg_id 201", text)

    def test_the_same_service_on_two_cpus_is_one_definition(self):
        a, b = self.ec(comp=1), self.ec(comp=2, name="ec2")
        res = GenerateSystem("rocket", [a, b])
        single = Run()[0].xml.count("<message ")
        self.assertEqual(res.xml.count("<message "), single)

    def test_the_same_msg_id_with_another_field_is_an_error(self):
        # the same id and name, a different payload type: not the same service
        def widen(rep):
            rep["events"]["Position"]["data_structure"]["out"] = {"type": "uint32"}
        res_b, _ = Run(svc_mut=widen)
        b = self._cpu("fc", res_b, self._daemon("fc", 2, 1, None))
        self.fails([self.ec(), b], "msg_id 200", "CPU 'ec'", "CPU 'fc'")

    def test_the_same_name_with_another_id_is_an_error(self):
        a = self.ec()
        res_b, _ = Run(svc_mut=lambda r: r["events"]["Position"]["mavlink"].update(msg_id=230))
        b = self._cpu("fc", res_b, self._daemon("fc", 2, 1, None))
        self.fails([a, b], "SRP_RADIO_POSITION", "CPU 'ec'", "CPU 'fc'")


class IdentityTest(SystemTestCase):
    def test_the_same_system_and_component_id_is_an_error(self):
        text = self.fails([self.ec(comp=1), self.fc(comp=1)], "CPU 'fc'", "CPU 'ec'",
                          "system_id=1", "component_id=1")
        self.assertIn("component_id per CPU", text)

    def test_different_component_ids_pass(self):
        res = GenerateSystem("rocket", [self.ec(), self.fc()])
        self.assertEqual(res.warnings, [])

    def test_different_system_ids_pass_with_a_warning(self):
        res = GenerateSystem("rocket", [self.ec(), self.fc(system=2)])
        self.assertEqual(len(res.warnings), 1)
        self.assertIn("system_id", res.warnings[0])

    def test_a_config_without_ids_is_an_error(self):
        fc = self.fc()
        cfg = _load(fc.daemon_config)
        del cfg["component_id"]
        _write(fc.daemon_config, json.dumps(cfg))
        self.fails([self.ec(), fc], "CPU 'fc'", "component_id")

    def test_two_cpus_with_one_name_are_an_error(self):
        a = self.ec()
        self.fails([a, a], "same name")


class LinksMdTest(SystemTestCase):
    def test_addressing_table_and_both_cpus(self):
        md = GenerateSystem("rocket", [self.ec(), self.fc()]).links_md
        self.assertIn("| ec | 1 | 1 | radio1, radio2, radio3, lora, spare |", md)
        self.assertIn("| fc | 1 | 2 | radio1 |", md)
        self.assertIn("## CPU `ec`", md)
        self.assertIn("## CPU `fc`", md)
        self.assertIn("target_component", md)
        self.assertNotIn("\n# MAVLink links of the CPU", md)


class CliTest(SystemTestCase):
    def run_cli(self, *cpus):
        argv = [self.d, "--dialect", "rocket"]
        for c in cpus:
            argv += ["--cpu", *c]
        err = io.StringIO()
        with contextlib.redirect_stderr(err), contextlib.redirect_stdout(io.StringIO()):
            rc = mavlink_system.main(argv)
        return rc, err.getvalue()

    def test_writes_the_six_artifacts(self):
        a, b = self.ec(), self.fc()
        rc, _ = self.run_cli(a, b)
        self.assertEqual(rc, 0)
        for ext in (".xml", "_links.md", ".lua", ".py", "_monitor.json", "_monitor.py"):
            self.assertTrue(os.path.exists(os.path.join(self.d, "rocket" + ext)), ext)

    def test_the_monitor_metadata_names_both_cpus_by_their_ids(self):
        res = GenerateSystem("rocket", [self.ec(comp=1), self.fc(comp=2)])
        cpus = res.monitor_meta["cpus"]
        self.assertEqual([(c["name"], c["system_id"], c["component_id"]) for c in cpus],
                         [("ec", 1, 1), ("fc", 1, 2)])
        elements = {e["element"] for c in cpus for e in c["elements"]}
        self.assertLessEqual({"Radio.Position", "Beacon.Ping"}, elements)
        self.assertEqual({m["element"] for c in cpus for m in c["messages"].values()}, elements)

    def test_a_msg_id_that_means_two_elements_is_a_monitor_error(self):
        meta_a = {"name": "a", "messages": {"5": {"element": "S.A", "name": "A", "role": "event"}}}
        meta_b = {"name": "b", "messages": {"5": {"element": "S.B", "name": "B", "role": "event"}}}
        with self.assertRaises(MavlinkModelError) as cm:
            monitor_meta.BuildMeta("rocket", [meta_a, meta_b])
        self.assertIn("msg_id 5", str(cm.exception))

    def test_one_info_line_per_cpu_with_the_percent_of_its_links(self):
        def payload32(svc):
            svc["events"]["Position"]["data_structure"] = {"out": {"type": "string"}}
            svc["events"]["Position"]["mavlink"] = {"msg_id": 200, "max_len": 28}

        def rate(app):  # 57 B x 10 Hz = 570 B/s of 9120 bit/s = 50.0%
            app["provide"][0]["elements"]["Position"]["radio1"] = {"max_rate_hz": 10}

        def air(daemon):
            daemon["links"]["radio1"]["air_rate_bps"] = 9120
        res, _ = Run(svc_mut=payload32, app_mut=rate, daemon_mut=air)
        cfg = _load(os.path.join(HERE, "testdata", "app", "daemon_config.json"))
        air(cfg)
        a = self._cpu("ec", res, self._daemon("ec", 1, 1, cfg))
        rc, err = self.run_cli(a, self.fc())
        self.assertEqual(rc, 0)
        lines = [l for l in err.splitlines() if l.startswith("INFO mavlink budget")]
        self.assertEqual(len(lines), 2)
        self.assertEqual(lines[0], "INFO mavlink budget: CPU ec: radio1 50.0% (max 80%), "
                                   "radio2 -, radio3 -, lora -, spare -")
        self.assertTrue(lines[1].startswith("INFO mavlink budget: CPU fc: "))

    def test_a_collision_exits_with_1_and_names_the_cpus(self):
        rc, err = self.run_cli(self.ec(), self.fc(msg_id=200))
        self.assertEqual(rc, 1)
        self.assertIn("CPU 'ec'", err)
        self.assertIn("CPU 'fc'", err)


class StationParsesBothCpusTest(unittest.TestCase):
    """AC: the one generated `<dialect>.py` parses the frames of both CPUs (two compids)."""

    @classmethod
    def setUpClass(cls):
        paths = os.environ.get("EXAMPLE_SYSTEM", "").split()
        py = [p for p in paths if p.endswith("srp_rocket.py")]
        if not py:
            raise unittest.SkipTest("run through bazel (EXAMPLE_SYSTEM)")
        spec = importlib.util.spec_from_file_location("srp_rocket", py[0])
        cls.m = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.m)
        cls.files = {os.path.basename(p): p for p in paths}

    def frame(self, comp, msg):
        return msg.pack(self.m.MAVLink(None, srcSystem=1, srcComponent=comp))

    def test_frames_from_two_compids_parse_with_one_dialect(self):
        pos = self.m.MAVLink_srp_radio_position_message(seq=1, instance=3, lat=1.5, lon=2.5,
                                                        fix_type=3)
        ping = self.m.MAVLink_srp_beacon_ping_message(seq=2, instance=1, value=77)
        station = self.m.MAVLink(None, srcSystem=255, srcComponent=190)
        got = station.parse_buffer(self.frame(1, pos) + self.frame(2, ping))
        self.assertEqual([(g.get_type(), g.get_srcSystem(), g.get_srcComponent()) for g in got],
                         [("SRP_RADIO_POSITION", 1, 1), ("SRP_BEACON_PING", 1, 2)])
        self.assertEqual((got[0].lat, got[1].value), (1.5, 77))

    def test_artifacts_of_the_rule(self):
        for name in ("srp_rocket.xml", "srp_rocket_links.md", "srp_rocket.lua", "srp_rocket.py",
                     "srp_rocket_monitor.json", "srp_rocket_monitor.py"):
            self.assertIn(name, self.files)
        with open(self.files["srp_rocket.xml"], encoding="utf-8") as f:
            xml = f.read()
        self.assertIn("SRP_RADIO_POSITION", xml)
        self.assertIn("SRP_BEACON_PING", xml)


if __name__ == "__main__":
    unittest.main()
