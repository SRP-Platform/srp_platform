"""`soak_run --local` end to end: the daemon and the example applications on this host, the station,
the CPU hogs and the verdict, with phases shortened to 10 + 40 + 10 s."""
# pylint: disable=missing-function-docstring
import json
import os
import subprocess
import sys

import pytest

SOAK_RUN = os.path.abspath(os.environ["SOAK_RUN"])


def test_the_local_soak_ends_with_a_pass(tmp_path):
    done = subprocess.run([SOAK_RUN, "--local", "--out", str(tmp_path)], capture_output=True,
                          text=True, timeout=300, check=False)
    assert done.returncode == 0, done.stdout[-4000:] + done.stderr[-2000:]
    assert done.stdout.rstrip().endswith("PASS"), done.stdout[-2000:]
    verdict = json.loads((tmp_path / "verdict.json").read_text(encoding="utf-8"))
    assert verdict["verdict"] == "PASS" and verdict["cpu"]["cpu_total_load_mean"] > 0
    assert verdict["station"]["client"]["timeouts"] == 0
    lines = (tmp_path / "station.jsonl").read_text(encoding="utf-8").splitlines()
    assert len(lines) >= 55 and {"baseline", "load", "cooldown"} <= {
        json.loads(ln)["phase"] for ln in lines}
    csv = (tmp_path / "soak_cpu.csv").read_text(encoding="utf-8").splitlines()
    assert csv[0].startswith("t_s,epoch,hogs,pid,restarts") and csv[-1] == "SOAK_CPU_DONE"


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v", "-s", "-p", "no:cacheprovider"]))
