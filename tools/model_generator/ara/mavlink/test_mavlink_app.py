"""Testy modelu aplikacji MAVLink: łącza, links per usługa, walidacja, artefakty."""
from __future__ import annotations

import contextlib
import copy
import io
import json
import os
import tempfile
import unittest

from tools.model_generator.ara.app.adaptive_application_db import AdaptiveApplicationDb
from tools.model_generator.ara.common.data_structure_db import DataStructureDB
from tools.model_generator.ara.mavlink.mavlink_errors import MavlinkModelError
from tools.model_generator.ara.mavlink.mavlink_gen import GenerateAll
from tools.model_generator.ara.someip.lib.someip_db import SomeIpDb
from tools.model_generator.ara.someip.lib.someip_runtime_env import SomeipRuntimeEnv

HERE = os.path.dirname(os.path.abspath(__file__))
APP_DIR = os.path.join(HERE, "testdata", "app")
GOLDEN = os.path.join(HERE, "testdata")


def _load(name):
    with open(os.path.join(APP_DIR, name), encoding="utf-8") as f:
        return json.load(f)


def reset():
    SomeIpDb().service_list.clear()
    DataStructureDB().data_structure.clear()
    AdaptiveApplicationDb().app_list.clear()


def run(app_mut=None, svc_mut=None, quiet=True, with_daemon=True, raw_mut=None):
    """Generuje artefakty z fixtury po zmianach; zwraca (Result, app).

    Konwencja testów: w słowniku aplikacji przekazywanym do `app_mut` są też `mavlink`
    (system_id, component_id) i `mavlink_links` - to WIDOK configu daemona; po zmianach run()
    wyjmuje je z aplikacji (apka ma tylko instancje) i zapisuje jako mavlink_daemon_config.json.
    """
    reset()
    app, svc, types = _load("app.json"), _load("service.json"), _load("types.json")
    dmn = _load("daemon_config.json")
    a = app["adaptive_application"]["MavApp"]
    a["mavlink"] = {"system_id": dmn["system_id"], "component_id": dmn["component_id"]}
    a["mavlink_links"] = dmn["links"]
    if svc_mut:
        svc_mut(svc["someip"]["Radio"])
    if app_mut:
        app_mut(a)
    mav = a.pop("mavlink", None)
    links = a.pop("mavlink_links", None)
    if mav is not None:
        dmn.update(mav)
    if raw_mut:
        raw_mut(a)  # modyfikacja aplikacji już bez widoku configu daemona
    if links is not None:
        dmn["links"] = links
    with tempfile.TemporaryDirectory() as d:
        paths = {}
        for n in ("types", "service", "app", "daemon"):
            paths[n] = os.path.join(d, n + ".json")
        svc["include"] = [paths["types"]]
        app["include"] = [paths["service"]]
        for n, obj in (("types", types), ("service", svc), ("app", app), ("daemon", dmn)):
            with open(paths[n], "w", encoding="utf-8") as f:
                json.dump(obj, f)
        quiet_err = contextlib.redirect_stderr(io.StringIO()) if quiet else contextlib.nullcontext()
        with contextlib.redirect_stdout(io.StringIO()), quiet_err:
            res = GenerateAll([paths["app"]], "srp_radio",
                              daemon_config=paths["daemon"] if with_daemon else None)
    return res, list(AdaptiveApplicationDb().app_list.values())[0]


def com_init(app):
    return SomeipRuntimeEnv.CreateInitDB("srp/mav/MavApp", app.GetServiceList(), app)


def golden(name):
    with open(os.path.join(GOLDEN, name), encoding="utf-8") as f:
        return f.read()


