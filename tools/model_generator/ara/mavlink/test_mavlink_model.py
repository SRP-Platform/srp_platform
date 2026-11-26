"""Model tests: service.json messages, `on: mavlink` entries and CPU-wide checks."""
from __future__ import annotations

import contextlib
import io
import unittest

from tools.model_generator.ara.common.model_error import ModelError
from tools.model_generator.ara.mavlink.mavlink_model import (
    ComputeCrcExtra, Field, LoadDaemonConfig, MAVLINK_ITEM_KEYS, MavlinkModelError)
from tools.model_generator.ara.mavlink.mavlink_testlib import (
    App2, Beacon, ComInit, Load, Models, Reset, Run, Write)


def ev(svc, name="Position"):
    return svc["events"][name]


def me(svc, name="SetMode"):
    return svc["methods"][name]


class ModelTest(unittest.TestCase):
    """Run() with a mutated fixture must fail with MavlinkModelError naming `needles`."""

    def fails(self, *needles, **kw):
        with self.assertRaises(MavlinkModelError) as cm:
            Run(**kw)
        for n in needles:
            self.assertIn(n, str(cm.exception))
        return str(cm.exception)


class MessageTest(unittest.TestCase):
    def test_void_method_has_only_req(self):
        xml = Run()[0].xml
        self.assertIn("SRP_RADIO_REBOOT_REQ", xml)
        self.assertNotIn("SRP_RADIO_REBOOT_RESP", xml)

    def test_string_becomes_char_array(self):
        self.assertIn('type="char[32]" name="value"', Run()[0].xml)

    def test_struct_is_flattened_to_snake_case(self):
        self.assertIn('name="fix_type"', Run()[0].xml)

    def test_headers(self):
        xml = Run()[0].xml

        def names(msg):
            body = xml.split(f'name="{msg}"')[1].split("</message>")[0]
            return [p.split('"')[0] for p in body.split('name="')[1:]]

        self.assertEqual(names("SRP_RADIO_POSITION")[:3], ["seq", "instance", "lat"])
        self.assertEqual(names("SRP_RADIO_SET_MODE_REQ")[:5],
                         ["seq", "instance", "target_system", "target_component", "value"])
        self.assertEqual(names("SRP_RADIO_SET_MODE_RESP")[:6],
                         ["seq", "instance", "target_system", "target_component", "status",
                          "value"])

    def test_description_comes_from_desc_with_a_fallback(self):
        def mut(svc):
            ev(svc)["desc"] = "Where the radio is"
        xml = Run(svc_mut=mut)[0].xml
        self.assertIn("<description>Where the radio is</description>", xml)
        self.assertIn("Event srp.mav.Radio.Position", Run()[0].xml)

    def test_crc_extra_known_vectors(self):
        # values produced by mavgen for the same message definitions
        self.assertEqual(ComputeCrcExtra("SRP_RADIO_POSITION", [
            Field("seq", "uint16_t", 2), Field("instance", "uint16_t", 2),
            Field("lat", "float", 4), Field("lon", "float", 4), Field("fix_type", "uint8_t", 1)]),
            212)
        self.assertEqual(ComputeCrcExtra("SRP_RADIO_SET_MODE_REQ", [
            Field("seq", "uint16_t", 2), Field("instance", "uint16_t", 2),
            Field("target_system", "uint8_t", 1), Field("target_component", "uint8_t", 1),
            Field("value", "uint8_t", 1)]), 233)

    def test_big_endian_struct_marks_its_fields(self):
        def mut(svc):
            ev(svc)["data_structure"]["out"]["type"] = "BePos"
        init = ComInit(Run(svc_mut=mut, types_extra={
            "package": "srp.mav", "endian": "big",
            "data_structure": {"BePos": {"a": "uint16"}}})[1][0])
        self.assertIn('MF{"a", MT::kU16, 0U, MR::kPayload, true}', init)
        self.assertIn('MF{"seq", MT::kU16, 0U, MR::kSeq, false}', init)


