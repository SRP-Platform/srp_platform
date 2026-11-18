"""Model tests: service.json messages, `on: mavlink` entries and CPU-wide checks."""
from __future__ import annotations

import contextlib
import io
import unittest

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

    def test_collision_with_heartbeat(self):
        def mut(svc):
            ev(svc)["mavlink"]["msg_id"] = 0
        self.fails("HEARTBEAT", svc_mut=mut)

    def test_duplicate_message_name(self):
        def mut(svc):
            svc["events"]["position"] = {
                "id": 32790, "data_structure": {"out": {"type": "uint8"}},
                "mavlink": {"msg_id": 230}}
        self.fails("duplicate message name SRP_RADIO_POSITION", svc_mut=mut)

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

    def test_v1_link_rejects_ids_above_255(self):
        def mut(svc):
            ev(svc)["mavlink"]["msg_id"] = 300  # the service is used on radio2 (v1)
        self.fails("> 255", svc_mut=mut)

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

    def test_timeout_must_be_positive_integer(self):
        for bad in (0, -5, 1.5, "x"):
            with self.subTest(bad=bad):
                def mut(svc, bad=bad):
                    me(svc)["mavlink"]["timeout_ms"] = bad
                self.fails("timeout_ms", svc_mut=mut)

    def test_event_rate_must_be_positive_number(self):
        for bad in (0, -1, True, "x"):
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
        with self.assertRaises(ValueError) as cm:
            Run(svc_mut=mut)
        self.assertIn("mavlnk", str(cm.exception))
        self.assertIn("Position", str(cm.exception))

    def test_element_name_outside_ascii_identifier(self):
        def mut(svc):
            svc["events"]["Café"] = {
                "id": 32790, "data_structure": {"out": {"type": "uint8"}},
                "mavlink": {"msg_id": 230}}
        self.fails("Café", svc_mut=mut)

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
            {"name", "on", "instance", "links", "exclude", "max_rate_hz", "dedup_ttl_ms",
             "target_system_id", "target_component_id", "source_system_id",
             "source_component_id"})

    def test_application_level_mavlink_blocks_are_an_error(self):
        for key, value in (("mavlink_links", {"lora": {"type": "serial"}}), ("mavlink", {})):
            with self.subTest(key=key):
                def mut(a, key=key, value=value):
                    a[key] = value
                self.fails("MavApp", f"unknown key '{key}'", app_mut=mut)

    def test_links_must_be_a_non_empty_list_of_names(self):
        for bad in ([], "lora", [1], ["bad name"], ["lora", "lora"]):
            with self.subTest(bad=bad):
                self.fails("radio_out", app_mut=self.entry(links=bad))

    def test_missing_links(self):
        def mut(a):
            del a["provide"][0]["links"]
        self.fails("links", app_mut=mut)

    def test_instance_range(self):
        for bad in (-1, 65536, True, "3"):
            with self.subTest(bad=bad):
                self.fails("instance", app_mut=self.entry(instance=bad))

    def test_exclude_unknown_name_not_list_and_duplicate(self):
        self.fails("unknown element 'Nope'", app_mut=self.entry(exclude=["Nope"]))
        self.fails("list of element names", app_mut=self.entry(exclude="Reboot"))
        self.fails("duplicate", app_mut=self.entry(exclude=["Reboot", "Reboot"]))

    def test_rate_must_name_an_event(self):
        self.fails("unknown element", app_mut=self.entry(max_rate_hz={"Nope": 1}))
        self.fails("events only", app_mut=self.entry(max_rate_hz={"Reboot": 1}))

    def test_rate_must_not_be_negative_and_zero_means_unlimited(self):
        self.fails("max_rate_hz.Position", app_mut=self.entry(max_rate_hz={"Position": -1}))
        init = ComInit(Run(app_mut=self.entry(max_rate_hz={"Position": 0}))[1][0])
        self.assertIn('opts.max_rate_hz["Position"] = 0.0;', init)

    def test_rate_for_an_excluded_element_warns_and_is_not_emitted(self):
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            _, apps = Run(app_mut=self.entry(exclude=["Position"], max_rate_hz={"Position": 3}),
                          quiet=False)
        self.assertIn("max_rate_hz.Position ignored", err.getvalue())
        first_block = ComInit(apps[0]).split("CreatSkeletonItem")[0]
        self.assertFalse('max_rate_hz["Position"]' in first_block)

    def test_rate_in_a_require_entry_warns_and_is_ignored(self):
        def mut(a):
            a["require"][0]["max_rate_hz"] = {"Position": 9}
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            _, apps = Run(app_mut=mut, quiet=False)
        self.assertIn("no effect", err.getvalue())
        self.assertEqual(apps[0].require_someip["radio_in"].mavlink.max_rate_hz, {})

    def test_source_ids_apply_to_require_only(self):
        self.fails("require entries only", app_mut=self.entry(source_system_id=2))
        _, apps = Run()
        self.assertEqual(apps[0].require_someip["radio_in"].mavlink.source_system_id, 2)

    def test_id_ranges(self):
        for key in ("target_system_id", "target_component_id"):
            for bad in (-1, 256, True):
                with self.subTest(key=key, bad=bad):
                    self.fails(key, app_mut=self.entry(**{key: bad}))

    def test_dedup_ttl(self):
        for bad in (0, -1, True, 1.5):
            with self.subTest(bad=bad):
                self.fails("dedup_ttl_ms", app_mut=self.entry(dedup_ttl_ms=bad))
        init = ComInit(Run(app_mut=self.entry(dedup_ttl_ms=2500))[1][0])
        self.assertIn("opts.dedup_ttl_ms = 2500U;", init)
        self.assertIn("opts.dedup_ttl_ms = 10000U;", init)

    def test_ipc_and_udp_entries_reject_mavlink_only_keys(self):
        for on in ("ipc", "udp"):
            for key, value in (("links", ["lora"]), ("exclude", []), ("dedup_ttl_ms", 5)):
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

    def test_v1_link_limits_ids_of_every_application(self):
        self.fails("300", app2=App2(service="Beacon", instance=1, links=("radio2",)),
                   svc2=Beacon(300))

    def test_links_of_one_instance_need_one_signing_mode(self):
        def mut(a):
            a["provide"][0]["links"] = ["radio1", "radio3"]  # signed + unsigned
        self.fails("same signing.enabled", app_mut=mut)

        def mut2(d):
            d["links"]["lora"]["signing"]["accept_unsigned"] = True
        self.fails("accept_unsigned", daemon_mut=mut2)

    def test_signing_may_differ_between_instances(self):
        Run()  # radio_out (signed links) and radio_in (unsigned) coexist in the fixture

    def test_names_are_not_checked_without_a_daemon_config(self):
        res, _ = Run(with_daemon=False, app_mut=lambda a: a["provide"][0].update(links=["anything"]))
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
    def test_reads_names_versions_and_signing(self):
        import os
        import tempfile
        with tempfile.TemporaryDirectory() as d:
            cfg = LoadDaemonConfig(Write({"c": Load("daemon_config.json")}, d)["c"])
        self.assertEqual(list(cfg.links), ["radio1", "radio2", "radio3", "lora", "spare"])
        self.assertEqual(cfg.links["radio2"].version, 1)
        self.assertTrue(cfg.links["radio1"].signing_enabled)
        self.assertFalse(cfg.links["radio3"].signing_enabled)

    def test_unreadable_config(self):
        with self.assertRaises(MavlinkModelError):
            LoadDaemonConfig("/nonexistent/daemon.json")


if __name__ == "__main__":
    unittest.main()
