"""Testy generatora dialektu MAVLink: golden XML + walidacja modelu."""
from __future__ import annotations

import copy
import json
import os
import tempfile
import unittest

from tools.model_generator.ara.common.data_structure_db import DataStructureDB
from tools.model_generator.ara.mavlink.mavlink_dialect import (
    BuildMessages, ExcludedElements, MavlinkModelError)
from tools.model_generator.ara.mavlink.mavlink_gen import Generate
from tools.model_generator.ara.someip.lib.someip_db import SomeIpDb

HERE = os.path.dirname(os.path.abspath(__file__))
GOLDEN = os.path.join(HERE, "testdata")

DATA_TYPES = {
    "package": "srp.t",
    "data_structure": {"Pos": {"lat": "float32", "lon": "float32", "fixType": "uint8"}},
}


def _ds(typ):
    return {"type": typ}


def base_service():
    return {
        "package": "srp.t",
        "someip": {"Radio": {
            "service_id": 7, "major_version": 1, "minor_version": 0,
            "events": {
                "Position": {"id": 32769,
                             "data_structure": {"out": _ds("Pos")},
                             "mavlink": {"msg_id": 42000, "max_rate_hz": 2}},
            },
            "methods": {
                "SetMode": {"id": 1,
                            "data_structure": {"in": _ds("uint8"), "out": _ds("bool")},
                            "mavlink": {"req_id": 42010, "resp_id": 42011,
                                        "timeout_ms": 500}},
                "Reboot": {"id": 2,
                           "data_structure": {"in": _ds("void"), "out": _ds("void")},
                           "mavlink": {"req_id": 42020}},
                "SetName": {"id": 3,
                            "data_structure": {"in": _ds("string"), "out": _ds("uint32")},
                            "mavlink": {"req_id": 42030, "resp_id": 42031,
                                        "max_len": 32}},
            },
        }},
    }


def v1_service():
    return {
        "package": "srp.t",
        "someip": {"Radio": {
            "service_id": 7, "major_version": 1, "minor_version": 0,
            "events": {
                "Position": {"id": 32769, "data_structure": {"out": _ds("Pos")},
                             "mavlink": {"msg_id": 200}},
                "Temp": {"id": 32770, "data_structure": {"out": _ds("float64")},
                         "mavlink": {"msg_id": 201}},
            },
            "methods": {
                "SetMode": {"id": 1,
                            "data_structure": {"in": _ds("int16"), "out": _ds("bool")},
                            "mavlink": {"req_id": 210, "resp_id": 211}},
            },
        }},
    }


def generate(service, version=2, dialect="srp_radio", extra_types=None):
    SomeIpDb().service_list.clear()
    DataStructureDB().data_structure.clear()
    with tempfile.TemporaryDirectory() as d:
        dt = os.path.join(d, "types.json")
        with open(dt, "w") as f:
            json.dump(DATA_TYPES, f)
        includes = [dt]
        for i, t in enumerate(extra_types or []):  # parser czyta jedną strukturę na plik
            pt = os.path.join(d, f"t{i}.json")
            with open(pt, "w") as f:
                json.dump({"package": "srp.t", "data_structure": t}, f)
            includes.append(pt)
        svc = copy.deepcopy(service)
        svc["include"] = includes
        sp = os.path.join(d, "service.json")
        with open(sp, "w") as f:
            json.dump(svc, f)
        return Generate([sp], dialect, version)


def read_golden(name):
    with open(os.path.join(GOLDEN, name), encoding="utf-8") as f:
        return f.read()


class GoldenTest(unittest.TestCase):
    def test_v2_golden(self):
        self.assertEqual(generate(base_service(), 2), read_golden("srp_radio_v2.xml"))

    def test_v1_golden(self):
        self.assertEqual(generate(v1_service(), 1, "srp_radio_v1"),
                         read_golden("srp_radio_v1.xml"))

    def test_deterministic(self):
        self.assertEqual(generate(base_service()), generate(base_service()))

    def test_void_method_has_only_req(self):
        xml = generate(base_service())
        self.assertIn("SRP_RADIO_REBOOT_REQ", xml)
        self.assertNotIn("SRP_RADIO_REBOOT_RESP", xml)

    def test_string_becomes_char_array(self):
        self.assertIn('type="char[32]" name="value"', generate(base_service()))

    def test_struct_flattened_snake_case(self):
        self.assertIn('name="fix_type"', generate(base_service()))

    def test_event_has_seq_and_instance_header(self):
        xml = generate(base_service())
        ev = xml.split('name="SRP_RADIO_POSITION"')[1].split("</message>")[0]
        self.assertLess(ev.index('name="seq"'), ev.index('name="instance"'))
        self.assertIn('type="uint16_t" name="instance"', ev)
        self.assertNotIn("target_system", ev)

    def test_method_header_order(self):
        xml = generate(base_service())
        for suffix in ("REQ", "RESP"):
            m = xml.split(f'name="SRP_RADIO_SET_MODE_{suffix}"')[1].split("</message>")[0]
            pos = [m.index(f'name="{n}"') for n in
                   ("seq", "instance", "target_system", "target_component")]
            self.assertEqual(pos, sorted(pos))

    def test_resp_has_status_header(self):
        xml = generate(base_service())
        resp = xml.split('name="SRP_RADIO_SET_MODE_RESP"')[1].split("</message>")[0]
        for n in ("seq", "instance", "target_system", "target_component", "status", "value"):
            self.assertIn(f'name="{n}"', resp)