class ServiceValidationTest(ModelTest):
    def test_id_collision(self):
        def mut(svc):
            me(svc)["mavlink"]["req_id"] = 200
        self.fails("msg_id 200 collision", "Radio.Position", "Radio.SetMode", svc_mut=mut)

    def test_msg_id_0_is_rejected_naming_the_element(self):
        def mut(svc):
            ev(svc)["mavlink"]["msg_id"] = 0
        text = self.fails("ID 0", "ordinary event", "Position", svc_mut=mut)
        self.assertIn("Radio", text)

    def test_duplicate_message_name(self):
        def mut(svc):
            svc["events"]["position"] = {
                "id": 32790, "data_structure": {"out": {"type": "uint8"}},
                "mavlink": {"msg_id": 230}}
        listed = lambda a: a["provide"][0]["elements"].update({"position": {"lora": {}}})
        self.fails("duplicate message name SRP_RADIO_POSITION", svc_mut=mut, app_mut=listed)

    def test_two_elements_with_the_same_someip_id(self):
        def mut(svc):
            svc["methods"]["Reboot"]["id"] = 1
        self.fails("SOME/IP id 1", svc_mut=mut)

    def test_excluded_element_with_the_same_someip_id_still_collides(self):
        def mut(svc):
            svc["methods"]["LocalOnly"]["id"] = 1
        self.fails("SOME/IP id 1", svc_mut=mut)

    def test_excluded_elements_consume_no_message_ids(self):
        def mut(svc):
            svc["events"]["DebugCounter"]["mavlink"] = False
            svc["methods"]["LocalOnly"]["mavlink"] = False
        self.assertNotIn("DEBUG", Run(svc_mut=mut)[0].xml)

    def test_missing_block_equals_false(self):
        def mut(svc):
            del svc["methods"]["LocalOnly"]["mavlink"]
        self.assertEqual(Run(svc_mut=mut)[0].xml, Run()[0].xml)

    def test_ids_above_255_are_accepted(self):
        def mut(svc):
            ev(svc)["mavlink"]["msg_id"] = 300  # MAVLink v2 carries 24-bit ids
        Run(svc_mut=mut)

    def test_payload_limit_includes_the_header(self):
        def mut(n):
            def f(svc):
                ev(svc)["data_structure"]["out"]["type"] = "string"
                ev(svc)["mavlink"]["max_len"] = n
            return f
        Run(svc_mut=mut(251))  # 4 B header + 251 = 255
        self.fails("payload 256 B", svc_mut=mut(252))

    def test_string_needs_max_len(self):
        def mut(svc):
            del me(svc, "SetName")["mavlink"]["max_len"]
        self.fails("max_len", "SetName", svc_mut=mut)

    def test_max_len_range(self):
        def mut(svc):
            me(svc, "SetName")["mavlink"]["max_len"] = 300
        self.fails("max_len", svc_mut=mut)

    def test_response_needs_resp_id(self):
        def mut(svc):
            del me(svc)["mavlink"]["resp_id"]
        self.fails("resp_id", svc_mut=mut)

    def test_method_without_out_cannot_have_resp_id(self):
        def mut(svc):
            me(svc, "Reboot")["mavlink"]["resp_id"] = 230
        self.fails("resp_id", svc_mut=mut)

    def test_timeout_ms_is_not_a_service_json_key_any_more(self):
        for value in (500, 0, "x"):
            with self.subTest(value=value):
                def mut(svc, value=value):
                    me(svc)["mavlink"]["timeout_ms"] = value
                msg = self.fails("SetMode", "mavlink.timeout_ms is removed", "method_timeout_ms",
                                 "daemon config", svc_mut=mut)
                self.assertNotIn("unknown key", msg)

    def test_event_rate_is_not_a_service_json_key_any_more(self):
        for bad in (0, -1, 2, True, "x"):
            with self.subTest(bad=bad):
                def mut(svc, bad=bad):
                    ev(svc)["mavlink"]["max_rate_hz"] = bad
                self.fails("max_rate_hz", svc_mut=mut)

    def test_block_must_be_an_object(self):
        for bad in (True, 5, "x", []):
            with self.subTest(bad=bad):
                def mut(svc, bad=bad):
                    ev(svc)["mavlink"] = bad
                self.fails("Radio.Position", svc_mut=mut)

    def test_unknown_block_key_names_it(self):
        def mut(svc):
            ev(svc)["mavlink"]["req_id"] = 3  # a method key on an event
        self.fails("mavlink.req_id", "Radio.Position", svc_mut=mut)

    def test_unknown_element_key_names_it(self):
        def mut(svc):
            ev(svc)["mavlnk"] = {"msg_id": 5}
        with self.assertRaises(ModelError) as cm:
            Run(svc_mut=mut)
        self.assertIn("mavlnk", str(cm.exception))
        self.assertIn("Position", str(cm.exception))

    def test_element_name_outside_ascii_identifier(self):
        def mut(svc):
            svc["events"]["Café"] = {
                "id": 32790, "data_structure": {"out": {"type": "uint8"}},
                "mavlink": {"msg_id": 230}}
        listed = lambda a: a["provide"][0]["elements"].update({"Café": {"lora": {}}})
        self.fails("Café", svc_mut=mut, app_mut=listed)

    def test_excluded_element_with_an_odd_name_is_not_validated(self):
        def mut(svc):
            svc["events"]["Café"] = {
                "id": 32790, "data_structure": {"out": {"type": "uint8"}}}
        Run(svc_mut=mut)

    def test_unknown_type_is_not_reported_as_a_mavlink_error(self):
        def mut(svc):
            ev(svc)["data_structure"]["out"]["type"] = "Nope"
        with self.assertRaises(Exception) as cm:
            Run(svc_mut=mut)
        self.assertNotIsInstance(cm.exception, MavlinkModelError)


