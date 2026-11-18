"""Wrapper mavgen: <dialect>.xml -> C (v1/v2), WLua, Python.

Użycie: mavgen_run.py <dialect.xml> <out_c_dir> <out.lua> <out.py> <wersje, np. 1,2>
C trafia do <out_c_dir>/v1 i/lub <out_c_dir>/v2; WLua i Python dla najwyższej wersji.
minimal.xml (include) kopiowany z pakietu pymavlink obok XML-a.
"""
from __future__ import annotations

import os
import shutil
import sys
import tempfile

from pymavlink.generator import mavgen


def _minimal_xml() -> str:
    import pymavlink
    base = os.path.dirname(pymavlink.__file__)
    for cand in (os.path.join(base, "message_definitions", "v1.0", "minimal.xml"),
                 os.path.join(os.path.dirname(base), "message_definitions", "v1.0",
                              "minimal.xml")):
        if os.path.isfile(cand):
            return cand
    raise FileNotFoundError("nie znaleziono minimal.xml w pymavlink")


def _gen(xml: str, out: str, lang: str, wire: int) -> None:
    opts = mavgen.Opts(out, wire_protocol=("1.0" if wire == 1 else "2.0"), language=lang,
                       validate=True, error_limit=200, strict_units=False)
    if not mavgen.mavgen(opts, [xml]):
        raise SystemExit(f"mavgen {lang} nie powiódł się dla {xml}")


def main(argv: list[str]) -> int:
    xml, out_c, out_lua, out_py, vers = argv
    versions = sorted({int(v) for v in vers.split(",")})
    wire = versions[-1]
    work = tempfile.mkdtemp()
    try:
        shutil.copy(xml, os.path.join(work, os.path.basename(xml)))
        shutil.copy(_minimal_xml(), os.path.join(work, "minimal.xml"))
        wxml = os.path.join(work, os.path.basename(xml))
        for v in versions:
            _gen(wxml, os.path.join(out_c, f"v{v}"), "C", v)
        lua_out = os.path.join(work, "dialect.lua")
        _gen(wxml, lua_out, "WLua", wire)
        shutil.copy(lua_out, out_lua)
        py_out = os.path.join(work, "py", os.path.splitext(os.path.basename(xml))[0] + ".py")
        os.makedirs(os.path.dirname(py_out))
        _gen(wxml, py_out, "Python3", wire)
        shutil.copy(py_out, out_py)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
