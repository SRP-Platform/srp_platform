"""Test fixture: builds the models of one or more applications and runs the generator."""
from __future__ import annotations

import contextlib
import io
import json
import os
import tempfile

from tools.model_generator.ara.app.adaptive_application_db import AdaptiveApplicationDb
from tools.model_generator.ara.common.data_structure_db import DataStructureDB
from tools.model_generator.ara.mavlink.mavlink_gen import GenerateAll
from tools.model_generator.ara.someip.lib.someip_db import SomeIpDb

HERE = os.path.dirname(os.path.abspath(__file__))
APP_DIR = os.path.join(HERE, "testdata", "app")


def Load(name: str) -> dict:
    with open(os.path.join(APP_DIR, name), encoding="utf-8") as f:
        return json.load(f)


def Golden(name: str) -> str:
    with open(os.path.join(HERE, "testdata", name), encoding="utf-8") as f:
        return f.read()


def Reset() -> None:
    SomeIpDb().service_list.clear()
    DataStructureDB().data_structure.clear()
    AdaptiveApplicationDb().app_list.clear()


def Beacon(msg_id: int) -> dict:
    """A second service with one event, for tests that need two services on one CPU."""
    return {"package": "srp.mav", "someip": {"Beacon": {
        "service_id": 21, "major_version": 1, "minor_version": 0,
        "events": {"Ping": {"id": 32769, "data_structure": {"out": {"type": "uint32"}},
                            "mavlink": {"msg_id": msg_id}}}}}}


def App2(instance=8, links=("lora",), service="Radio", name="MavApp2", direction="require") -> dict:
    """A second application of the CPU with one on=mavlink entry."""
    app = Load("app.json")
    a = app["adaptive_application"].pop("MavApp")
    a.pop("provide")
    a.pop("require")
    a[direction] = [{"name": service, "on": "mavlink", "instance": instance,
                     "links": list(links)}]
    app["adaptive_application"] = {name: a}
    return app


def Write(models: dict, d: str) -> dict[str, str]:
    paths = {n: os.path.join(d, n + ".json") for n in models}
    for n, obj in models.items():
        with open(paths[n], "w", encoding="utf-8") as f:
            json.dump(obj, f)
    return paths


def Models(app_mut=None, svc_mut=None, daemon_mut=None, app2=None, svc2=None,
           types_extra=None) -> dict:
    """The fixture models after the mutations; includes are filled in by `Run`."""
    app, svc = Load("app.json"), Load("service.json")
    daemon, types = Load("daemon_config.json"), Load("types.json")
    if app_mut:
        app_mut(app["adaptive_application"]["MavApp"])
    if svc_mut:
        svc_mut(svc["someip"]["Radio"])
    if daemon_mut:
        daemon_mut(daemon)
    models = {"types": types, "service": svc, "app": app, "daemon": daemon}
    if types_extra:
        models["types_extra"] = types_extra
    if app2 is not None:
        models["app2"] = app2
    if svc2 is not None:
        models["svc2"] = svc2
    return models


def Run(app_mut=None, svc_mut=None, daemon_mut=None, app2=None, svc2=None, types_extra=None,
        with_daemon=True, dialect="srp_radio", quiet=True):
    """Generates the CPU artifacts from the fixture; returns (Result, [AdaptiveApplication])."""
    Reset()
    models = Models(app_mut, svc_mut, daemon_mut, app2, svc2, types_extra)
    with tempfile.TemporaryDirectory() as d:
        paths = {n: os.path.join(d, n + ".json") for n in models}
        models["service"]["include"] = [paths["types"]] + (
            [paths["types_extra"]] if "types_extra" in models else [])
        models["app"]["include"] = [paths["service"]]
        if "svc2" in models:
            models["svc2"]["include"] = [paths["types"]]
        if "app2" in models:
            models["app2"]["include"] = [paths["service"]] + (
                [paths["svc2"]] if "svc2" in models else [])
        Write(models, d)
        apps = [paths["app"]] + ([paths["app2"]] if "app2" in models else [])
        err = contextlib.redirect_stderr(io.StringIO()) if quiet else contextlib.nullcontext()
        with contextlib.redirect_stdout(io.StringIO()), err:
            res = GenerateAll(apps, dialect, paths["daemon"] if with_daemon else None)
    return res, list(AdaptiveApplicationDb().app_list.values())


def ComInit(app) -> str:
    """The InitializeComDb text of initialization.cc for an application."""
    from tools.model_generator.ara.mavlink.mavlink_gen import CreateItemCpp
    from tools.model_generator.ara.someip.lib.someip_runtime_env import SomeipRuntimeEnv
    return SomeipRuntimeEnv.CreateInitDB(
        "srp/mav/MavApp", app.GetServiceList(), app,
        lambda name, key, item: CreateItemCpp(name, key, item.dir, item.item, item.instance,
                                              item.mavlink))