class FieldNameTest(ModelTest):
    def run_with_field(self, name):
        def mut(svc):
            ev(svc)["data_structure"]["out"]["type"] = "Named"
        return Run(svc_mut=mut, types_extra={
            "package": "srp.mav", "data_structure": {"Named": {name: "uint8"}}})

    def test_header_names_are_rejected(self):
        for name in ("seq", "instance", "target_system", "target_component", "status"):
            with self.subTest(name=name):
                with self.assertRaisesRegex(MavlinkModelError, "collides with the message header"):
                    self.run_with_field(name)

    def test_python_reserved_names_are_rejected(self):
        for name in ("lambda", "pass", "pack", "to_dict", "get_msgbuf"):
            with self.subTest(name=name):
                with self.assertRaisesRegex(MavlinkModelError, "reserved Python name"):
                    self.run_with_field(name)

    def test_c_and_lua_keywords_are_fine(self):
        for name in ("int", "end", "local", "register"):
            with self.subTest(name=name):
                self.run_with_field(name)

    def test_name_pattern(self):
        with self.assertRaisesRegex(MavlinkModelError, "ASCII"):
            self.run_with_field("_x")

    def test_duplicate_field_after_flattening(self):
        def mut(svc):
            ev(svc)["data_structure"]["out"]["type"] = "Outer"
        with self.assertRaisesRegex(MavlinkModelError, "duplicate field 'a_b'"):
            Run(svc_mut=mut, types_extra={
                "package": "srp.mav", "data_structure": {"Outer": {"a_b": "uint8", "aB": "uint8"}}})


