"""AC4 M3: fragmenty JSON w docs/mavlink/README.md generuja poprawny model, a macierz "Co idzie ktorym radiem" w README
zgadza sie z wyjsciem generatora (`<dialekt>_links.md`) dla tych samych modeli."""
import os
import re
import subprocess
import sys

import pytest

README = os.environ.get("MAVLINK_README", "docs/mavlink/README.md")
GEN = os.environ.get("MAVLINK_GEN", "tools/model_generator/ara/mavlink/mavlink_gen")
BLOCK = re.compile(r"<!-- file: (?P<path>[^ ]+) -->\n```json\n(?P<body>.*?)\n```", re.S)
LINKS = re.compile(r"<!-- generated: links_md -->\n```text\n(?P<body>.*?)\n```", re.S)


@pytest.fixture(scope="module")
def generated(tmp_path_factory):
    text = open(README, encoding="utf-8").read()
    d = tmp_path_factory.mktemp("readme")
    blocks = BLOCK.findall(text)
    assert len(blocks) >= 4, "README musi miec fragmenty JSON z markerem <!-- file: ... -->"
    for path, body in blocks:
        p = d / path
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(body + "\n", encoding="utf-8")
    # CPU z dwiema aplikacjami i jednym daemonem: modele WSZYSTKICH aplikacji + config daemona
    p = subprocess.run([os.path.abspath(GEN), str(d / "out.xml"), "--links-md", str(d / "links.md"), "--dialect", "srp_example",
                        "--daemon-config", "daemon/mavlink_daemon_config.json", "models/app_config.json",
                        "models/app2_config.json"], cwd=d, capture_output=True, text=True)
    return text, d, p


def test_json_snippets_generate_a_model(generated):
    _, d, p = generated
    assert p.returncode == 0, p.stderr
    xml = (d / "out.xml").read_text(encoding="utf-8")
    assert "<include>minimal.xml</include>" in xml and "SRP_ENGINE_SERVICE_TELEMETRY" in xml


def test_matrix_in_readme_matches_generator_output(generated):
    text, d, p = generated
    m = LINKS.search(text)
    assert m, "brak bloku <!-- generated: links_md --> w README"
    assert m.group("body").strip() == (d / "links.md").read_text(encoding="utf-8").strip()


def test_readme_covers_required_topics():
    text = open(README, encoding="utf-8").read()
    for topic in ("Co idzie którym radiem", "MavlinkDaemon", "serial_radio", "driver_config", "SRP_MAVLINK_DAEMON_SOCKET", "state_file",
                  "key_file", "HEARTBEAT", "255", "serial2udp", "keygen", "rx_sig_err", "ara_mavlink_dialect", "<dialekt>.lua", "exclude",
                  "max_rate_hz", "mavlink_links"):
        assert topic in text, topic
    assert not re.search(r"Register\w*Transport", text) and "wifi" not in text.lower(), "dokumentacja opisuje stary model albo zna wifi"


def test_readme_pymavlink_snippet_works_with_generated_module(generated):
    """Droga z README: srp_example.MAVLink(port) + parse_buffer na wygenerowanym <dialekt>.py (bez instalacji w pymavlink)."""
    import importlib.util
    import shutil

    import pymavlink
    from pymavlink.generator import mavgen

    text, d, p = generated
    assert "srp_example.MAVLink(port)" in text and 'dialect="srp_example"' not in text.split("Uwaga:")[0]
    shutil.copy(os.path.join(os.path.dirname(pymavlink.__file__), "message_definitions", "v1.0", "minimal.xml"), d)
    out = str(d / "srp_example.py")
    assert mavgen.mavgen(mavgen.Opts(out, wire_protocol="2.0", language="Python3", validate=True), [str(d / "out.xml")])
    spec = importlib.util.spec_from_file_location("srp_example", out)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)

    class Port:  # obiekt jak serial.Serial: MAVLink(port) pisze przez port.write
        def __init__(self):
            self.buf = bytearray()

        def write(self, b):
            self.buf += b

    port = Port()
    mav = mod.MAVLink(port, srcSystem=1, srcComponent=1)
    mav.robust_parsing = True
    mav.send(mod.MAVLink_srp_engine_service_telemetry_message(seq=1, instance=3, rpm=1500, temp=21.5))
    got = mav.parse_buffer(bytes(port.buf))
    assert [m.get_type() for m in got] == ["SRP_ENGINE_SERVICE_TELEMETRY"] and got[0].rpm == 1500 and got[0].instance == 3


def test_readme_pymavlink_message_name_exists_in_generated_dialect(generated):
    text, d, p = generated
    assert "SRP_ENGINE_SERVICE_TELEMETRY" in text and "SRP_ENGINE_SERVICE_TELEMETRY" in (d / "out.xml").read_text(encoding="utf-8")


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v", "-p", "no:cacheprovider"]))
