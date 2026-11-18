"""Testy generatora per CPU (rev. 10): wiele aplikacji na jednym MavlinkDaemonie, config daemona."""
from __future__ import annotations

import contextlib
import io
import json
import os
import tempfile
import unittest

from tools.model_generator.ara.app.adaptive_application_db import AdaptiveApplicationDb
from tools.model_generator.ara.common.data_structure_db import DataStructureDB
from tools.model_generator.ara.mavlink import mavlink_gen
from tools.model_generator.ara.mavlink.mavlink_daemon_config import (
    ParseDaemonConfig, LoadDaemonConfig)
from tools.model_generator.ara.mavlink.mavlink_errors import MavlinkModelError
from tools.model_generator.ara.mavlink.mavlink_gen import GenerateAll
from tools.model_generator.ara.someip.lib.someip_db import SomeIpDb

HERE = os.path.dirname(os.path.abspath(__file__))
APP_DIR = os.path.join(HERE, "testdata", "app")


def _load(name):
    with open(os.path.join(APP_DIR, name), encoding="utf-8") as f:
        return json.load(f)


def _app2(instance=8, links=("lora",), service="Radio", name="MavApp2", direction="require"):
    """Druga aplikacja CPU: logger jak w fixturze, jedna instancja usługi."""
    app = _load("app.json")
    a = app["adaptive_application"].pop("MavApp")
    a[direction] = [{"name": service, "on": "mavlink", "instance": instance,
                     "links": list(links)}]
    for other in ("provide", "require"):
        if other != direction:
            a.pop(other, None)
    app["adaptive_application"] = {name: a}
    return app


def _beacon(msg_id):
    return {"package": "srp.mav", "someip": {"Beacon": {
        "service_id": 21, "major_version": 1, "minor_version": 0,
        "events": {"Ping": {"id": 32769, "data_structure": {"out": {"type": "uint32"}},
                            "mavlink": {"msg_id": msg_id}}}}}}


def run_cpu(app2=None, svc2=None, daemon_mut=None, app1_mut=None, dialect="cpu_a"):
    """Generuje artefakty dla CPU z dwóch aplikacji; zwraca Result."""
    SomeIpDb().service_list.clear()
    DataStructureDB().data_structure.clear()
    AdaptiveApplicationDb().app_list.clear()
    types, svc = _load("types.json"), _load("service.json")
    app1, dmn = _load("app.json"), _load("daemon_config.json")
    if app1_mut:
        app1_mut(app1["adaptive_application"]["MavApp"])
    if daemon_mut:
        daemon_mut(dmn)
    with tempfile.TemporaryDirectory() as d:
        def path(n):
            return os.path.join(d, n + ".json")
        svc["include"] = [path("types")]
        app1["include"] = [path("service")]
        files = {"types": types, "service": svc, "app1": app1, "daemon": dmn}
        apps = [path("app1")]
        if app2 is not None:
            app2["include"] = [path("service")] + ([path("svc2")] if svc2 else [])
            files["app2"] = app2
            apps.append(path("app2"))
        if svc2 is not None:
            svc2["include"] = [path("types")]
            files["svc2"] = svc2
        for n, obj in files.items():
            with open(path(n), "w", encoding="utf-8") as f:
                json.dump(obj, f)
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            return GenerateAll(apps, dialect, daemon_config=path("daemon"))