class EntryTest(ModelTest):
    def entry(self, **kw):
        def mut(a):
            a["provide"][0].update(kw)
        return mut

    def test_unknown_key_names_it(self):
        self.fails("MavApp.", "unknown key 'exlude'", app_mut=self.entry(exlude=["Reboot"]))

    def test_unknown_key_is_an_error_in_provide_and_require(self):
        def mut(a):
            a["require"][0]["link"] = "lora"
        self.fails("MavApp.", "unknown key 'link'", app_mut=mut)

    def test_the_allowed_entry_keys_are_the_documented_ones(self):
        self.assertEqual(
            set(MAVLINK_ITEM_KEYS),
            {"name", "on", "instance", "elements",
             "target_system_id", "target_component_id", "source_system_id",
             "source_component_id"})

    def test_application_level_mavlink_blocks_are_an_error(self):
        for key, value in (("mavlink_links", {"lora": {"type": "serial"}}), ("mavlink", {})):
            with self.subTest(key=key):
                def mut(a, key=key, value=value):
                    a[key] = value
                self.fails("MavApp", f"unknown key '{key}'", app_mut=mut)

    def test_elements_must_be_a_non_empty_whitelist_of_radios(self):
        for bad in ({}, [], "Position", {"Position": {}}, {"Position": []},
                    {"Position": {"bad name": {}}}, {"Position": {"lora": []}},
                    {"Position": {"lora": {"rate": 1}}}):
            with self.subTest(bad=bad):
                self.fails("radio_out", app_mut=self.entry(elements=bad))

    def test_missing_elements_is_an_error_nothing_goes_over_a_radio_by_default(self):
        def mut(a):
            del a["provide"][0]["elements"]
        self.fails("MavApp.radio_out", "'elements'", app_mut=mut)

    def test_instance_range(self):
        for bad in (-1, 65536, True, "3"):
            with self.subTest(bad=bad):
                self.fails("instance", app_mut=self.entry(instance=bad))

    def test_removed_keys_are_errors_with_a_hint(self):
        for key, value in (("links", ["lora"]), ("exclude", ["Reboot"]),
                           ("max_rate_hz", {"Position": 1})):
            with self.subTest(key=key):
                self.fails("MavApp.radio_out", f"the key '{key}' was removed",
                           "\"elements\"", app_mut=self.entry(**{key: value}))

    def test_elements_must_name_an_element_with_a_mavlink_block(self):
        self.fails("elements.Nope", "unknown element", app_mut=self.entry(
            elements={"Nope": {"lora": {}}}))
        for name in ("LocalOnly", "DebugCounter"):  # `"mavlink": false`
            with self.subTest(name=name):
                self.fails(f"elements.{name}", "no 'mavlink' block", app_mut=self.entry(
                    elements={name: {"lora": {}}}))

    def test_rate_applies_to_events_only(self):
        self.fails("events only", app_mut=self.entry(
            elements={"Reboot": {"lora": {"max_rate_hz": 1}}}))

    def test_rate_must_not_be_negative_and_zero_means_unlimited(self):
        self.fails("max_rate_hz=-1", app_mut=self.entry(
            elements={"Position": {"lora": {"max_rate_hz": -1}}}))
        init = ComInit(Run(app_mut=self.entry(
            elements={"Position": {"lora": {"max_rate_hz": 0}}}))[1][0])
        self.assertIn('.links = {{"lora", 0.0}};', init)

    def test_a_method_on_two_radios_warns_that_the_request_runs_twice(self):
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            Run(app_mut=self.entry(elements={"SetMode": {"radio1": {}, "lora": {}}}), quiet=False)
        self.assertIn("elements.SetMode: method on 2 radios (radio1, lora)", err.getvalue())
        self.assertIn("executed 2 times", err.getvalue())

    def test_a_method_on_one_radio_does_not_warn(self):
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            Run(app_mut=self.entry(elements={"SetMode": {"lora": {}}}), quiet=False)
        self.assertNotIn("elements.SetMode: method on", err.getvalue())

    def test_a_rate_in_service_json_is_an_error_with_a_hint(self):
        def mut(svc):
            ev(svc)["mavlink"]["max_rate_hz"] = 2
        self.fails("Radio.Position", "mavlink.max_rate_hz was removed", "app.json", svc_mut=mut)

    def test_rate_in_a_require_entry_warns_and_is_ignored(self):
        def mut(a):
            a["require"][0]["elements"]["Position"]["radio3"] = {"max_rate_hz": 9}
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            _, apps = Run(app_mut=mut, quiet=False)
        self.assertIn("no effect", err.getvalue())
        self.assertEqual(apps[0].require_someip["radio_in"].mavlink.elements["Position"],
                         {"radio3": 0.0})

    def test_source_ids_apply_to_require_only(self):
        self.fails("require entries only", app_mut=self.entry(source_system_id=2))
        _, apps = Run()
        self.assertEqual(apps[0].require_someip["radio_in"].mavlink.source_system_id, 2)

    def test_id_ranges(self):
        for key in ("target_system_id", "target_component_id"):
            for bad in (-1, 256, True):
                with self.subTest(key=key, bad=bad):
                    self.fails(key, app_mut=self.entry(**{key: bad}))

    def test_dedup_ttl_ms_is_an_unknown_key(self):
        # the rocket side does not deduplicate (the ground station does, by `seq`)
        self.fails("unknown key 'dedup_ttl_ms'", app_mut=self.entry(dedup_ttl_ms=2500))
        self.assertNotIn("dedup", ComInit(Run()[1][0]))

    def test_ipc_and_udp_entries_reject_mavlink_only_keys(self):
        for on in ("ipc", "udp"):
            for key, value in (("elements", {"Position": {"lora": {}}}),):
                with self.subTest(on=on, key=key):
                    def mut(a, on=on, key=key, value=value):
                        a["provide"].append({"name": "Radio as other", "on": on, "instance": 9,
                                             key: value})
                    self.fails("only allowed with on=mavlink", app_mut=mut)

    def test_duplicate_entry_name_across_provide_and_require(self):
        def mut(a):
            a["require"][0]["name"] = "Radio as radio_out"
        self.fails("duplicate entry name", app_mut=mut)