class ValidationTest(unittest.TestCase):
    def _fails(self, mutate, version=2, base=base_service, *needles):
        svc = base()
        mutate(svc["someip"]["Radio"])
        with self.assertRaises(MavlinkModelError) as cm:
            generate(svc, version)
        msg = str(cm.exception)
        for n in needles:
            self.assertIn(n, msg)
        return msg

    def test_missing_block_means_excluded(self):
        svc = base_service()
        del svc["someip"]["Radio"]["events"]["Position"]["mavlink"]
        xml = generate(svc)
        self.assertNotIn("SRP_RADIO_POSITION", xml)
        self.assertEqual(ExcludedElements(SomeIpDb().service_list["srp.t.Radio"]),
                         ["srp.t.Radio.Position"])

    def test_false_means_excluded(self):
        svc = base_service()
        svc["someip"]["Radio"]["methods"]["Reboot"]["mavlink"] = False
        xml = generate(svc)
        self.assertNotIn("SRP_RADIO_REBOOT", xml)
        self.assertIn("SRP_RADIO_SET_MODE_REQ", xml)

    def test_no_blocks_at_all(self):
        def m(s):
            del s["events"]["Position"]["mavlink"]
            for x in s["methods"].values():
                del x["mavlink"]
        self._fails(m, 2, base_service, "mavlink")

    def test_id_collision(self):
        def m(s):
            s["methods"]["SetMode"]["mavlink"]["req_id"] = 42000
        self._fails(m, 2, base_service, "42000", "Radio")

    def test_two_events_with_same_someip_id(self):
        def m(s):
            s["events"]["Second"] = {"id": 32769, "data_structure": {"out": _ds("Pos")},
                                     "mavlink": {"msg_id": 42001}}
        self._fails(m, 2, base_service, "32769", "Second", "Position", "unikalne")

    def test_two_methods_with_same_someip_id(self):
        def m(s):
            s["methods"]["Reboot"]["id"] = 1
        self._fails(m, 2, base_service, "SOME/IP id 1", "Reboot")

    def test_excluded_event_with_same_id_still_collides(self):
        def m(s):
            s["events"]["Hidden"] = {"id": 32769, "data_structure": {"out": _ds("Pos")},
                                     "mavlink": False}
        self._fails(m, 2, base_service, "SOME/IP id 32769")

    def test_collision_with_heartbeat(self):
        def m(s):
            s["events"]["Position"]["mavlink"]["msg_id"] = 0
        self._fails(m, 2, base_service, "HEARTBEAT", "Position")

    def test_v1_id_over_255(self):
        def m(s):
            s["events"]["Position"]["mavlink"]["msg_id"] = 256
        self._fails(m, 1, v1_service, "255", "Position")

    def test_v1_rejects_v2_ids_in_methods(self):
        def m(s):
            s["methods"]["SetMode"]["mavlink"]["resp_id"] = 70000
        self._fails(m, 1, v1_service, "70000", "SetMode")

    def test_payload_over_255(self):
        def m(s):
            s["methods"]["SetName"]["mavlink"]["max_len"] = 255
        self._fails(m, 2, base_service, "255", "SetName")

    def test_string_without_max_len(self):
        def m(s):
            del s["methods"]["SetName"]["mavlink"]["max_len"]
        self._fails(m, 2, base_service, "max_len", "SetName")

    def test_unknown_type(self):
        svc = base_service()
        svc["someip"]["Radio"]["methods"]["SetMode"]["data_structure"]["in"]["type"] = "nieznany"
        with self.assertRaises(Exception) as cm:
            generate(svc)
        self.assertIn("nieznany", str(cm.exception))

    def test_missing_resp_id(self):
        def m(s):
            del s["methods"]["SetMode"]["mavlink"]["resp_id"]
        self._fails(m, 2, base_service, "resp_id", "SetMode")

    def test_void_method_with_resp_id(self):
        def m(s):
            s["methods"]["Reboot"]["mavlink"]["resp_id"] = 42021
        self._fails(m, 2, base_service, "Reboot")

    def test_bad_timeout(self):
        def m(s):
            s["methods"]["SetMode"]["mavlink"]["timeout_ms"] = 0
        self._fails(m, 2, base_service, "timeout_ms")

    def test_bad_max_rate(self):
        def m(s):
            s["events"]["Position"]["mavlink"]["max_rate_hz"] = -1
        self._fails(m, 2, base_service, "max_rate_hz")

    def test_message_name_collision(self):
        def m(s):
            s["methods"]["Set_Mode"] = copy.deepcopy(s["methods"]["SetMode"])
            s["methods"]["Set_Mode"]["id"] = 9
            s["methods"]["Set_Mode"]["mavlink"] = {"req_id": 43000, "resp_id": 43001}
        self._fails(m, 2, base_service, "SRP_RADIO_SET_MODE")

    def test_message_ids_helper(self):
        generate(base_service())
        msgs = BuildMessages(SomeIpDb().service_list["srp.t.Radio"], 2)
        self.assertEqual(sorted(m.msg_id for m in msgs),
                         [42000, 42010, 42011, 42020, 42030, 42031])


