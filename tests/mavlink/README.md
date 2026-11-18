# tests/mavlink — testy integracyjne transportu MAVLink

Testy jednostkowe leżą przy kodzie (`ara/com/mavlink/ut`, `ara/com/mavlink/client/ut`, `platform/common/mavlink_daemon/code/ut`,
`tools/model_generator/ara/mavlink/test_*.py`, `tools/mavlink/*_test.py`). Tu jest zwarty zestaw integracyjny: prawdziwy **MavlinkDaemon**
(rdzeń `daemon_host`) i aplikacje (peer C++, przykłady `example_app*`) rozmawiają z `pymavlink` przez symulowany kanał radiowy (pty).

## Uruchomienie

```sh
bazel test //tests/mavlink/...                     # bez targetów `manual`
bazel test //tests/mavlink:e2e_soak_30min          # soak 30 min (manual)
bazel test //tests/mavlink:e2e_fuzz_10min          # fuzz losowy 10 min (manual)
bazel run  //tests/mavlink:fuzz_corpus -- --out /tmp/corpus --seed 1
```
Wymagane: `tshark` i `lua5.4` (bez `tshark` testy Wiresharka są pomijane). `pymavlink` tylko przez bazel.
Ciężkie buildy pod `flock -o` i przez `agent-res`, osobny `--output_base` dla ARM.

## Topologia

Jeden „CPU” testu = dwa procesy: **`daemon_host`** (ten sam rdzeń co produkcyjny `mavlink_daemon`, bez Execution Management, sterowany JSON lines; jedyny host daemona w testach — używa go też `e2e_daemon_test`)
i aplikacja (**peer** albo przykłady). Daemon dostaje łącza `serial_radio` na jednym końcu pty (`ChannelSim`), GS na pymavlink siedzi na drugim.
Aplikacja łączy się z daemonem przez gniazdo unix (`SRP_MAVLINK_DAEMON_SOCKET`).

## Pliki

| Plik | Co sprawdza |
|------|-------------|
| `channel_sim.py` + `_test` | symulator radia (pty: utrata, bitflip, opóźnienie, limit baud, seed) |
| `mav_helpers.py` + `_test` | mavgen z XML w teście (v1/v2), kodowanie, `crc_extra`, podpisy (HMAC liczony niezależnie, zły klucz, replay, osobne strumienie `link_id`) |
| `tshark_harness.py`, `tshark_lua_test.py` | pcap → `tshark -X lua_script:` z wygenerowanym `mavgen --lang=WLua`: wartości pól v1, v2, v2 podpisane |
| `fuzz_corpus.py` + `_test` | korpus do fuzzingu parsera (poprawne, obcięte, zły CRC, losowe, sklejone) |
| `peer/mavlink_peer.cc`, `peer_model/` | aplikacja-peer C++ na prawdziwym runtime (klient daemona); model przez generator |
| `peer/daemon_host.cc` | host `MavlinkDaemonCore` do testów (liczniki łączy i daemona, zatrzymanie z zapisem stanu podpisu) |
| `collide_model/` | ten sam peer z usługą o `msg_id` zajętym przez inną definicję (kolizja między aplikacjami) |
| `peer_api.py` | sterowanie daemonem i peerem (`ProcPeer`), GS na pymavlink, topologia z `ChannelSim` |
| `e2e_interop_test.py` | interop peer ↔ pymavlink v1/v2/v2 signed, `status=2`, target, podpis (zły klucz, replay, starszy timestamp, restart), tshark na ramkach z peera, `max_rate_hz`, `exclude` |
| `e2e_app_lifecycle_test.py` | restart daemona (ponowna rejestracja ≤ 5 s, ruch wraca, bez daemona metoda kończy się błędem od razu), restart aplikacji (daemon sprząta rejestracje), kolizja `msg_id` między aplikacjami (odrzucenie rejestracji) |
| `e2e_channel_test.py` | kanał stratny/bitflip/opóźnienie, redundancja 2 łączy, dedup, restart nadawcy |
| `e2e_signing_persist_test.py` | `state_file` podpisu: restart nadawcy i odbiorcy (daemona) |
| `e2e_fuzz_test.py` | korpus + losowe/mutowane ramki (10 s; pełne 10 min: `e2e_fuzz_10min`) |
| `e2e_soak_test.py` | soak (20 s; 30 min: `e2e_soak_30min`): RSS, liczniki |
| `e2e_examples_test.py` | `example_app` (provide) + `example_app2` (require) na jednym daemonie CPU `test_cpu/cpu/ec`, dwa łącza, GS na pymavlink: eventy obu aplikacji, metoda z timeoutem, `exclude`, `max_rate_hz` 1 vs 20 Hz, redundancja 2 łączy → 1 dostarczenie, tshark + Lua CPU |

## Protokół peera i daemon_host (JSON lines)

