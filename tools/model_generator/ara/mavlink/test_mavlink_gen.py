"""Generator tests: golden artifacts, C++ emission, matrix, CPU generation, CLI and mavgen."""
from __future__ import annotations

import ast
import contextlib
import importlib.util
import io
import json
import os
import subprocess
import sys
import tempfile
import unittest

from tools.model_generator.ara.mavlink import mavlink_gen, monitor_meta
from tools.model_generator.ara.mavlink.mavlink_testlib import (
    App2, Beacon, ComInit, Golden, Models, Reset, Run, Write)


class GoldenTest(unittest.TestCase):
    def test_xml(self):
        self.assertEqual(Run()[0].xml, Golden("srp_radio.xml"))

    def test_links_md(self):
        self.assertEqual(Run()[0].links_md, Golden("srp_radio_links.md"))

    def test_initialization(self):
        self.assertEqual(ComInit(Run()[1][0]), Golden("srp_radio_com_init.cc.txt"))

    def test_output_is_deterministic(self):
        a, b = Run(app2=App2())[0], Run(app2=App2())[0]
        self.assertEqual((a.xml, a.links_md), (b.xml, b.links_md))


class EmissionTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = Run()[1][0]
        cls.init = ComInit(cls.app)

    def test_one_block_per_entry_with_skeleton_for_provide_and_proxy_for_require(self):
        self.assertEqual(self.init.count("CreatSkeletonItem"), 3)
        self.assertEqual(self.init.count("CreatProxyItem"), 1)

    def test_redundancy_lists_every_link(self):
        self.assertIn('CreatSkeletonItem("someip_mavlink_20_3", 3U, 0U, 0U, {"radio1", "lora"}, opts)',
                      self.init)

    def test_constructor_has_no_version_arguments(self):
        self.assertIn('CreatProxyItem("someip_mavlink_20_4", 4U, 2U, 1U, {"radio3"}, opts)',
                      self.init)

    def test_links_and_rate_per_element(self):
        self.assertIn('el1.links = {{"radio1", 2.0}, {"lora", 2.0}};', self.init)  # radio_out
        self.assertIn('el1.links = {{"lora", 0.0}};', self.init)  # radio_lora: 0 = no limit
        self.assertIn('el2.links = {{"radio1", 0.0}, {"lora", 0.0}};', self.init)  # SetMode
        for gone in ("opts.exclude", "opts.max_rate_hz", ".max_rate_hz ="):
            self.assertNotIn(gone, self.init)

    def test_an_element_outside_elements_is_not_in_the_model_or_the_dialect(self):
        def mut(a):
            for e in a["provide"] + a["require"]:
                e["elements"].pop("SetName", None)
        res, apps = Run(app_mut=mut)
        self.assertNotIn("SET_NAME", res.xml)
        self.assertNotIn("SetName", res.links_md)
        init = ComInit(apps[0])
        self.assertIn('el5.name = "SetName";', init)  # the stub the runtime needs for Update()
        self.assertIn('el5.enabled = false;', init)
        self.assertNotIn("SET_NAME", init)
        self.assertNotIn("213U", init)

    def test_source_ids_for_require_only(self):
        self.assertEqual(self.init.count("opts.source_system_id = 2U;"), 1)
        self.assertEqual(self.init.count("opts.source_component_id = 1U;"), 1)

    def test_excluded_elements_are_described_without_messages(self):
        block = self.init.split('el0.name = "DebugCounter";')[1].split("push_back")[0]
        self.assertIn("el0.enabled = false;", self.init)
        self.assertNotIn("msg_id", block)

    def test_method_with_response_has_status_and_no_timeout(self):
        self.assertIn("el2.has_resp = true;", self.init)
        self.assertIn('MF{"status", MT::kU8, 0U, MR::kStatus, false}', self.init)
        self.assertNotIn("timeout_ms", self.init)  # the timeout comes from the daemon at registration

    def test_string_is_a_char_array(self):
        self.assertIn('MF{"value", MT::kChar, 32U, MR::kPayload, false}', self.init)


class LinksMdTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lines = Run(app2=App2())[0].links_md.splitlines()

    def row(self, prefix):
        return next(l for l in self.lines if l.startswith(prefix))

    def test_every_daemon_link_is_a_column(self):
        self.assertEqual(self.row("| element |"),
                         "| element | radio1 | radio2 | radio3 | lora | spare |")

    def test_event_cell_has_application_instance_and_rate(self):
        self.assertIn("MavApp.3 @ 2 Hz", self.row("| srp.mav.Radio.Position"))

    def test_second_application_is_named_and_proxy_is_marked(self):
        row = self.row("| srp.mav.Radio.Position")
        self.assertIn("MavApp2.8 (proxy)", row)
        self.assertNotIn("(proxy) @", row)  # only a sender has an event rate limit

    def test_method_cell_has_no_rate(self):
        self.assertNotIn("Hz", self.row("| srp.mav.Radio.SetMode"))

    def test_instance_exclude_leaves_a_dash(self):
        # radio_lora (instance 5) excludes Reboot, so the lora column has no MavApp.5
        lora = self.row("| srp.mav.Radio.Reboot").split("|")[5]
        self.assertIn("MavApp.3", lora)
        self.assertNotIn("MavApp.5", lora)

    def test_an_element_listed_by_no_instance_is_not_in_the_matrix(self):
        text = "\n".join(self.lines)
        for name in ("LocalOnly", "DebugCounter"):
            self.assertNotIn(name, text)

    def test_rate_is_per_radio(self):
        cells = [c.strip() for c in self.row("| srp.mav.Radio.Position").split("|")]
        self.assertIn("MavApp.6 @ 2 Hz", cells[3])  # radio2
        self.assertIn("MavApp.6 @ 0.5 Hz", cells[4])  # radio3

    def test_a_method_cell_names_only_the_radios_of_the_method(self):
        cells = [c.strip() for c in self.row("| srp.mav.Radio.SetMode").split("|")]
        self.assertEqual(cells[3], "MavApp.6")  # radio2 (radio_mix has SetMode on radio2 only)
        self.assertNotIn("MavApp.6", cells[4])  # radio3

    def test_nothing_but_the_matrix(self):
        headings = [l for l in self.lines if l.startswith("#")]
        self.assertEqual(len(headings), 2)


class WarningTest(unittest.TestCase):
    def stderr(self, **kw):
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            Run(quiet=False, **kw)
        return err.getvalue()

    def test_unused_link_warns(self):
        self.assertIn("link 'spare' of the daemon config is used by no service", self.stderr())

    def test_method_on_two_radios_warns(self):
        self.assertIn("elements.SetMode: method on 2 radios (radio1, lora)", self.stderr())

    def test_a_method_on_one_radio_only_does_not_warn_for_that_instance(self):
        self.assertNotIn("radio_lora: elements.SetMode: method on", self.stderr())

    def test_no_removed_info_line(self):
        self.assertNotIn("outside MAVLink", self.stderr())



class CpuTest(unittest.TestCase):
    def test_two_applications_share_one_dialect(self):
        res, _ = Run(app2=App2())
        self.assertEqual(res.xml.count("<message "), 6)  # Radio once, not twice
        self.assertIn("MavApp2", res.links_md)


class CliTest(unittest.TestCase):
    def run_cli(self, *args, daemon_config="daemon"):
        """Runs main() on the fixture models; returns (exit code, stderr, files, pymavlink text)."""
        Reset()
        models = Models()
        err = io.StringIO()
        with tempfile.TemporaryDirectory() as d:
            models["service"]["include"] = [os.path.join(d, "types.json")]
            models["app"]["include"] = [os.path.join(d, "service.json")]
            p = Write(models, d)
            p["missing"] = os.path.join(d, "missing.json")
            argv = [d, *args, "--daemon-config", p[daemon_config], p["app"]]
            with contextlib.redirect_stderr(err), contextlib.redirect_stdout(io.StringIO()):
                try:
                    rc = mavlink_gen.main(argv)
                except SystemExit as e:  # argparse
                    rc = e.code
            files = sorted(os.listdir(d))
            py_path = os.path.join(d, "cpu_a.py")
            py = ""
            if os.path.exists(py_path):
                with open(py_path) as f:
                    py = f.read()
        return rc, err.getvalue(), files, py

    def test_monitor_metadata_lists_events_and_methods_with_rates(self):
        meta = Run()[0].monitor_meta
        self.assertEqual(meta["dialect"], "srp_radio")
        cpu = meta["cpus"][0]
        kinds = {e["element"]: (e["kind"], e["msg_id"], e["resp_msg_id"]) for e in cpu["elements"]}
        self.assertTrue(kinds)
        for element, (kind, msg_id, resp_id) in kinds.items():
            self.assertEqual(cpu["messages"][str(msg_id)]["element"], element)
            self.assertEqual(cpu["messages"][str(msg_id)]["role"],
                             "event" if kind == "event" else "req")
            if kind == "event":
                self.assertIsNone(resp_id)  # a method may have no response either
        for e in cpu["elements"]:
            for inst in e["instances"]:
                self.assertIn(inst["direction"], ("in", "out"))
                self.assertTrue(all(r >= 0 for r in inst["links"].values()))

    def test_the_monitor_script_embeds_the_metadata(self):
        meta = Run()[0].monitor_meta
        text = monitor_meta.RenderMonitorPy(meta)
        compile(text, "monitor.py", "exec")
        literal = text.split("\nMETA = ", 1)[1].split("\n\n\ndef _runtime", 1)[0]
        self.assertEqual(ast.literal_eval(literal), meta)
        self.assertEqual(json.loads(monitor_meta.RenderMetaJson(meta)), meta)

    def test_writes_the_six_artifacts(self):
        rc, _, files, py = self.run_cli("--dialect", "cpu_a")
        self.assertEqual(rc, 0)
        for name in ("cpu_a.xml", "cpu_a_links.md", "cpu_a.lua", "cpu_a.py", "cpu_a_monitor.json",
                     "cpu_a_monitor.py"):
            self.assertIn(name, files)
        self.assertIn("MAVLink_srp_radio_position_message", py)

    def test_dialect_is_required(self):
        self.assertNotEqual(self.run_cli()[0], 0)

    def test_model_error_exits_with_1(self):
        rc, err, _, _ = self.run_cli("--dialect", "cpu_a", daemon_config="missing")
        self.assertEqual(rc, 1)
        self.assertTrue(err.startswith("ERROR:"), err)