def _event_service(type_name, **block):
    svc = base_service()
    radio = svc["someip"]["Radio"]
    radio["methods"] = {}
    radio["events"] = {"E": {"id": 32769, "data_structure": {"out": _ds(type_name)},
                             "mavlink": dict({"msg_id": 42000}, **block)}}
    return svc


class FieldNameTest(unittest.TestCase):
    def _fails(self, struct, *needles):
        with self.assertRaises(MavlinkModelError) as cm:
            generate(_event_service("S"), extra_types=[{"S": struct}])
        for n in needles:
            self.assertIn(n, str(cm.exception))

    def test_header_names_rejected(self):
        for field in ("seq", "instance", "targetSystem", "targetComponent", "status"):
            snake = {"targetSystem": "target_system", "targetComponent": "target_component"}.get(
                field, field)
            self._fails({field: "uint8"}, snake, "nagłówkiem")

    def test_reserved_words_rejected(self):
        for word in ("class", "end", "int", "while", "lambda", "nil", "register"):
            self._fails({word: "uint8"}, word, "zastrzeżonym")

    def test_ordinary_names_pass(self):
        generate(_event_service("S"), extra_types=[{"S": {"lat": "float32", "fixType": "uint8"}}])

    def test_event_collision_message_names_service_and_element(self):
        self._fails({"status": "uint8"}, "Radio.E")


class PayloadLimitTest(unittest.TestCase):
    def _event(self, n):
        svc = _event_service("string", max_len=n)
        return generate(svc)

    def test_event_255_with_header_ok(self):
        self._event(251)  # seq(2) + instance(2) + 251 = 255

    def test_event_over_255_with_header(self):
        with self.assertRaises(MavlinkModelError) as cm:
            self._event(252)
        self.assertIn("256", str(cm.exception))

    def test_method_req_limit_includes_full_header(self):
        svc = base_service()
        svc["someip"]["Radio"]["methods"]["SetName"]["mavlink"]["max_len"] = 249  # 6 (seq,instance,target_*) + 249
        generate(svc)
        svc["someip"]["Radio"]["methods"]["SetName"]["mavlink"]["max_len"] = 250
        with self.assertRaises(MavlinkModelError):
            generate(svc)


