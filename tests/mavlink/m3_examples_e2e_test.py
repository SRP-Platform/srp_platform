"""M3 AC1: E2E przykladow example_app (skeleton) <-> example_app2 (proxy) przez channel_sim na hoscie.

Dwa lacza radiowe: "lora" i "wifi", oba rejestrowane w kodzie jako SerialTransport (lora: port z env, wifi: symlink do pty).
Sprawdzamy: event przez oba lacza (redundancja) bez duplikatu, metoda przez lora OK, przez wifi wykluczona (status=2 ->
blad ComErrc::kServiceNotAvailable), limit Telemetry 1 Hz (lora) vs 20 Hz (wifi).
"""
import fcntl
import os
import re
import signal
import subprocess
import sys
import time
from collections import Counter, defaultdict

import pytest

from channel_sim import ChannelSim

APP1 = os.environ.get("EXAMPLE_APP1", "example_app/ExampleApp/code/application/ExampleApp")
APP2 = os.environ.get("EXAMPLE_APP2", "example_app2/ExampleApp2/code/application/ExampleApp")
RUN_SECONDS = float(os.environ.get("EXAMPLE_RUN_SECONDS", "10"))
K_SERVICE_NOT_AVAILABLE = 1  # ara::com::ComErrc::kServiceNotAvailable (zdalnie: metoda nieznana/wykluczona, status=2)


def start(binary, env, out_path):
    out = open(out_path, "wb")
    e = dict(os.environ, SRP_EXAMPLE_NO_EM="1", **env)
    return subprocess.Popen([binary], stdout=out, stderr=subprocess.STDOUT, env=e, start_new_session=True), out


def stop(proc):
    """SIGKILL: okno pomiaru konczy sie dokladnie z czasem testu (SIGTERM = powolne zamykanie, aplikacja dalej drukuje)."""
    if proc.poll() is None:
        os.killpg(proc.pid, signal.SIGKILL)
        proc.wait()


@pytest.fixture(scope="module")
def run(tmp_path_factory):
    d = tmp_path_factory.mktemp("ex")
    wdir = "/tmp/srp_example"  # sciezki urzadzen wifi sa wpisane w modelach przykladow (testowe)
    os.makedirs(wdir, exist_ok=True)
    lock = open(os.path.join(wdir, ".lock"), "w")
    fcntl.flock(lock, fcntl.LOCK_EX)  # rownolegle przebiegi dziela te same symlinki - serializujemy
    lora = ChannelSim(baud=115200, seed=1).start()
    wifi = ChannelSim(baud=115200, seed=2).start()
    for name, target in (("wifi_a", wifi.path_a), ("wifi_b", wifi.path_b)):
        link = os.path.join(wdir, name)
        if os.path.lexists(link):
            os.unlink(link)
        os.symlink(target, link)
    skel, f1 = start(APP1, {"SRP_EXAMPLE_LORA_DEVICE": lora.path_a}, str(d / "app1.out"))
    prox, f2 = start(APP2, {"SRP_EXAMPLE_LORA_DEVICE": lora.path_b}, str(d / "app2.out"))
    time.sleep(RUN_SECONDS)
    stop(prox)
    stop(skel)
    f1.close()
    f2.close()
    lora.stop()
    wifi.stop()
    fcntl.flock(lock, fcntl.LOCK_UN)
    lock.close()
    text2 = open(d / "app2.out", "rb").read().decode("utf-8", "replace")
    text1 = open(d / "app1.out", "rb").read().decode("utf-8", "replace")
    return text1, text2


def lines(text, prefix):
    return [l for l in text.splitlines() if l.startswith(prefix)]


def events(text, name):
    per = defaultdict(list)
    for l in lines(text, f"EVT {name} "):
        m = re.match(r"EVT \w+ instance=(\d+) (?:mode|rpm)=(\d+)", l)
        if m:
            per[int(m.group(1))].append(int(m.group(2)))
    return per


def test_redundant_instance_delivers_each_event_once(run):
    _, t2 = run
    modes = events(t2, "CurrentMode")[2]  # Engine_red: ta sama ramka idzie lacznie lora i wifi
    assert len(modes) >= 8, modes
    dup = [m for m, c in Counter(modes).items() if c > 1]
    assert not dup, f"duplikaty w aplikacji: {dup}"  # dedup: kopia z drugiego lacza odrzucona
    assert modes == sorted(modes)


def test_method_over_lora_ok(run):
    t1, t2 = run
    ok = [l for l in lines(t2, "CALL SetMode instance=3 ") if " ok " in l]
    assert len(ok) >= 3, lines(t2, "CALL SetMode instance=3")
    assert not [l for l in lines(t2, "CALL SetMode instance=3 ") if "err=" in l]
    assert any(l.startswith("REQ SetMode instance=3") for l in t1.splitlines())  # skeleton wykonal


def test_method_over_wifi_is_excluded_with_status_2(run):
    t1, t2 = run
    calls = lines(t2, "CALL SetMode instance=4 ")
    assert calls and all(f"err={K_SERVICE_NOT_AVAILABLE}" in l for l in calls), calls  # RESP status=2
    assert not any(l.startswith("REQ SetMode instance=4") for l in t1.splitlines())  # callback skeletonu nie wolany


def test_debug_dump_excluded_on_lora_but_not_on_wifi(run):
    t1, t2 = run
    assert all("err=" in l for l in lines(t2, "CALL DebugDump instance=3 ")) and lines(t2, "CALL DebugDump instance=3 ")
    assert any(" ok" in l for l in lines(t2, "CALL DebugDump instance=4 "))  # wifi: DebugDump dozwolony


def test_telemetry_rate_1hz_lora_vs_20hz_wifi(run):
    _, t2 = run
    per = events(t2, "Telemetry")
    n_lora, n_wifi = len(per[3]), len(per[4])
    secs = RUN_SECONDS - 3  # rozruch aplikacji (zliczamy zdarzenia z calego przebiegu)
    assert n_lora <= 1.5 * RUN_SECONDS + 2, (n_lora, RUN_SECONDS)  # ~1 Hz (limit instancji 1 < limit uslugi 2)
    assert n_wifi >= 8 * secs, (n_wifi, secs)  # ~20 Hz
    assert n_wifi >= 8 * n_lora
    assert len(per[2]) <= 2.5 * RUN_SECONDS  # Engine_red: limit z service.json (2 Hz)


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v", "-p", "no:cacheprovider"]))
