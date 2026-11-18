"""Lista nazw pól zastrzeżonych vs klasa bazowa wiadomości z pymavlink.

Pole o nazwie metody/atrybutu klasy MAVLink_message psuje wygenerowany moduł
Pythona (SyntaxError / TypeError przy imporcie lub pakowaniu), a mavgen tego
nie wykrywa. Test porównuje listę generatora z `dir()` klasy z pymavlink.
"""
from __future__ import annotations

import importlib
import unittest

from tools.model_generator.ara.mavlink.mavlink_dialect import (
    RESERVED_FIELD_NAMES, RESERVED_WORDS)


def _members(module_name: str) -> set[str]:
    mod = importlib.import_module(module_name)
    cls = mod.MAVLink_message
    names = set(dir(cls))
    try:
        names |= set(vars(cls(1, "X")))
    except Exception:  # noqa: BLE001 - inna sygnatura w danej wersji pymavlink
        pass
    return names


# Składowe klasy, które w praktyce nie psują modułu (sprawdzone interopem).
ALLOWED_DESPITE_MEMBER = {"id", "fieldnames", "msgname"}


class ReservedPythonNamesTest(unittest.TestCase):
    def test_every_message_member_is_reserved(self):
        for module in ("pymavlink.dialects.v20.common", "pymavlink.dialects.v10.common"):
            try:
                names = _members(module)
            except ImportError:
                self.skipTest(f"brak {module}")
            missing = sorted(
                n for n in names
                if not n.startswith("_") and n not in RESERVED_FIELD_NAMES
                and n not in RESERVED_WORDS and n not in ALLOWED_DESPITE_MEMBER)
            self.assertEqual(missing, [], f"{module}: dodaj do RESERVED_FIELD_NAMES")


class GeneratedPythonImportTest(unittest.TestCase):
    """Wygenerowany moduł musi się importować i pakować dla pól-pułapek, które
    generator DOPUSZCZA (id, type, len, format, crc, payload, print...)."""

    NAMES = ("id", "type", "len", "format", "crc", "payload", "print", "msgname",
             "fieldnames", "count", "name", "value_x")

    def test_allowed_tricky_field_names_work_in_generated_python(self):
        import os
        import tempfile
        try:
            from pymavlink.generator import mavgen
        except ImportError:
            self.skipTest("pymavlink niedostępny")
        from tools.model_generator.ara.mavlink import mavgen_run
        from tools.model_generator.ara.mavlink.mavlink_dialect import (
            BuildMessages, RenderXml, Validate)
        from tools.model_generator.ara.mavlink.test_mavlink_dialect import (
            _event_service, generate)  # noqa: F401
        from tools.model_generator.ara.someip.lib.someip_db import SomeIpDb
        struct = {"S": {n: "uint8" for n in self.NAMES}}
        generate(_event_service("S"), extra_types=[struct])
        msgs = BuildMessages(SomeIpDb().service_list["srp.t.Radio"], 2)
        Validate(msgs, 2)
        xml = RenderXml("srp_tricky", msgs)
        with tempfile.TemporaryDirectory() as d:
            xml_path = os.path.join(d, "srp_tricky.xml")
            with open(xml_path, "w", encoding="utf-8") as f:
                f.write(xml)
            import shutil
            shutil.copy(mavgen_run._minimal_xml(), os.path.join(d, "minimal.xml"))
            out = os.path.join(d, "srp_tricky.py")
            opts = mavgen.Opts(out, wire_protocol="2.0", language="Python3", validate=True,
                               error_limit=200, strict_units=False)
            self.assertTrue(mavgen.mavgen(opts, [xml_path]))
            import importlib.util
            spec = importlib.util.spec_from_file_location("srp_tricky", out)
            mod = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(mod)
            cls = mod.MAVLink_srp_radio_e_message
            kwargs = {"seq": 1, "instance": 2}
            kwargs.update({n: i + 1 for i, n in enumerate(self.NAMES)})
            msg = cls(**kwargs)
            frame = msg.pack(mod.MAVLink(None, srcSystem=1, srcComponent=1))
            back = mod.MAVLink(None).parse_buffer(frame)
            self.assertEqual(len(back), 1)
            for i, n in enumerate(self.NAMES):
                self.assertEqual(getattr(back[0], n), i + 1, n)


class CrcExtraAndFieldOrderTest(unittest.TestCase):
    """crc_extra i kolejność pól przewodowych zgodne z mavgen (podstawa kodeka C++)."""

    def test_crc_extra_and_order_match_mavgen_for_example_app(self):
        import importlib.util
        import os
        import shutil
        import tempfile
        try:
            from pymavlink.generator import mavgen
        except ImportError:
            self.skipTest("pymavlink niedostępny")
        from tools.model_generator.ara.mavlink import mavgen_run
        from tools.model_generator.ara.mavlink.mavlink_app import AppMessages
        from tools.model_generator.ara.mavlink.test_mavlink_app import run
        res, app = run()
        msgs = AppMessages(app.name, app.GetMavlinkEntries())
        with tempfile.TemporaryDirectory() as d:
            xml_path = os.path.join(d, "srp_radio.xml")
            with open(xml_path, "w", encoding="utf-8") as f:
                f.write(res.xml)
            shutil.copy(mavgen_run._minimal_xml(), os.path.join(d, "minimal.xml"))
            out = os.path.join(d, "srp_radio.py")
            opts = mavgen.Opts(out, wire_protocol="2.0", language="Python3", validate=True,
                               error_limit=200, strict_units=False)
            self.assertTrue(mavgen.mavgen(opts, [xml_path]))
            spec = importlib.util.spec_from_file_location("srp_radio_x", out)
            mod = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(mod)
            self.assertGreaterEqual(len(msgs), 6)
            for m in msgs:
                cls = mod.mavlink_map[m.msg_id]
                self.assertEqual(cls.crc_extra, m.crc_extra, m.name)
                ordered = [f.name for f in sorted(
                    m.fields, key=lambda f: -(f.size // max(1, f.array_len)))]
                self.assertEqual(list(cls.ordered_fieldnames), ordered, m.name)


if __name__ == "__main__":
    unittest.main()