class ElementKeyTest(unittest.TestCase):
    def _svc(self, mutate):
        svc = base_service()
        mutate(svc["someip"]["Radio"])
        return svc

    def test_typo_keys_rejected(self):
        for typo in ("mavlnk", "Mavlink", "MAVLINK", "mavlink_", "mavlin", "mavlinks", "mavlnik"):
            def m(s, typo=typo):
                el = s["events"]["Position"]
                el[typo] = el.pop("mavlink")
            with self.assertRaises(MavlinkModelError) as cm:
                generate(self._svc(m))
            self.assertIn(typo, str(cm.exception))
            self.assertIn("Position", str(cm.exception))

    def test_typo_on_method(self):
        def m(s):
            el = s["methods"]["SetMode"]
            el["mavlnk"] = el.pop("mavlink")
        with self.assertRaises(MavlinkModelError):
            generate(self._svc(m))

    def test_unrelated_extra_key_allowed(self):
        def m(s):
            s["events"]["Position"]["desc"] = "opis"
        generate(self._svc(m))

    def test_empty_block_rejected(self):
        def m(s):
            s["events"]["Position"]["mavlink"] = {}
        with self.assertRaises(MavlinkModelError) as cm:
            generate(self._svc(m))
        self.assertIn("msg_id", str(cm.exception))

    def test_true_block_rejected(self):
        def m(s):
            s["events"]["Position"]["mavlink"] = True
        with self.assertRaises(MavlinkModelError):
            generate(self._svc(m))

    def test_block_wrong_types_rejected(self):
        for bad in ("42000", 4.5, None, [1], True):
            def m(s, bad=bad):
                s["events"]["Position"]["mavlink"] = {"msg_id": bad}
            if bad is None:
                with self.assertRaises(MavlinkModelError):
                    generate(self._svc(m))
                continue
            with self.assertRaises(MavlinkModelError):
                generate(self._svc(m))

    def test_unknown_block_field_rejected(self):
        def m(s):
            s["methods"]["SetMode"]["mavlink"]["timeout"] = 5
        with self.assertRaises(MavlinkModelError) as cm:
            generate(self._svc(m))
        self.assertIn("timeout", str(cm.exception))

    def test_event_block_with_method_fields_rejected(self):
        def m(s):
            s["events"]["Position"]["mavlink"]["req_id"] = 1
        with self.assertRaises(MavlinkModelError):
            generate(self._svc(m))

    def test_method_missing_req_id(self):
        def m(s):
            del s["methods"]["SetMode"]["mavlink"]["req_id"]
        with self.assertRaises(MavlinkModelError) as cm:
            generate(self._svc(m))
        self.assertIn("req_id", str(cm.exception))


class CollisionWarningTest(unittest.TestCase):
    def test_fake_index_produces_warning(self):
        from tools.model_generator.ara.mavlink.mavlink_collisions import CollisionWarnings
        generate(base_service())
        msgs = BuildMessages(SomeIpDb().service_list["srp.t.Radio"], 2)
        warns = CollisionWarnings(msgs, {42000: ["common:FAKE"]})
        self.assertEqual(len(warns), 1)
        self.assertIn("42000", warns[0])
        self.assertIn("common:FAKE", warns[0])

    def test_no_collision_no_warning(self):
        from tools.model_generator.ara.mavlink.mavlink_collisions import CollisionWarnings
        generate(base_service())
        msgs = BuildMessages(SomeIpDb().service_list["srp.t.Radio"], 2)
        self.assertEqual(CollisionWarnings(msgs, {1: ["common:SYS_STATUS"]}), [])

    def test_real_pymavlink_index_when_available(self):
        from tools.model_generator.ara.mavlink.mavlink_collisions import PymavlinkIdIndex
        idx = PymavlinkIdIndex()
        if not idx:
            self.skipTest("pymavlink niedostępny w tym środowisku")
        self.assertTrue(any(n.startswith("common:") for n in idx.get(1, [])))
        self.assertNotIn(0, idx)  # HEARTBEAT: błąd walidacji, nie ostrzeżenie

    def test_collision_with_own_dialect_is_error(self):
        def m(s):
            s["events"]["Position"]["mavlink"]["msg_id"] = 42010
        svc = base_service()
        m(svc["someip"]["Radio"])
        with self.assertRaises(MavlinkModelError):
            generate(svc)