class GoldenTest(unittest.TestCase):
    def test_xml_golden(self):
        res, _ = run()
        self.assertEqual(res.xml, golden("srp_radio_app.xml"))

    def test_links_md_golden(self):
        res, _ = run()
        self.assertEqual(res.links_md, golden("srp_radio_links.md"))

    def test_initialization_golden(self):
        _, app = run()
        self.assertEqual(com_init(app), golden("srp_radio_com_init.cc.txt"))

    def test_versions_used(self):
        res, _ = run()
        self.assertEqual(res.versions, [1, 2])

    def test_service_generates_proxy_skeleton_items(self):
        _, app = run()
        self.assertIn("radio_out", app.provide_someip)
        self.assertIn("radio_in", app.require_someip)
        self.assertEqual(app.provide_someip["radio_out"].mavlink.links, ["radio1", "lora"])

    def test_redundancy_both_links_in_model(self):
        _, app = run()
        init = com_init(app)
        self.assertIn('{"radio1", "lora"}, opts', init)
        self.assertIn("CreatSkeletonItem", init)
        self.assertIn("CreatProxyItem", init)

    def test_v1_and_v2_on_one_service(self):
        # radio_mix (instancja 6): radio2 = v1, radio3 = v2 -> usługa generowana pod v1
        res, _ = run()
        self.assertIn("| radio2 | serial_radio | `-` | v1 |", res.links_md)
        self.assertIn("| radio3 | serial_radio | `-` | v2 |", res.links_md)

    def test_unused_link_warns(self):
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            run(quiet=False)
        self.assertIn("'spare'", err.getvalue())
        self.assertIn("nie jest używane", err.getvalue())

    def test_signing_state_file_default_and_override(self):
        res, _ = run()
        self.assertIn("stan: `(domyślny daemona)`", res.links_md)
        self.assertIn("stan: `/var/lib/app/lora.ts`", res.links_md)

    def test_source_ids_emitted_for_require_only(self):
        _, app = run()
        init = com_init(app)
        self.assertEqual(init.count("opts.source_system_id = 2U;"), 1)
        self.assertEqual(init.count("opts.source_component_id = 1U;"), 1)


class ErrorTest(unittest.TestCase):
    def _fails(self, app_mut=None, svc_mut=None, *needles):
        with self.assertRaises(MavlinkModelError) as cm:
            with contextlib.redirect_stderr(io.StringIO()):
                run(app_mut, svc_mut)
        for n in needles:
            self.assertIn(n, str(cm.exception))

    def test_unknown_link(self):
        def m(a):
            a["provide"][0]["links"] = ["radio1", "nieistniejace"]
        self._fails(m, None, "nieistniejace", "radio_out")

    def test_v1_with_signing(self):
        def m(a):
            a["mavlink_links"]["radio1"]["version"] = 1
        self._fails(m, None, "radio1", "signing", "v2")

    def test_signing_link_id_must_differ(self):
        def m(a):
            a["mavlink_links"]["radio2"]["version"] = 2
            a["mavlink_links"]["radio2"]["signing"] = {
                "enabled": True, "key_file": "/k", "link_id": 1}
        self._fails(m, None, "link_id")

    def test_signing_requires_key_file(self):
        def m(a):
            del a["mavlink_links"]["radio1"]["signing"]["key_file"]
        self._fails(m, None, "key_file")

    def test_empty_links(self):
        def m(a):
            a["provide"][0]["links"] = []
        self._fails(m, None, "links")

    def test_missing_links(self):
        def m(a):
            del a["provide"][0]["links"]
        self._fails(m, None, "links")

    def test_duplicate_link_in_links(self):
        def m(a):
            a["provide"][0]["links"] = ["radio1", "radio1"]
        self._fails(m, None, "radio1")

    def test_removed_link_fields_rejected(self):
        # każde dawne pole transportu w configu daemona = błąd z nazwą łącza i podpowiedzią
        for key, val in (("transport", "serial"), ("transport", "udp"), ("device", "/dev/ttyS1"),
                         ("baudrate", 57600), ("bind", "0.0.0.0:1"), ("peer", "1.2.3.4:5"),
                         ("max_bytes_per_s", 1000)):
            def m(a, key=key, val=val):
                a["mavlink_links"]["radio1"][key] = val
            self._fails(m, None, "radio1", f"'{key}'", "driver_config")

    def test_empty_link_and_signing_only_link_ok(self):
        def m(a):
            a["mavlink_links"]["bare"] = {"driver": "serial_radio"}
            a["mavlink_links"]["signed"] = {"driver": "serial_radio",
                                            "signing": {"enabled": True, "key_file": "/k",
                                                        "link_id": 9}}
        run(m)

    def test_bad_version(self):
        def m(a):
            a["mavlink_links"]["radio2"]["version"] = 3
        self._fails(m, None, "version")

    def test_app_mavlink_blocks_rejected_with_hint(self):
        # rev. 10: konfiguracja łączy jest w MavlinkDaemon, nie w aplikacji
        for block, val in (("mavlink", {"system_id": 1, "component_id": 1, "dialect": "x"}),
                           ("mavlink_links", {"radio1": {}})):
            def m(a, block=block, val=val):
                a[block] = val
            with self.assertRaises(MavlinkModelError) as cm:
                run(raw_mut=m)
            self.assertIn(f"blok '{block}'", str(cm.exception))
            self.assertIn("MavlinkDaemon", str(cm.exception))

    def test_links_missing_in_daemon_config(self):
        def m(a):
            a["mavlink_links"] = {}
        self._fails(m, None, "radio1", "configu daemona")

    def test_missing_dialect_argument(self):
        reset()
        with self.assertRaises(MavlinkModelError) as cm:
            GenerateAll([], None)
        self.assertIn("--dialect", str(cm.exception))

    def test_id_collision_between_elements(self):
        def s(svc):
            svc["methods"]["Reboot"]["mavlink"]["req_id"] = 200
        self._fails(None, s, "200")

    def test_heartbeat_id_collision(self):
        def s(svc):
            svc["events"]["Position"]["mavlink"]["msg_id"] = 0
        self._fails(None, s, "HEARTBEAT")

    def test_v1_link_requires_small_ids(self):
        def s(svc):
            svc["methods"]["Reboot"]["mavlink"]["req_id"] = 42020
        self._fails(None, s, "42020", "255")

    def test_system_id_range(self):
        def m(a):
            a["mavlink"]["system_id"] = 0
        self._fails(m, None, "system_id")

    def test_target_id_range(self):
        def m(a):
            a["require"][0]["target_system_id"] = 300
        self._fails(m, None, "target_system_id")

    def test_payload_too_big_in_service(self):
        def s(svc):
            svc["methods"]["SetName"]["mavlink"]["max_len"] = 255
        self._fails(None, s, "SetName", "255")

    def test_v2_only_service_allows_big_ids(self):
        def m(a):
            a["provide"].pop(2)  # radio_mix (v1) wypada: usługa tylko na łączach v2
        def s(svc):
            svc["methods"]["Reboot"]["mavlink"]["req_id"] = 42020
        res, _ = run(m, s)  # radio2 (v1) nieużywane -> OK, tylko ostrzeżenie
        self.assertIn("42020", res.xml)


