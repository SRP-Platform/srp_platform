"""The package for hardware tests (`//tests/mavlink/udp_cpu`): its config on the host.

The daemon config and the driver files that go to the RPi/BBB are checked as they are (links named
like in the models, the statically linked driver, the documented station address) and the same
config, with the loopback in place of the station, gives the expected result of the generated
monitor on UDP: every event of the link within 15 % of the model rate, every REQ answered.
"""
# pylint: disable=missing-function-docstring,redefined-outer-name,import-error,wrong-import-position
import json
import os
import sys

import pytest

import e2e_examples_test as ee
import e2e_monitor_test as em
import udp_cpu_station as station
import udp_harness as uh

CONFIG = os.environ["EXAMPLE_DAEMON_CONFIG"]
LOGGER_CONFIG = os.environ["LOGGER_CONFIG"]
PLATFORM_CONFIG = os.environ["PLATFORM_CONFIG"]
STATION = "192.168.7.1"


def load(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def test_the_config_names_the_links_of_the_models_with_the_udp_driver():
    cfg = load(CONFIG)
    assert (cfg["system_id"], cfg["component_id"]) == (1, 1)
    assert set(cfg["links"]) == set(uh.LINKS)
    for name, link in cfg["links"].items():
        assert link["driver"] == "example_udp_radio", name
        assert link["driver_config"].startswith("/srp/opt/mavlink_daemon/etc/"), name
    assert cfg["links"]["radio_b"]["air_rate_bps"] == 2400 and cfg["links"]["radio_b"]["mtu"] == 100


@pytest.mark.parametrize("env,link,ports", [("UDP_GROUND", "radio_a", (14560, 14550)),
                                            ("UDP_GROUND_B", "radio_b", (14561, 14551))])
def test_the_driver_files_point_at_the_station(env, link, ports):
    drv = load(os.environ[env])
    assert drv == {"bind_port": ports[0], "peer_host": STATION, "peer_port": ports[1]}
    path = load(CONFIG)["links"][link]["driver_config"]
    assert os.path.basename(path) == os.path.basename(os.environ[env])


def test_the_logger_sends_dlt_from_the_address_of_the_cpu():
    # the logger joins the multicast on `ip` (IP_MULTICAST_IF): any other address than the CPU's one
    # makes the socket fail and no DLT reaches the PC
    assert load(LOGGER_CONFIG)["ip"] == load(PLATFORM_CONFIG)["platform"]["ip"]


@pytest.fixture
def scn():
    s = em.hn.Scenario()
    yield s
    s.close()


def test_the_config_of_the_package_gives_the_expected_traffic_on_udp(tmp_path, scn):
    stack = uh.UdpStack(tmp_path, scn, em.UDP_DAEMON_HOST, CONFIG, ee.APP1, ee.APP2)
    stack.wait_registered()
    (port,) = uh.free_udp_ports(1)
    stack.gs["radio_a"].tap = scn.add(em.hn.UdpTap(port))
    monitor = scn.add(em.Monitor(["--udp", f"127.0.0.1:{port}", "--link", "radio_a",
                                  "--daemon-config", str(tmp_path / "mavlink_daemon_config.json")]))
    snap = em.run_taps([stack.gs["radio_a"].tap], [monitor])[0]
    em.check(snap, "radio_a", "udp_cpu")


def test_the_station_script_gets_every_response_from_the_package_config(tmp_path, scn, capsys):
    stack = uh.UdpStack(tmp_path, scn, em.UDP_DAEMON_HOST, CONFIG, ee.APP1, ee.APP2)
    stack.wait_registered()
    gs_port, driver_port = stack.ports["radio_a"]
    stack.gs["radio_a"].stop()  # the script takes the place of the station on this link
    rc = station.main(["--dialect-py", em.dialect_files(".py")[0], "--target", "127.0.0.1",
                       "--peer-port", str(driver_port), "--listen-port", str(gs_port),
                       "--listen-host", "127.0.0.1", "--set-mode", "5", "--debug-dump",
                       "--duration", "6"])
    out = capsys.readouterr().out
    assert rc == 0, out
    assert "answered 5" in out and "SRP_ENGINE_SERVICE_TELEMETRY#2" in out, out


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v", "-s", "-p", "no:cacheprovider"]))
