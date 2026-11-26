"""End-to-end test of a daemon binary built like in srp: `mavlink_daemon_binary(drivers = [...])`.

The binary links a test radio driver statically (`//ara/com/mavlink:test_radio_driver`, ids
`test_serial_a` / `test_serial_b`); the links of the config use `test_serial_b`. The scenario is the
one of `e2e_examples_test.py` (the Stack class is reused, the environment points it at this binary
and this config): the example applications register and their events reach the ground stations
on the other end of the ptys, read by pymavlink.

A second binary links two libraries that register the same driver id; it must refuse to start
and name the id.
"""
import json
import os
import subprocess
import sys

import pytest

import e2e_examples_test as base
import harness as hn

DUPLICATE_HOST = os.environ["DUPLICATE_DRIVER_DAEMON_HOST"]


@pytest.fixture
def scn():
    s = hn.Scenario()
    yield s
    s.close()


def test_events_reach_the_pty_through_a_statically_linked_driver(tmp_path, scn):
    st = base.new_stack(tmp_path, scn)  # waits until the GS sees the events of both applications
    instances = {m["d"]["instance"] for m in st.gs["radio_a"].of("SRP_ENGINE_SERVICE_CURRENT_MODE")}
    assert {2, 3} <= instances
    assert st.stats()["register_rejected"] == 0
    # the links run on the driver that the config named (its Name() is in the "link ready" log)
    log = st.daemon.stderr_tail(100000)
    assert "link ready (test_serial_b)" in log, log


def test_a_driver_id_registered_twice_is_a_start_error_naming_the_id(tmp_path):
    cfg = json.load(open(base.DAEMON_CFG, encoding="utf-8"))
    cfg["socket_path"] = str(tmp_path / "daemon.sock")
    path = tmp_path / "mavlink_daemon_config.json"
    path.write_text(json.dumps(cfg), encoding="utf-8")
    run = subprocess.run([DUPLICATE_HOST, str(path)], input="", capture_output=True, text=True, timeout=30)
    assert run.returncode == 2, (run.stdout, run.stderr)
    event = json.loads(run.stdout.strip().splitlines()[-1])
    assert event["ev"] == "error"
    assert "duplicate driver id 'test_serial_a'" in event["error"], event


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v", "-p", "no:cacheprovider"]))