class CpuTest(ModelTest):
    def test_unknown_link_names_the_application(self):
        self.fails("srp.mav.MavApp2", "nowhere", app2=App2(links=("nowhere",)))

    def test_instance_rule_is_the_daemons_service_instance_role(self):
        self.fails("srp.mav.MavApp2", "already registered", app2=App2(instance=3,
                                                                       direction="provide"))

    def test_provide_and_require_of_one_instance_do_not_conflict(self):
        Run(app2=App2(instance=3, direction="require"))
        Run(app_mut=lambda a: a["require"][0].update(instance=3))  # even in one application

    def test_duplicate_instance_in_one_application(self):
        def mut(a):
            a["provide"][1]["instance"] = 3
        self.fails("already registered", app_mut=mut)

    def test_msg_id_collision_between_applications(self):
        text = self.fails("msg_id 200 collision", app2=App2(service="Beacon", instance=1),
                          svc2=Beacon(200))
        self.assertIn("srp.mav.MavApp2", text)
        self.assertIn("srp.mav.MavApp", text)

    def test_distinct_msg_ids_between_applications(self):
        res, _ = Run(app2=App2(service="Beacon", instance=1), svc2=Beacon(300))
        self.assertIn('name="SRP_BEACON_PING"', res.xml)

    def test_links_of_one_instance_need_one_signing_mode(self):
        def mut(a):
            a["provide"][0]["elements"]["SetMode"] = {"radio1": {}, "radio3": {}}  # signed + unsigned
        self.fails("same signing.enabled", app_mut=mut)

        def mut2(d):
            d["links"]["lora"]["signing"]["accept_unsigned"] = True
        self.fails("accept_unsigned", daemon_mut=mut2)

    def test_signing_may_differ_between_instances(self):
        Run()  # radio_out (signed links) and radio_in (unsigned) coexist in the fixture

    def test_names_are_not_checked_without_a_daemon_config(self):
        res, _ = Run(with_daemon=False, app_mut=lambda a: a["provide"][0].update(
            elements={"Position": {"anything": {}}}))
        self.assertIn("| element | anything |", res.links_md)

    def test_no_application_with_a_mavlink_entry_is_an_error(self):
        def mut(a):
            a["provide"] = [{"name": "Radio", "on": "ipc", "instance": 1}]
            del a["require"]
        self.fails("no application", app_mut=mut)

    def test_parsing_an_application_prints_nothing(self):
        from tools.model_generator.ara.common.common_parser import CommonParser
        import os
        import tempfile
        Reset()
        models = Models()
        with tempfile.TemporaryDirectory() as d:
            models["service"]["include"] = [os.path.join(d, "types.json")]
            models["app"]["include"] = [os.path.join(d, "service.json")]
            paths = Write(models, d)
            err = io.StringIO()
            with contextlib.redirect_stderr(err), contextlib.redirect_stdout(io.StringIO()):
                CommonParser.LoadJson(paths["app"])
        self.assertEqual(err.getvalue(), "")