class CpuGenerationTest(unittest.TestCase):
    def test_two_apps_share_one_dialect(self):
        res = run_cpu(_app2())
        self.assertEqual(res.xml.count("<message "), 6)  # usługa Radio raz, nie dwa razy
        self.assertIn("Aplikacje: srp.mav.MavApp, srp.mav.MavApp2", res.links_md)
        self.assertIn("| MavApp2 | Radio (srp.mav.Radio) | require | 8 | lora |",
                      res.links_md)
        self.assertEqual(res.versions, [1, 2])

    def test_matrix_names_apps_and_instances_on_the_link(self):
        res = run_cpu(_app2())
        row = next(l for l in res.links_md.splitlines()
                   if l.startswith("| srp.mav.Radio.Position (event)"))
        self.assertIn("MavApp2.8 (proxy) @ 2 Hz", row)

    def test_msg_id_collision_between_apps_is_generation_error(self):
        with self.assertRaises(MavlinkModelError) as cm:
            run_cpu(_app2(service="Beacon", instance=1), svc2=_beacon(200))
        text = str(cm.exception)
        self.assertIn("kolizja msg_id 200", text)
        self.assertIn("srp.mav.MavApp2", text)
        self.assertIn("srp.mav.MavApp", text)

    def test_distinct_msg_ids_between_apps_ok(self):
        res = run_cpu(_app2(service="Beacon", instance=1), svc2=_beacon(300))
        self.assertIn('name="SRP_BEACON_PING"', res.xml)
        self.assertIn("| 300 | SRP_BEACON_PING | 8 |", res.links_md)

    def test_same_instance_same_role_in_two_apps_is_conflict(self):
        with self.assertRaises(MavlinkModelError) as cm:
            run_cpu(_app2(instance=3, direction="provide"))
        self.assertIn("konflikt instancji", str(cm.exception))
        self.assertIn("srp.mav.MavApp2", str(cm.exception))

    def test_provide_and_require_same_instance_in_two_apps_ok(self):
        res = run_cpu(_app2(instance=3, direction="require"))
        self.assertIn("| MavApp2 | Radio (srp.mav.Radio) | require | 3 |", res.links_md)

    def test_unknown_link_in_second_app_names_the_app(self):
        with self.assertRaises(MavlinkModelError) as cm:
            run_cpu(_app2(links=("nie_ma",)))
        self.assertIn("srp.mav.MavApp2", str(cm.exception))
        self.assertIn("nie_ma", str(cm.exception))

    def test_v1_link_limits_ids_across_cpu(self):
        # Beacon (id 300 > 255) na łączu v1 (radio2) = błąd generacji
        with self.assertRaises(MavlinkModelError) as cm:
            run_cpu(_app2(service="Beacon", instance=1, links=("radio2",)), svc2=_beacon(300))
        self.assertIn("300", str(cm.exception))

    def test_second_app_uses_only_instances(self):
        app2 = _app2()
        app2["adaptive_application"]["MavApp2"]["mavlink"] = {"dialect": "x"}
        with self.assertRaises(MavlinkModelError) as cm:
            run_cpu(app2)
        self.assertIn("MavlinkDaemon", str(cm.exception))

    def test_output_is_deterministic(self):
        a = run_cpu(_app2()).links_md
        b = run_cpu(_app2()).xml
        self.assertEqual(run_cpu(_app2()).links_md, a)
        self.assertEqual(run_cpu(_app2()).xml, b)

    def test_without_daemon_config_names_are_not_checked(self):
        SomeIpDb().service_list.clear()
        DataStructureDB().data_structure.clear()
        AdaptiveApplicationDb().app_list.clear()
        with tempfile.TemporaryDirectory() as d:
            paths = {n: os.path.join(d, n + ".json") for n in ("types", "service", "app")}
            svc, app = _load("service.json"), _load("app.json")
            svc["include"] = [paths["types"]]
            app["include"] = [paths["service"]]
            for n, obj in (("types", _load("types.json")), ("service", svc), ("app", app)):
                with open(paths[n], "w", encoding="utf-8") as f:
                    json.dump(obj, f)
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                res = GenerateAll([paths["app"]], "cpu_b")
        self.assertIn("Łącza (nazwy z `links` aplikacji", res.links_md)
        self.assertEqual(res.versions, [2])