**Peer** czyta ze stdin linie `{"id":N,"cmd":...}` i odpowiada `{"id":N,"ok":true,"result":...}` (błąd: `"ok":false,"error"`).
Polecenia: `start` (`config`: `socket` (gniazdo daemona), `instances[]` {instance, links (nazwy łączy daemona), exclude, max_rate_hz, dedup_ttl_ms,
target_system, target_component}, opcjonalnie `seq_start`, `events_limit`, `method_timeout_ms`, `reconnect_ms`), `ready` (`timeout_ms`: czeka, aż wszystkie instancje są zarejestrowane i mają gotowe łącze),
`emit` (`instance`, `element`, `values`), `call` (`instance`, `method`, `call_timeout`, `args` → `["ok", status, {…}]`, `["timeout"]` albo `["error", kod]`),
`stats` (liczniki instancji: `req_executed`, `resp_ok`, `calls`, `call_timeout`, `call_rejected`, `tx_excluded`, `registered`…), `rss`, `stop`.
Zdarzenia asynchroniczne: `{"ev":"event",…}` (event po filtrach daemona) i `{"ev":"handled",…}` (REQ wykonany).

**daemon_host** (`daemon_host <mavlink_daemon_config.json>`): `{"ev":"ready"}` po starcie; `{"cmd":"stats"}` → liczniki łączy (`rx_ok`, `rx_crc_err`, `rx_sig_err`, `tx`…),
`rx_duplicate`, `register_rejected`, `instances`, `sessions`; `{"cmd":"stop"}`/EOF = zatrzymanie z zapisem stanu podpisu.

`peer_api.ProcPeer` składa oba procesy: z `links[]` konfiguracji peera robi plik `mavlink_daemon_config.json` (driver `serial_radio` na urządzeniu pty,
podpis), startuje daemon i aplikację, a `stats` zwraca sumę liczników daemona i instancji.

## Wymaganie → test

| Wymaganie | Test |
|-----------|------|
| `on: mavlink` w modelu | `test_mavlink_app.py` (`test_service_generates_proxy_skeleton_items`); `e2e_examples_test.py` (modele przykładów) |
| dwie aplikacje na jednym daemonie / jednym radiu | `e2e_examples_test::test_both_apps_registered_in_one_daemon`, `test_provide_app_events_reach_gs_on_their_links`, `test_require_app_gets_each_gs_event_once_despite_two_links` |
| XML dla GS (mavgen przyjmuje) | `test_mavgen_output.py`; `test_mavlink_dialect.py` (golden XML); `e2e_interop_test::test_peer_event_decoded_by_pymavlink` |
| Lua dla Wiresharka (per CPU) | `tshark_lua_test.py`; `e2e_examples_test::test_tshark_with_cpu_lua_decodes_frames_of_both_apps`; `e2e_interop_test::test_tshark_decodes_frames_captured_from_peer`; `tools/mavlink/serial2udp_test.py` |
| wersja v1 / v2 | `e2e_interop_test` (parametry `v1`/`v2`), `test_cpp_interop.py` (v1, v2, v2 signed) |
| podpis v2 + klucz | `e2e_interop_test::test_wrong_key_is_rejected`, `test_replayed_frame_is_rejected_and_executes_once`, `e2e_signing_persist_test.py`, `tools/mavlink/keygen_test.py` |
| wiele łączy / redundancja / dedup | `e2e_channel_test::test_one_dead_link_*`, `test_both_links_lossy_*`, `test_second_link_delayed_3s_*`, `e2e_examples_test::test_require_app_gets_each_gs_event_once_despite_two_links` |
| kanał stratny | `e2e_channel_test::test_event_delivery_matches_channel_loss`, `test_method_timeout_rate_*` |
| `exclude` | `e2e_interop_test::test_excluded_method_answers_status_2_*`, `test_excluded_event_puts_zero_bytes_on_the_link`; `e2e_examples_test::test_method_call_from_gs_executes_and_excluded_gets_status_2` |
| `max_rate_hz` | `e2e_interop_test::test_max_rate_1hz_vs_20hz_on_two_instances`; `e2e_examples_test::test_telemetry_rate_1hz_vs_20hz` |
| metoda z timeoutem | `e2e_channel_test::test_method_timeout_rate_*`; `e2e_examples_test::test_require_app_method_answered_by_gs_ok_ignored_one_times_out_excluded_fails_fast` |
| restart daemona / aplikacji | `e2e_app_lifecycle_test.py` (restart daemona, restart aplikacji); `ara/com/mavlink/client/ut/test_daemon_client.cc` (zerwanie, ponowna rejestracja, odrzucenie) |
| kolizja `msg_id` | generator: `test_mavlink_cpu::test_msg_id_collision_between_apps_is_generation_error`; daemon: `e2e_app_lifecycle_test::test_msg_id_collision_between_apps_is_rejected_by_daemon` |
| fuzz parsera | `e2e_fuzz_test.py` |

Weryfikator podpisu `mav_helpers.StrictVerifier` działa wg spec (pymavlink 2.4.43 nie wykrywa replayu ramki starszej niż ostatnia).