class FollowUpDialectTest(unittest.TestCase):
    def test_typo_message_has_service_and_element(self):
        svc = base_service()
        el = svc["someip"]["Radio"]["events"]["Position"]
        el["mavlnk"] = el.pop("mavlink")
        with self.assertRaises(MavlinkModelError) as cm:
            generate(svc)
        self.assertIn("Radio.Position", str(cm.exception))
        self.assertIn("mavlnk", str(cm.exception))

    def test_typo_case_message_has_service_and_method(self):
        svc = base_service()
        el = svc["someip"]["Radio"]["methods"]["SetMode"]
        el["Mavlink"] = el.pop("mavlink")
        with self.assertRaises(MavlinkModelError) as cm:
            generate(svc)
        self.assertIn("Radio.SetMode", str(cm.exception))

    def test_unknown_type_is_generator_error_not_traceback(self):
        svc = _event_service("complex128")
        with self.assertRaises(MavlinkModelError) as cm:
            generate(svc)
        msg = str(cm.exception)
        self.assertIn("Radio.E", msg)
        self.assertIn("complex128", msg)

    def test_unknown_type_in_method_param(self):
        svc = base_service()
        svc["someip"]["Radio"]["methods"]["SetMode"]["data_structure"]["in"]["type"] = "nieznany"
        with self.assertRaises(MavlinkModelError) as cm:
            generate(svc)
        self.assertIn("Radio.SetMode", str(cm.exception))

    def test_unknown_type_inside_struct_definition(self):
        svc = _event_service("S")
        with self.assertRaises(MavlinkModelError) as cm:
            generate(svc, extra_types=[{"S": {"a": "uint8", "b": "float128"}}])
        self.assertIn("float128", str(cm.exception))
        self.assertIn("S.b", str(cm.exception))

    def test_description_from_model_desc(self):
        svc = base_service()
        svc["someip"]["Radio"]["events"]["Position"]["desc"] = "Pozycja GPS"
        svc["someip"]["Radio"]["methods"]["SetMode"]["desc"] = "Ustaw tryb"
        xml = generate(svc)
        self.assertIn("<description>Pozycja GPS</description>", xml)
        self.assertIn("<description>Ustaw tryb</description>", xml)
        self.assertIn("<description>Ustaw tryb (odpowiedź)</description>", xml)

    def test_description_fallback_when_no_desc(self):
        xml = generate(base_service())
        self.assertIn("<description>Event srp.t.Radio.Position</description>", xml)


class TesterFindingsDialectTest(unittest.TestCase):
    """Rozbieżności z test-m1.md: pola-pułapki Pythona, nazwy elementów, NaN."""

    def _field_fails(self, name):
        with self.assertRaises(MavlinkModelError) as cm:
            generate(_event_service("S"), extra_types=[{"S": {name: "uint8"}}])
        self.assertIn(name, str(cm.exception))
        self.assertIn("Radio.E", str(cm.exception))

    def test_python_message_members_rejected(self):
        for name in ("self", "force_mavlink1", "pack", "get_msgbuf", "get_header", "to_dict",
                     "to_json", "format_attr", "sign_packet", "unpacker", "crc_extra",
                     "lengths", "orders", "native_format", "ordered_fieldnames",
                     "fieldtypes", "array_lengths", "get_seq"):
            self._field_fails(name)

    def test_harmless_member_names_allowed(self):
        for name in ("id", "fieldnames", "msgname", "type", "len", "format", "crc",
                     "payload", "print", "count"):
            generate(_event_service("S"), extra_types=[{"S": {name: "uint8"}}])

    def test_leading_underscore_or_digit_field_rejected(self):
        for name in ("_hidden", "1abc"):
            with self.assertRaises(MavlinkModelError):
                generate(_event_service("S"), extra_types=[{"S": {name: "uint8"}}])

    def test_element_names_outside_ascii_identifier_rejected(self):
        for bad in ("Bad Name", "Bad-Name", "Bad.Name", "zażółć", "über", "1Abc"):
            svc = base_service()
            svc["someip"]["Radio"]["events"][bad] = {
                "id": 32800, "data_structure": {"out": _ds("uint8")}, "mavlink": {"msg_id": 43000}}
            with self.assertRaises(MavlinkModelError) as cm:
                generate(svc)
            self.assertIn(bad, str(cm.exception))

    def test_method_name_outside_ascii_identifier_rejected(self):
        svc = base_service()
        svc["someip"]["Radio"]["methods"]["Set Mode2"] = {
            "id": 9, "data_structure": {"in": _ds("void"), "out": _ds("void")},
            "mavlink": {"req_id": 43100}}
        with self.assertRaises(MavlinkModelError) as cm:
            generate(svc)
        self.assertIn("Set Mode2", str(cm.exception))

    def test_excluded_element_with_odd_name_is_not_validated(self):
        svc = base_service()
        svc["someip"]["Radio"]["events"]["Odd Name"] = {
            "id": 32801, "data_structure": {"out": _ds("uint8")}, "mavlink": False}
        generate(svc)  # poza MAVLinkiem: nazwa nie trafia do dialektu

    def test_max_rate_nan_and_infinity_rejected(self):
        for bad in (float("nan"), float("inf"), float("-inf")):
            svc = base_service()
            svc["someip"]["Radio"]["events"]["Position"]["mavlink"]["max_rate_hz"] = bad
            with self.assertRaises(MavlinkModelError) as cm:
                generate(svc)
            self.assertIn("max_rate_hz", str(cm.exception))


if __name__ == "__main__":
    unittest.main()