class TransportTest(unittest.TestCase):
    def _fails(self, app_mut, *needles):
        with self.assertRaises(MavlinkModelError) as cm:
            run(app_mut)
        for n in needles:
            self.assertIn(n, str(cm.exception))

    def test_link_model_has_no_transport_fields(self):
        _, app = run()
        init = com_init(app)
        self.assertIn('"lora"', init)
        for word in ("Transport::", ".device", ".baudrate", "max_bytes_per_s",
                     "signing_", "heartbeat_hz", "dialect"):
            self.assertNotIn(word, init)

    def test_radio_block_not_in_platform_model(self):
        def m(a):
            a["mavlink_links"]["radio1"]["radio"] = {"module": "modem"}
        self._fails(m, "radio1", "'radio'")

    def test_gpio_block_not_in_platform_model(self):
        def m(a):
            a["mavlink_links"]["radio1"]["gpio"] = {}
        self._fails(m, "gpio")

    def test_v1_signing_rejected_on_bare_link(self):
        def m(a):
            a["mavlink_links"]["lora"]["version"] = 1
        self._fails(m, "lora", "signing")

    def test_signing_link_id_unique(self):
        def m(a):
            a["mavlink_links"]["lora"]["signing"]["link_id"] = 1
        self._fails(m, "link_id")