class DaemonConfigTest(unittest.TestCase):
    def test_reads_names_and_signing(self):
        import os
        import tempfile
        with tempfile.TemporaryDirectory() as d:
            cfg = LoadDaemonConfig(Write({"c": Load("daemon_config.json")}, d)["c"])
        self.assertEqual(list(cfg.links), ["radio1", "radio2", "radio3", "lora", "spare"])
        self.assertTrue(cfg.links["radio1"].signing_enabled)
        self.assertFalse(cfg.links["radio3"].signing_enabled)

    def test_unreadable_config(self):
        with self.assertRaises(MavlinkModelError):
            LoadDaemonConfig("/nonexistent/daemon.json")

    def load(self, **link):
        import tempfile
        raw = Load("daemon_config.json")
        raw["links"]["radio2"].update(link)
        with tempfile.TemporaryDirectory() as d:
            return LoadDaemonConfig(Write({"c": raw}, d)["c"])

    def test_bandwidth_fields_and_defaults(self):
        cfg = self.load(air_rate_bps=2400, max_utilization=0.5, mtu=200)
        link = cfg.links["radio2"]
        self.assertEqual((link.air_rate_bps, link.max_utilization, link.mtu), (2400, 0.5, 200))
        other = cfg.links["radio3"]
        self.assertEqual((other.air_rate_bps, other.max_utilization, other.mtu), (0, 0.8, 0))

    def test_bad_bandwidth_fields_are_errors(self):
        for bad in ({"air_rate_bps": 0}, {"air_rate_bps": 9.5}, {"air_rate_bps": "9600"},
                    {"max_utilization": 0}, {"max_utilization": 1.5}, {"mtu": 11}, {"mtu": 281},
                    {"mtu": True}):
            with self.subTest(bad=bad), self.assertRaises(MavlinkModelError):
                self.load(**bad)


# Position with a 32 B payload (header seq+instance 4 B + a 28 B string): signed frame
# 10 + 2 + 13 + 32 = 57 B; on the unsigned radio2 it is 44 B.
def Payload32(svc):
    ev(svc)["data_structure"] = {"out": {"type": "string"}}
    ev(svc)["mavlink"] = {"msg_id": 200, "max_len": 28}


def Link(name, **fields):
    def mut(daemon):
        daemon["links"][name].update(fields)
    return mut


def Rate(link, hz):
    def mut(app):
        app["provide"][0]["elements"]["Position"][link] = {"max_rate_hz": hz}
    return mut


