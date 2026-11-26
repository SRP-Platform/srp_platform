"""End-to-end test of the example radio driver `example_udp_radio`.

Source: ara/com/mavlink/examples/udp_radio.

The daemon binary `example_udp_daemon_host` is built like in srp (`mavlink_daemon_binary(drivers =
[...])`); both links of the CPU config run on the UDP driver (one MAVLink frame = one datagram). The
example applications register, and a pymavlink ground station on UDP sees their events and calls
their methods.
"""
# pylint: disable=missing-function-docstring,redefined-outer-name,import-error,too-many-locals
# pylint: disable=too-many-arguments,too-many-branches,too-many-statements,consider-using-with
import os
import sys

import pytest

import harness as hn
import udp_harness as uh

DAEMON_HOST = os.environ["UDP_DAEMON_HOST"]
DAEMON_CFG = os.environ["EXAMPLE_DAEMON_CONFIG"]
APP1 = os.environ["EXAMPLE_APP1"]
APP2 = os.environ["EXAMPLE_APP2"]


@pytest.fixture
def scn():
    s = hn.Scenario()
    yield s
    s.close()


@pytest.fixture
def stack(tmp_path, scn):
    st = uh.UdpStack(tmp_path, scn, DAEMON_HOST, DAEMON_CFG, APP1, APP2)
    st.wait_registered()
    return st


def test_application_events_reach_the_udp_ground_station(stack):
    assert {2, 3} <= stack.instances("radio_a")
    assert 2 in stack.instances("radio_b")
    assert 3 not in stack.instances("radio_b")  # Engine_a is on radio_a only
    for g in stack.gs.values():
        assert not g.bad and not g.rejected
        # a frame fits into a datagram (MaxFrameSize)
        assert all(len(d) <= 280 for _, d in g.datagrams)


def test_the_daemon_runs_the_links_on_the_named_driver(stack):
    log = stack.daemon.stderr_tail(100000)
    assert "link ready (example_udp_radio(:" in log, log


def test_a_method_called_by_the_ground_station_is_served(stack):
    g = stack.gs["radio_a"]
    g.send(stack.mod.MAVLink_srp_engine_service_set_mode_req_message(
        seq=77, instance=2, target_system=1, target_component=1, mode=5))
    assert hn.wait_until(lambda: [m for m in g.of("SRP_ENGINE_SERVICE_SET_MODE_RESP")
                                  if m["d"]["seq"] == 77], 5.0), stack.out("app1")
    resp = [m for m in g.of("SRP_ENGINE_SERVICE_SET_MODE_RESP") if m["d"]["seq"] == 77][0]["d"]
    assert (resp["status"], resp["mode"], resp["instance"]) == (0, 5, 2)
    assert "REQ SetMode instance=2 mode=5" in stack.out("app1")


def test_the_proxy_application_calls_through_the_udp_link(stack):
    assert hn.wait_until(lambda: len(stack.lines("app2", "CALL SetMode ok")) >= 2, 10.0), \
        stack.out("app2")
    assert stack.gs["radio_a"].of("SRP_ENGINE_SERVICE_SET_MODE_REQ")


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v", "-p", "no:cacheprovider"]))