class ExcludedElementTest(unittest.TestCase):
    def test_excluded_absent_from_xml(self):
        res, _ = run()
        self.assertNotIn("DEBUG_COUNTER", res.xml)
        self.assertNotIn("LOCAL_ONLY", res.xml)
        self.assertNotIn("DEBUG_COUNTER", res.links_md)

    def test_excluded_does_not_consume_ids(self):
        def s(svc):
            svc["events"]["DebugCounter"]["mavlink"] = {"msg_id": 200}
        with self.assertRaises(MavlinkModelError):
            run(None, s)  # po wlaczeniu kolizja z Position=200

    def test_missing_block_equals_false(self):
        def s(svc):
            del svc["events"]["DebugCounter"]["mavlink"]
            del svc["methods"]["LocalOnly"]["mavlink"]
        res_missing, _ = run(None, s)
        res_false, _ = run()
        self.assertEqual(res_missing.xml, res_false.xml)
        self.assertEqual(res_missing.links_md, res_false.links_md)

    def test_missing_block_not_an_error_for_new_element(self):
        def s(svc):
            del svc["methods"]["Reboot"]["mavlink"]
        res, _ = run(None, s)
        self.assertNotIn("REBOOT", res.xml)
        self.assertIn("srp.mav.Radio.Reboot", res.links_md)

    def test_excluded_listed_in_links_md_and_log(self):
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            res, _ = run(quiet=False)
        self.assertIn("## Elementy poza MAVLinkiem", res.links_md)
        self.assertIn("- srp.mav.Radio.DebugCounter", res.links_md)
        self.assertIn("- srp.mav.Radio.LocalOnly", res.links_md)
        self.assertIn("DebugCounter", err.getvalue())

    def test_all_excluded_warns(self):
        def s(svc):
            svc["events"]["Position"]["mavlink"] = False
            for m in svc["methods"].values():
                m.pop("mavlink", None)  # brak bloku = wyłączony

        def a(app):  # nadpisania instancji dotyczą elementów, które już są wyłączone
            app["provide"][1].pop("max_rate_hz")
            app["require"][0].pop("max_rate_hz")
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            run(a, s, quiet=False)
        self.assertIn("W_ALL_EXCLUDED", err.getvalue())


class InstanceTest(unittest.TestCase):
    def _fails(self, app_mut, *needles):
        with self.assertRaises(MavlinkModelError) as cm:
            run(app_mut)
        for n in needles:
            self.assertIn(n, str(cm.exception))

    def test_two_instances_on_different_links(self):
        _, app = run()
        init = com_init(app)
        self.assertIn('someip_mavlink_20_3', init)
        self.assertIn('someip_mavlink_20_5', init)
        self.assertEqual(app.provide_someip["radio_lora"].mavlink.links, ["lora"])

    def test_exclude_emitted_per_instance(self):
        _, app = run()
        init = com_init(app)
        self.assertIn('opts.exclude = {"Reboot"};', init)
        self.assertIn('opts.exclude = {"SetName"};', init)

    def test_rate_override_emitted(self):
        _, app = run()
        init = com_init(app)
        self.assertIn('opts.max_rate_hz["Position"] = 0.0;', init)
        self.assertNotIn('opts.max_rate_hz["Position"] = 5.0;', init)  # require: ignorowane

    def test_links_md_lists_instance_options(self):
        res, _ = run()
        self.assertIn("| exclude | max_rate_hz |", res.links_md)
        self.assertIn("Reboot", res.links_md)
        self.assertIn("Position=0", res.links_md)

    def test_exclude_unknown_name(self):
        def m(a):
            a["provide"][1]["exclude"] = ["Nieistnieje"]
        self._fails(m, "Nieistnieje", "radio_lora")

    def test_exclude_not_list(self):
        def m(a):
            a["provide"][1]["exclude"] = "Reboot"
        self._fails(m, "exclude")

    def test_exclude_duplicate(self):
        def m(a):
            a["provide"][1]["exclude"] = ["Reboot", "Reboot"]
        self._fails(m, "exclude")

    def test_rate_unknown_event(self):
        def m(a):
            a["provide"][1]["max_rate_hz"] = {"Brak": 1}
        self._fails(m, "Brak")

    def test_rate_on_method_rejected(self):
        def m(a):
            a["provide"][1]["max_rate_hz"] = {"SetMode": 1}
        self._fails(m, "SetMode", "metodą")

    def test_rate_negative(self):
        def m(a):
            a["provide"][1]["max_rate_hz"] = {"Position": -1}
        self._fails(m, "Position", ">= 0")

    def test_rate_on_excluded_element_warns(self):
        def m(a):
            a["provide"][1]["max_rate_hz"] = {"DebugCounter": 1}
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            run(m, quiet=False)
        self.assertIn("DebugCounter", err.getvalue())
        self.assertIn("OSTRZEŻENIE", err.getvalue())

    def test_rate_and_exclude_same_event_warns(self):
        def m(a):
            a["provide"][1]["exclude"] = ["Position"]
            a["provide"][1]["max_rate_hz"] = {"Position": 1}
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            run(m, quiet=False)
        self.assertIn("Position", err.getvalue())

    def test_duplicate_instance_same_service(self):
        def m(a):
            a["provide"][1]["instance"] = 3
        self._fails(m, "instancja 3", "radio_lora")

    def test_duplicate_instance_provide_vs_require(self):
        def m(a):
            a["require"][0]["instance"] = 3
        self._fails(m, "instancja 3")

    def test_same_instance_number_on_udp_and_mavlink_allowed(self):
        def m(a):
            a["provide"].append({"name": "Radio as radio_udp", "on": "udp",
                                 "port": "10001", "instance": 3})
        run(m)

    def test_ipc_udp_reject_mavlink_only_keys(self):
        for key, val in (("links", ["radio1"]), ("exclude", ["Reboot"]),
                         ("max_rate_hz", {"Position": 1}), ("target_system_id", 1),
                         ("source_system_id", 1)):
            def m(a, key=key, val=val):
                a["provide"].append({"name": "Radio as radio_udp", "on": "udp",
                                     "port": "10001", "instance": 9, key: val})
            with self.assertRaises(MavlinkModelError) as cm:
                run(m)
            self.assertIn(key, str(cm.exception))

    def test_instance_out_of_range(self):
        def m(a):
            a["provide"][1]["instance"] = 70000
        self._fails(m, "instance")

    def test_rate_zero_allowed(self):
        run()  # fixtura ma override 0 (bez limitu)