class ModelErrorTest(unittest.TestCase):
    """A SOME/IP model error ends every generator entry point as `ERROR: ...`, not a traceback."""
    ARA = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

    def run_entry(self, script, svc_mut):
        Reset()
        models = Models(svc_mut=svc_mut)
        with tempfile.TemporaryDirectory() as d:
            models["service"]["include"] = [os.path.join(d, "types.json")]
            models["app"]["include"] = [os.path.join(d, "service.json")]
            p = Write(models, d)
            env = dict(os.environ, PYTHONPATH=os.pathsep.join(sys.path))
            return subprocess.run([sys.executable, os.path.join(self.ARA, script), d, p["app"]],
                                  capture_output=True, text=True, env=env)

    def test_malformed_service_exits_1_without_traceback(self):
        def no_id(svc):
            del svc["methods"]["SetName"]["id"]

        def typo(svc):
            svc["events"]["Position"]["mavlnk"] = {"msg_id": 200}

        for script in ("json2config.py", "json2runtime_env.py", "json2someip_lib.py"):
            for mut, text in ((no_id, "Radio.SetName: missing 'id'"),
                              (typo, "Radio.Position: unknown key 'mavlnk'")):
                with self.subTest(script=script, text=text):
                    r = self.run_entry(script, mut)
                    self.assertEqual(r.returncode, 1, r.stderr)
                    self.assertTrue(r.stderr.startswith("ERROR: "), r.stderr)
                    self.assertIn(text, r.stderr)
                    self.assertNotIn("Traceback", r.stderr)


class MavgenOutputTest(unittest.TestCase):
    """The generated pymavlink module packs and parses the dialect messages."""

    @classmethod
    def setUpClass(cls):
        path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "srp_radio.py")
        spec = importlib.util.spec_from_file_location("srp_radio", path)
        cls.m = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.m)

    def roundtrip(self, msg):
        frame = msg.pack(self.m.MAVLink(None, srcSystem=1, srcComponent=1))
        return self.m.MAVLink(None).parse_buffer(frame)[0]

    def test_event_with_seq_and_instance(self):
        back = self.roundtrip(self.m.MAVLink_srp_radio_position_message(
            seq=7, instance=3, lat=1.5, lon=-2.5, fix_type=3))
        self.assertEqual((back.seq, back.instance, back.lat, back.lon, back.fix_type),
                         (7, 3, 1.5, -2.5, 3))

    def test_response_header(self):
        back = self.roundtrip(self.m.MAVLink_srp_radio_set_mode_resp_message(
            seq=9, instance=4, target_system=2, target_component=1, status=0, value=1))
        self.assertEqual((back.seq, back.target_system, back.status), (9, 2, 0))

    def test_ids_and_string(self):
        self.assertEqual(self.m.MAVLINK_MSG_ID_SRP_RADIO_SET_NAME_REQ, 213)
        back = self.roundtrip(self.m.MAVLink_srp_radio_set_name_req_message(
            seq=1, instance=1, target_system=0, target_component=0, value=b"abc"))
        self.assertEqual(back.value, "abc")


if __name__ == "__main__":
    unittest.main()
