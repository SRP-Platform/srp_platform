"""Generator tests: golden artifacts, C++ emission, matrix, CPU generation, CLI and mavgen."""
from __future__ import annotations

import contextlib
import importlib.util
import io
import os
import tempfile
import unittest

from tools.model_generator.ara.mavlink import mavlink_gen
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

    def test_exclude_and_rate_override_per_instance(self):
        self.assertIn('opts.exclude = {"Reboot"};', self.init)
        self.assertIn('opts.max_rate_hz["Position"] = 0.0;', self.init)

    def test_source_ids_for_require_only(self):
        self.assertEqual(self.init.count("opts.source_system_id = 2U;"), 1)
        self.assertEqual(self.init.count("opts.source_component_id = 1U;"), 1)

    def test_excluded_elements_are_described_without_messages(self):
        block = self.init.split('el0.name = "DebugCounter";')[1].split("push_back")[0]
        self.assertIn("el0.enabled = false;", self.init)
        self.assertNotIn("msg_id", block)

    def test_method_with_response_has_status_and_timeout(self):
        self.assertIn("el2.has_resp = true;", self.init)
        self.assertIn('MF{"status", MT::kU8, 0U, MR::kStatus, false}', self.init)
        self.assertIn("el2.timeout_ms = 500U;", self.init)

    def test_string_is_a_char_array_and_event_rate_comes_from_service_json(self):
        self.assertIn('MF{"value", MT::kChar, 32U, MR::kPayload, false}', self.init)
        self.assertIn("el1.max_rate_hz = 2.0;", self.init)


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

    def test_excluded_elements_have_dashes_only(self):
        cells = self.row("| srp.mav.Radio.LocalOnly").split("|")[2:-1]
        self.assertEqual([c.strip() for c in cells], ["-"] * 5)

    def test_exclusion_lists(self):
        text = "\n".join(self.lines)
        self.assertIn("- srp.mav.Radio.DebugCounter\n- srp.mav.Radio.LocalOnly", text)
        self.assertIn("- MavApp.radio_in (instance 4): SetName", text)
        self.assertIn("- MavApp.radio_lora (instance 5): Reboot", text)

    def test_nothing_but_matrix_and_exclusions(self):
        headings = [l for l in self.lines if l.startswith("#")]
        self.assertEqual(len(headings), 4)


class WarningTest(unittest.TestCase):
    def stderr(self, **kw):
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            Run(quiet=False, **kw)
        return err.getvalue()

    def test_unused_link_warns(self):
        self.assertIn("link 'spare' of the daemon config is used by no service", self.stderr())

    def test_info_lists_excluded_elements_once_per_application(self):
        out = self.stderr()
        self.assertEqual(out.count("INFO mavlink: srp.mav.MavApp: 2 elements outside MAVLink"), 1)

    def test_instance_without_any_element_warns(self):
        def mut(a):
            a["provide"][1]["exclude"] = ["Reboot", "SetMode", "SetName", "Position"]
            del a["provide"][1]["max_rate_hz"]
        self.assertIn("radio_lora (service srp.mav.Radio, instance 5): every element is excluded",
                      self.stderr(app_mut=mut))

    def test_normal_model_has_no_all_excluded_warning(self):
        self.assertNotIn("every element", self.stderr())


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

    def test_writes_the_four_artifacts(self):
        rc, _, files, py = self.run_cli("--dialect", "cpu_a")
        self.assertEqual(rc, 0)
        for name in ("cpu_a.xml", "cpu_a_links.md", "cpu_a.lua", "cpu_a.py"):
            self.assertIn(name, files)
        self.assertIn("MAVLink_srp_radio_position_message", py)

    def test_dialect_is_required(self):
        self.assertNotEqual(self.run_cli()[0], 0)

    def test_model_error_exits_with_1(self):
        rc, err, _, _ = self.run_cli("--dialect", "cpu_a", daemon_config="missing")
        self.assertEqual(rc, 1)
        self.assertIn("ERROR mavlink", err)


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