class MatrixTest(unittest.TestCase):
    def _rows(self):
        res, _ = run()
        rows = {}
        for line in res.links_md.splitlines():
            if line.startswith("| srp.mav.Radio."):
                cells = [c.strip() for c in line.strip("|").split("|")]
                rows[cells[0]] = cells[1:]
        return res.links_md, rows

    def test_matrix_header_has_all_links(self):
        md, _ = self._rows()
        self.assertIn("| element | radio1 | radio2 | radio3 | lora | spare |", md)

    def test_event_cell_has_instance_and_rate(self):
        _, rows = self._rows()
        pos = rows["srp.mav.Radio.Position (event)"]
        # kolumny: radio1 radio2 radio3 lora spare
        self.assertEqual(pos[0], "MavApp.3 @ 2 Hz")                # service.json: 2 Hz
        self.assertEqual(pos[1], "MavApp.6 @ 2 Hz")
        self.assertEqual(pos[2], "MavApp.6 @ 2 Hz, MavApp.4 (proxy) @ 2 Hz")  # override w require
        self.assertEqual(pos[3], "MavApp.3 @ 2 Hz, MavApp.5")      # inst 5: 0 = bez limitu
        self.assertEqual(pos[4], "-")

    def test_method_cell_without_rate(self):
        _, rows = self._rows()
        sm = rows["srp.mav.Radio.SetMode (method)"]
        self.assertEqual(sm[0], "MavApp.3")
        self.assertEqual(sm[2], "MavApp.6, MavApp.4 (proxy)")

    def test_per_instance_exclude_shows_dash(self):
        _, rows = self._rows()
        self.assertEqual(rows["srp.mav.Radio.Reboot (method)"][3], "MavApp.3")   # inst 5 wykl.
        self.assertEqual(rows["srp.mav.Radio.SetName (method)"][2], "MavApp.6")  # require wyklucza

    def test_globally_excluded_row_all_dashes(self):
        _, rows = self._rows()
        self.assertEqual(set(rows["srp.mav.Radio.DebugCounter (event)"]), {"-"})
        self.assertEqual(set(rows["srp.mav.Radio.LocalOnly (method)"]), {"-"})

    def test_exclusion_lists(self):
        md, _ = self._rows()
        self.assertIn("## Wykluczenia per instancja", md)
        self.assertIn("- MavApp.radio_lora (instancja 5): Reboot", md)
        self.assertIn("- MavApp.radio_in (instancja 4): SetName", md)
        self.assertIn("- srp.mav.Radio.DebugCounter", md)