class BudgetTest(ModelTest):
    def test_overloaded_link_is_an_error_with_the_link_and_the_percent(self):
        # radio1 is signed: 57 B x 10 Hz = 570 B/s = 4560 bit/s of 2400 bit/s = 190.0%
        msg = self.fails("link 'radio1'", "570 B/s", "4560 bit/s", "190.0%", "> 100%",
                         svc_mut=Payload32, app_mut=Rate("radio1", 10),
                         daemon_mut=Link("radio1", air_rate_bps=2400))
        self.assertIn("air_rate_bps=2400", msg)

    def test_exactly_the_full_link_is_not_an_error(self):
        res, _ = Run(svc_mut=Payload32, app_mut=Rate("radio1", 10),
                     daemon_mut=Link("radio1", air_rate_bps=4560, max_utilization=1.0))
        self.assertIn("| **sum of events** | | | | | 570 | 100.0% |", res.links_md)

    def test_half_load_gives_the_table_with_hand_computed_values(self):
        res, _ = Run(svc_mut=Payload32, app_mut=Rate("radio1", 10),
                     daemon_mut=Link("radio1", air_rate_bps=9120))
        lines = res.links_md.splitlines()
        self.assertIn("### Link `radio1`: signed, air_rate_bps 9120, max_utilization 80%", lines)
        self.assertIn("| srp.mav.Radio.Position | MavApp.3 | event | 57 | 10 | 570 | 50.0% |", lines)
        self.assertIn("| **sum of events** | | | | | 570 | 50.0% |", lines)
        # methods: REQ/RESP frame sizes (signed), no sum: SetMode 7 B / 8 B, Reboot 6 B,
        # SetName 6+32 B / 6+1+4 B payload
        self.assertIn("| srp.mav.Radio.SetMode | MavApp.3 | method | REQ 32 / RESP 33 | - | - | - |", lines)
        self.assertIn("| srp.mav.Radio.Reboot | MavApp.3 | method | REQ 31 | - | - | - |", lines)
        self.assertIn("| srp.mav.Radio.SetName | MavApp.3 | method | REQ 63 / RESP 36 | - | - | - |", lines)

    def info_line(self, **kw):
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            Run(quiet=False, **kw)
        return [l for l in err.getvalue().splitlines() if l.startswith("INFO mavlink budget")]

    def test_info_line_lists_the_percent_of_every_link_and_a_dash_without_budget(self):
        # radio1: 570 B/s of 9120 bit/s = 50%; radio2 has air_rate_bps = no budget
        lines = self.info_line(svc_mut=Payload32, app_mut=Rate("radio1", 10),
                               daemon_mut=Link("radio1", air_rate_bps=9120))
        self.assertEqual(lines, ["INFO mavlink budget: radio1 50.0% (max 80%), radio2 -, "
                                 "radio3 -, lora -, spare -"])

    def test_a_link_load_above_max_utilization_warns(self):
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            Run(quiet=False, svc_mut=Payload32, app_mut=Rate("radio1", 10),
                daemon_mut=Link("radio1", air_rate_bps=5000))
        self.assertIn("WARNING mavlink: link 'radio1'", err.getvalue())
        self.assertIn("91.2% of air_rate_bps=5000 (> max_utilization 80%)", err.getvalue())

    def test_no_warning_below_max_utilization(self):
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            Run(quiet=False, svc_mut=Payload32, app_mut=Rate("radio1", 10),
                daemon_mut=Link("radio1", air_rate_bps=9120))
        self.assertNotIn("max_utilization", err.getvalue())

    def test_an_event_without_max_rate_on_a_link_with_a_budget_is_an_error(self):
        # radio_lora (instance 5) sends Position on lora with max_rate_hz 0 = unlimited
        self.fails("elements.Position", "link 'lora'", "max_rate_hz > 0",
                   daemon_mut=Link("lora", air_rate_bps=100000))

    def test_an_event_without_max_rate_is_fine_on_a_link_without_a_budget(self):
        res, _ = Run(daemon_mut=Link("radio2", mtu=200))
        self.assertIn("| srp.mav.Radio.Position | MavApp.5 | event | 38 | unlimited | - | - |",
                      res.links_md)
        self.assertIn("no air_rate_bps (no budget, no TX pacing)", res.links_md)

    def test_a_frame_above_mtu_is_an_error_naming_element_link_and_size(self):
        # unsigned radio2: 12 + 218 = 230 B
        def big(svc):
            ev(svc)["data_structure"] = {"out": {"type": "string"}}
            ev(svc)["mavlink"] = {"msg_id": 200, "max_len": 214}
        msg = self.fails("elements.Position", "link 'radio2'", "SRP_RADIO_POSITION", "230 B",
                         "mtu 200 B", svc_mut=big, daemon_mut=Link("radio2", mtu=200))
        self.assertNotIn("signature", msg)

    def test_a_frame_exactly_at_mtu_passes(self):
        def big(svc):
            ev(svc)["data_structure"] = {"out": {"type": "string"}}
            ev(svc)["mavlink"] = {"msg_id": 200, "max_len": 214}
        Run(svc_mut=big, daemon_mut=Link("radio2", mtu=230))

    def test_mtu_counts_the_signature_and_the_response_frames(self):
        # signed radio1: SetName RESP = 12 + 13 + 11 = 36 B, REQ = 12 + 13 + 38 = 63 B
        self.fails("link 'radio1'", "SRP_RADIO_SET_NAME_REQ", "63 B", "signature 13",
                   daemon_mut=Link("radio1", mtu=62))
        Run(daemon_mut=Link("radio1", mtu=63))

    def test_a_received_event_is_not_counted_in_the_load(self):
        # radio3: MavApp.6 sends Position @ 0.5 Hz (unsigned: 12 + 13 = 25 B); MavApp.4 requires it
        res, _ = Run(daemon_mut=Link("radio3", air_rate_bps=9600))
        lines = res.links_md.splitlines()
        self.assertIn("| srp.mav.Radio.Position | MavApp.6 | event | 25 | 0.5 | 12.5 | 1.0% |", lines)
        self.assertFalse([l for l in lines if "Position | MavApp.4 |" in l])
        self.assertIn("| **sum of events** | | | | | 12.5 | 1.0% |", lines)

    def test_without_air_rate_or_mtu_the_links_file_has_only_the_matrix(self):
        self.assertNotIn("Bandwidth", Run()[0].links_md)

    def test_without_a_daemon_config_there_is_no_budget(self):
        self.assertNotIn("Bandwidth", Run(with_daemon=False)[0].links_md)


if __name__ == "__main__":
    unittest.main()