class DaemonConfigTest(unittest.TestCase):
    def _cfg(self, **kw):
        cfg = {"system_id": 1, "component_id": 1,
               "links": {"lora": {"driver": "serial_radio", "driver_config": "/etc/lora.json"}}}
        cfg.update(kw)
        return cfg

    def _fails(self, raw, *needles):
        with self.assertRaises(MavlinkModelError) as cm:
            ParseDaemonConfig(raw)
        for n in needles:
            self.assertIn(n, str(cm.exception))

    def test_valid_minimal_and_defaults(self):
        cfg = ParseDaemonConfig(self._cfg())
        link = cfg.links["lora"]
        self.assertEqual((link.driver, link.driver_config, link.version, link.heartbeat_hz),
                         ("serial_radio", "/etc/lora.json", 2, 1))
        self.assertFalse(link.signing.enabled)

    def test_all_documented_keys(self):
        ParseDaemonConfig({
            "system_id": 7, "component_id": 3, "socket_path": "/run/x.sock",
            "socket_mode": "0660", "driver_dir": "/usr/lib/drivers", "restart_delay_ms": 1000,
            "links": {"lora": {"driver": "ebyte_e220", "driver_config": "/etc/e220.json",
                               "version": 2, "heartbeat_hz": 0.5,
                               "signing": {"enabled": True, "key_file": "/k", "link_id": 4,
                                           "state_file": "/var/s", "accept_unsigned": True}}}})

    def test_unknown_top_level_key(self):
        self._fails(self._cfg(sytem_id=3), "sytem_id")

    def test_links_required(self):
        self._fails({"system_id": 1, "component_id": 1}, "links")

    def test_system_and_component_id_required_like_in_the_daemon_parser(self):
        links = self._cfg()["links"]
        self._fails({"component_id": 1, "links": links}, "brak pola 'system_id'")
        self._fails({"system_id": 1, "links": links}, "brak pola 'component_id'")

    def test_socket_mode_must_be_octal_permissions(self):
        for ok in ("0660", "660", "0777", "0", "00600"):
            ParseDaemonConfig(self._cfg(socket_mode=ok))
        for bad in ("", "0888", "rw-rw----", "1000", "0x1ff", "-1", " 7", "0660 "):
            self._fails(self._cfg(socket_mode=bad), "socket_mode")
        self._fails(self._cfg(socket_mode=660), "socket_mode")

    def test_system_and_component_range(self):
        self._fails(self._cfg(system_id=0), "system_id")
        self._fails(self._cfg(component_id=256), "component_id")

    def test_driver_required_and_named(self):
        self._fails(self._cfg(links={"lora": {}}), "lora", "driver")
        self._fails(self._cfg(links={"lora": {"driver": "zly-id"}}), "driver")

    def test_driver_config_must_be_path_string(self):
        self._fails(self._cfg(links={"lora": {"driver": "d", "driver_config": 3}}),
                    "driver_config")

    def test_link_name_pattern(self):
        self._fails(self._cfg(links={"1lora": {"driver": "d"}}), "1lora")

    def test_unknown_link_key_and_old_fields(self):
        self._fails(self._cfg(links={"lora": {"driver": "d", "foo": 1}}), "foo")
        for key in ("transport", "device", "baudrate", "bind", "peer", "max_bytes_per_s"):
            self._fails(self._cfg(links={"lora": {"driver": "d", key: 1}}), key, "driver_config")

    def test_version_and_heartbeat(self):
        self._fails(self._cfg(links={"lora": {"driver": "d", "version": 3}}), "version")
        self._fails(self._cfg(links={"lora": {"driver": "d", "heartbeat_hz": -1}}), "heartbeat_hz")
        self._fails(self._cfg(links={"lora": {"driver": "d", "heartbeat_hz": float("nan")}}),
                    "heartbeat_hz")

    def test_signing_rules(self):
        self._fails(self._cfg(links={"lora": {"driver": "d", "version": 1,
                                              "signing": {"enabled": True, "key_file": "/k"}}}),
                    "v2")
        self._fails(self._cfg(links={"lora": {"driver": "d", "signing": {"enabled": True}}}),
                    "key_file")
        self._fails(self._cfg(links={"lora": {"driver": "d", "signing": {"nope": 1}}}), "nope")
        self._fails(self._cfg(links={
            "a": {"driver": "d", "signing": {"enabled": True, "key_file": "/k", "link_id": 1}},
            "b": {"driver": "d", "signing": {"enabled": True, "key_file": "/k", "link_id": 1}}}),
            "link_id")

    def test_load_missing_file(self):
        with self.assertRaises(MavlinkModelError):
            LoadDaemonConfig("/nie/ma/takiego.json")

    def test_fixture_config_parses(self):
        cfg = LoadDaemonConfig(os.path.join(APP_DIR, "daemon_config.json"))
        self.assertEqual(list(cfg.links), ["radio1", "radio2", "radio3", "lora", "spare"])


class CliTest(unittest.TestCase):
    def _run(self, *args):
        err = io.StringIO()
        with contextlib.redirect_stderr(err), contextlib.redirect_stdout(io.StringIO()):
            rc = mavlink_gen.main(list(args))
        return rc, err.getvalue()

    def _models(self, d):
        paths = {n: os.path.join(d, n + ".json") for n in ("types", "service", "app", "daemon")}
        svc, app = _load("service.json"), _load("app.json")
        svc["include"] = [paths["types"]]
        app["include"] = [paths["service"]]
        for n, obj in (("types", _load("types.json")), ("service", svc), ("app", app),
                       ("daemon", _load("daemon_config.json"))):
            with open(paths[n], "w", encoding="utf-8") as f:
                json.dump(obj, f)
        return paths

    def test_cli_writes_xml_and_links_md(self):
        SomeIpDb().service_list.clear()
        DataStructureDB().data_structure.clear()
        AdaptiveApplicationDb().app_list.clear()
        with tempfile.TemporaryDirectory() as d:
            p = self._models(d)
            rc, _ = self._run(os.path.join(d, "o.xml"), "--links-md", os.path.join(d, "o.md"),
                              "--dialect", "cpu_a", "--daemon-config", p["daemon"], p["app"])
            self.assertEqual(rc, 0)
            with open(os.path.join(d, "o.md"), encoding="utf-8") as f:
                self.assertIn("system_id=1, component_id=1", f.read())

    def test_cli_requires_dialect(self):
        SomeIpDb().service_list.clear()
        DataStructureDB().data_structure.clear()
        AdaptiveApplicationDb().app_list.clear()
        with tempfile.TemporaryDirectory() as d:
            p = self._models(d)
            rc, err = self._run(os.path.join(d, "o.xml"), p["app"])
            self.assertEqual(rc, 1)
            self.assertIn("--dialect", err)

    def test_cli_error_has_exit_code_1(self):
        SomeIpDb().service_list.clear()
        DataStructureDB().data_structure.clear()
        AdaptiveApplicationDb().app_list.clear()
        with tempfile.TemporaryDirectory() as d:
            p = self._models(d)
            rc, err = self._run(os.path.join(d, "o.xml"), "--dialect", "cpu_a",
                                "--daemon-config", os.path.join(d, "brak.json"), p["app"])
            self.assertEqual(rc, 1)
            self.assertIn("BŁĄD mavlink", err)


if __name__ == "__main__":
    unittest.main()