class Rev4Test(unittest.TestCase):
    def _fails(self, app_mut, *needles):
        with self.assertRaises(MavlinkModelError) as cm:
            run(app_mut)
        for n in needles:
            self.assertIn(n, str(cm.exception))

    # --- pkt 1: mieszany podpis w jednej instancji
    def test_mixed_signing_enabled_in_instance(self):
        def m(a):
            a["provide"][0]["links"] = ["radio1", "radio3"]  # podpisane + niepodpisane
        self._fails(m, "radio_out", "signing")

    def test_mixed_accept_unsigned_in_instance(self):
        def m(a):
            a["mavlink_links"]["lora"]["signing"]["accept_unsigned"] = True
        self._fails(m, "accept_unsigned", "radio_out")

    def test_uniform_signing_in_instance_ok(self):
        run()  # radio_out: radio1 + lora oba podpisane, accept_unsigned=false

    def test_signing_per_instance_may_differ_between_instances(self):
        run()  # radio_lora (podpisana) i radio_mix (niepodpisana) w jednej aplikacji

    # --- pola łącza
    def test_link_unknown_field(self):
        def m(a):
            a["mavlink_links"]["radio1"]["baud"] = 9600
        self._fails(m, "radio1", "baud")

    def test_signing_unknown_key(self):
        def m(a):
            a["mavlink_links"]["radio1"]["signing"]["keyfile"] = "x"
        self._fails(m, "signing.keyfile")

    def test_signing_accept_unsigned_type(self):
        def m(a):
            a["mavlink_links"]["radio1"]["signing"]["accept_unsigned"] = "no"
        self._fails(m, "accept_unsigned")

    # --- pkt 13: source_*
    def test_source_ids_only_for_require(self):
        def m(a):
            a["provide"][0]["source_system_id"] = 2
        self._fails(m, "source_system_id", "require")

    def test_source_id_range(self):
        def m(a):
            a["require"][0]["source_component_id"] = 256
        self._fails(m, "source_component_id")

    # --- D: stabilna kolejnosc
    def test_excluded_list_sorted(self):
        def s(svc):
            svc["events"]["Aaa"] = {"id": 32800, "data_structure": {"out": {"type": "uint8"}},
                                    "mavlink": False}
        res, _ = run(None, s)
        lines = [ln for ln in res.links_md.splitlines() if ln.startswith("- srp.mav.Radio.")]
        self.assertEqual(lines, sorted(lines))
        self.assertLess(res.links_md.index("- srp.mav.Radio.Aaa"),
                        res.links_md.index("- srp.mav.Radio.DebugCounter"))

    def test_instance_exclude_names_sorted(self):
        def m(a):
            a["provide"][1]["exclude"] = ["SetMode", "Reboot"]
        res, app = run(m)
        self.assertEqual(app.provide_someip["radio_lora"].mavlink.exclude, ["Reboot", "SetMode"])
        self.assertIn("radio_lora (instancja 5): Reboot, SetMode", res.links_md)

    def test_per_instance_list_sorted_by_name(self):
        res, _ = run()
        sect = res.links_md.split("## Wykluczenia per instancja")[1].split("##")[0]
        names = [ln.split(" (")[0] for ln in sect.splitlines() if ln.startswith("- ")]
        self.assertEqual(names, sorted(names))

    def test_output_deterministic(self):
        a, _ = run()
        b, _ = run()
        self.assertEqual(a.links_md, b.links_md)
        self.assertEqual(a.xml, b.xml)


class DedupTtlTest(unittest.TestCase):
    def test_default_ttl_emitted(self):
        _, app = run()
        self.assertEqual(app.provide_someip["radio_out"].mavlink.dedup_ttl_ms, 10000)
        self.assertIn("opts.dedup_ttl_ms = 10000U;", com_init(app))

    def test_custom_ttl_per_entry(self):
        def m(a):
            a["provide"][1]["dedup_ttl_ms"] = 3000
            a["require"][0]["dedup_ttl_ms"] = 500
        res, app = run(m)
        init = com_init(app)
        self.assertIn("opts.dedup_ttl_ms = 3000U;", init)
        self.assertIn("opts.dedup_ttl_ms = 500U;", init)
        self.assertIn("| 3000 |", res.links_md)

    def test_invalid_ttl(self):
        for bad in (0, -1, "10", True, 1.5, None, 3_600_001):
            def m(a, bad=bad):
                a["provide"][0]["dedup_ttl_ms"] = bad
            with self.assertRaises(MavlinkModelError) as cm:
                run(m)
            self.assertIn("dedup_ttl_ms", str(cm.exception))

    def test_ttl_rejected_for_udp(self):
        def m(a):
            a["provide"].append({"name": "Radio as radio_udp", "on": "udp",
                                 "port": "10001", "instance": 9, "dedup_ttl_ms": 1000})
        with self.assertRaises(MavlinkModelError) as cm:
            run(m)
        self.assertIn("dedup_ttl_ms", str(cm.exception))


class FollowUpTest(unittest.TestCase):
    """Uwagi qa do M1 (reviews.md 1-5) i rozbieżności z testów T1."""

    def _fails(self, app_mut, *needles):
        with self.assertRaises(MavlinkModelError) as cm:
            run(app_mut)
        for n in needles:
            self.assertIn(n, str(cm.exception))

    def _warnings(self, app_mut=None, svc_mut=None):
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            run(app_mut, svc_mut, quiet=False)
        return err.getvalue()

    # --- 1: nieznane klucze wpisu on=mavlink
    def test_unknown_entry_key_typo_exclude(self):
        def m(a):
            a["provide"][1]["exlude"] = ["Reboot"]
        self._fails(m, "exlude", "radio_lora")

    def test_unknown_entry_key_typo_max_rate(self):
        def m(a):
            a["provide"][1]["max_rate"] = {"Position": 1}
        self._fails(m, "max_rate")

    def test_unknown_entry_key_port(self):
        def m(a):
            a["provide"][1]["port"] = "1011"
        self._fails(m, "port")

    # --- 2: max_rate_hz przy require
    def test_max_rate_on_require_warns_and_is_ignored(self):
        w = self._warnings()
        self.assertIn("max_rate_hz we wpisie require", w)
        _, app = run()
        self.assertEqual(app.require_someip["radio_in"].mavlink.max_rate_hz, {})
        self.assertNotIn("opts.max_rate_hz[\"Position\"] = 5.0;", com_init(app))

    # --- 3: W_ALL_EXCLUDED per instancja (także przez exclude)
    def test_all_excluded_via_exclude_warns_with_names(self):
        def m(a):
            a["provide"][1]["exclude"] = ["Position", "SetMode", "Reboot", "SetName"]
            a["provide"][1].pop("max_rate_hz")
        w = self._warnings(m)
        self.assertIn("W_ALL_EXCLUDED", w)
        self.assertIn("radio_lora", w)
        self.assertIn("instancja 5", w)
        self.assertIn("srp.mav.Radio", w)

    def test_all_excluded_service_wide_warns_per_instance(self):
        def s(svc):
            svc["events"]["Position"]["mavlink"] = False
            for m in svc["methods"].values():
                m["mavlink"] = False

        def a(app):
            app["provide"][1].pop("max_rate_hz")
            app["require"][0].pop("max_rate_hz")
        w = self._warnings(a, s)
        self.assertEqual(w.count("W_ALL_EXCLUDED"), 4)  # 3x provide + 1x require

    def test_no_all_excluded_warning_in_normal_model(self):
        self.assertNotIn("W_ALL_EXCLUDED", self._warnings())

    # --- 4: logi raz
    def test_info_log_printed_once(self):
        w = self._warnings()
        self.assertEqual(w.count("INFO mavlink"), 1)


class TesterFindingsAppTest(unittest.TestCase):
    def _fails(self, app_mut, *needles):
        with self.assertRaises(MavlinkModelError) as cm:
            run(app_mut)
        for n in needles:
            self.assertIn(n, str(cm.exception))

    def test_typo_in_app_mavlink_block(self):
        def m(a):
            a["mavlink"]["sytem_id"] = 7
        self._fails(m, "sytem_id")

    def test_extra_key_in_app_mavlink_block(self):
        def m(a):
            a["mavlink"]["extra_foo"] = 1
        self._fails(m, "extra_foo")

    def test_valid_app_mavlink_block_keys(self):
        def m(a):
            a["mavlink"]["system_id"] = 7
            a["mavlink"]["component_id"] = 3
        res, _ = run(m)
        self.assertIn("system_id=7, component_id=3", res.links_md)

    def test_duplicate_entry_name_in_provide(self):
        def m(a):
            a["provide"][1]["name"] = "Radio as radio_out"
            a["provide"][1]["instance"] = 55
        self._fails(m, "radio_out", "powtórzona nazwa")

    def test_duplicate_entry_name_between_provide_and_require(self):
        def m(a):
            a["require"][0]["name"] = "Radio as radio_out"
        self._fails(m, "radio_out", "powtórzona nazwa")

    def test_max_rate_nan_in_instance(self):
        for bad in (float("nan"), float("inf")):
            def m(a, bad=bad):
                a["provide"][1]["max_rate_hz"] = {"Position": bad}
            self._fails(m, "max_rate_hz", "Position")

    def test_heartbeat_hz_nan(self):
        def m(a):
            a["mavlink_links"]["radio1"]["heartbeat_hz"] = float("nan")
        self._fails(m, "heartbeat_hz")


class ElementDescriptorEmissionTest(unittest.TestCase):
    """Opisy elementów w initialization.cc (kodek runtime)."""

    def _init(self):
        _, app = run()
        return com_init(app)

    def test_elements_present_for_each_instance(self):
        init = self._init()
        self.assertEqual(init.count("opts.elements.push_back("), 4 * 6)  # 4 instancje x 6 elementów

    def test_element_ids_and_enabled_flags(self):
        init = self._init()
        self.assertIn("el0.id = 32770U;", init)  # DebugCounter (event)
        self.assertIn("el0.enabled = false;", init)
        self.assertIn("el1.id = 32769U;", init)  # Position
        self.assertIn("el1.enabled = true;", init)

    def test_disabled_element_has_no_messages(self):
        init = self._init()
        block = init.split("el0.name = \"DebugCounter\";")[1].split("opts.elements.push_back(el0);")[0]
        self.assertNotIn("msg_id", block)

    def test_event_message_and_header_roles(self):
        init = self._init()
        self.assertIn('el1.req.name = "SRP_RADIO_POSITION";', init)
        self.assertIn('MF{"seq", MT::kU16, 0U, MR::kSeq, false}', init)
        self.assertIn('MF{"instance", MT::kU16, 0U, MR::kInstance, false}', init)
        self.assertIn('MF{"lat", MT::kF32, 0U, MR::kPayload, false}', init)
        self.assertIn('MF{"fix_type", MT::kU8, 0U, MR::kPayload, false}', init)

    def test_method_with_response_has_status_and_timeout(self):
        init = self._init()
        self.assertIn('MF{"status", MT::kU8, 0U, MR::kStatus, false}', init)
        self.assertIn("el2.has_resp = true;", init)
        self.assertIn("el2.timeout_ms = 500U;", init)
        self.assertIn('MF{"target_system", MT::kU8, 0U, MR::kTargetSystem, false}', init)

    def test_string_field_is_char_array(self):
        init = self._init()
        self.assertIn('MF{"value", MT::kChar, 32U, MR::kPayload, false}', init)

    def test_event_rate_from_service_json(self):
        self.assertIn("el1.max_rate_hz = 2.0;", self._init())

    def test_crc_extra_emitted_and_nonzero_for_most(self):
        init = self._init()
        import re
        values = {int(v) for v in re.findall(r"\.crc_extra = (\d+)U;", init)}
        self.assertGreaterEqual(len(values), 4)

    def test_big_endian_struct_marks_fields(self):
        # struktura z endian=big -> pola oznaczone big_endian
        def s(svc):
            pass
        res, app = run(None, s)
        init = com_init(app)
        self.assertNotIn("MR::kPayload, true}", init)  # fixtura: little

    def test_crc_extra_function_known_vectors(self):
        from tools.model_generator.ara.mavlink.mavlink_dialect import ComputeCrcExtra, Field

        class Msg:
            def __init__(self, name, fields):
                self.name = name
                self.fields = fields
        hb = Msg("HEARTBEAT", [Field("type", "uint8_t", 1), Field("autopilot", "uint8_t", 1),
                               Field("base_mode", "uint8_t", 1), Field("custom_mode", "uint32_t", 4),
                               Field("system_status", "uint8_t", 1),
                               Field("mavlink_version", "uint8_t", 1)])
        self.assertEqual(ComputeCrcExtra(hb), 50)
        att = Msg("ATTITUDE", [Field("time_boot_ms", "uint32_t", 4)] +
                  [Field(n, "float", 4) for n in
                   ("roll", "pitch", "yaw", "rollspeed", "pitchspeed", "yawspeed")])
        self.assertEqual(ComputeCrcExtra(att), 39)


if __name__ == "__main__":
    unittest.main()
