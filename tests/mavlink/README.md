# tests/mavlink — testy integracyjne transportu MAVLink

Testy jednostkowe leżą przy kodzie (`ara/com/mavlink/ut`, `tools/model_generator/ara/mavlink/test_*.py`, `tools/mavlink/*_test.py`).
Tu jest zwarty zestaw integracyjny: prawdziwy runtime (peer C++) i przykłady `example_app*` rozmawiają z `pymavlink`
przez symulowany kanał radiowy.

## Uruchomienie

```sh
bazel test //tests/mavlink/...                     # bez targetów `manual`
bazel test //tests/mavlink:e2e_soak_30min          # soak 30 min (manual)
bazel test //tests/mavlink:e2e_fuzz_10min          # fuzz losowy 10 min (manual)
bazel run  //tests/mavlink:fuzz_corpus -- --out /tmp/corpus --seed 1
```
Wymagane: `tshark` i `lua5.4` (bez `tshark` testy Wiresharka są pomijane). `pymavlink` tylko przez bazel.
WSL: ciężkie buildy pod `flock -o`, osobny `--output_base` dla ARM.

## Pliki

| Plik | Co sprawdza |
|------|-------------|
| `channel_sim.py` + `_test` | symulator radia (pty: utrata, bitflip, opóźnienie, limit baud, seed) |
| `mav_helpers.py` + `_test` | mavgen z XML w teście (v1/v2), kodowanie, `crc_extra`, podpisy (HMAC liczony niezależnie, zły klucz, replay, osobne strumienie `link_id`) |
| `tshark_harness.py`, `tshark_lua_test.py` | pcap → `tshark -X lua_script:` z wygenerowanym `mavgen --lang=WLua`: wartości pól v1, v2, v2 podpisane |
| `fuzz_corpus.py` + `_test` | korpus do fuzzingu parsera (poprawne, obcięte, zły CRC, losowe, sklejone) |
| `peer/mavlink_peer.cc`, `peer_model/`, `peer_model_v1/` | peer C++ na prawdziwym runtime; model przez generator (v2 i v1) |
| `peer_api.py` | sterowanie peerem, GS na pymavlink, topologia z `ChannelSim` |
| `e2e_interop_test.py` | interop peer ↔ pymavlink v1/v2/v2 signed, `status=2`, target, podpis (zły klucz, replay, starszy timestamp, restart), tshark na ramkach z peera, `max_rate_hz`, `exclude`, łącze zbindowane w kodzie |
| `e2e_channel_test.py` | kanał stratny/bitflip/opóźnienie, redundancja 2 łączy, dedup, restart nadawcy |
| `e2e_signing_persist_test.py` | `state_file` podpisu: restart nadawcy i odbiorcy |
| `e2e_fuzz_test.py` | korpus + losowe/mutowane ramki (10 s; pełne 10 min: `e2e_fuzz_10min`) |
| `e2e_soak_test.py` | soak (20 s; 30 min: `e2e_soak_30min`): RSS, liczniki |
| `m3_examples_e2e_test.py` | `example_app` (skeleton) ↔ `example_app2` (proxy) przez kanał: redundancja, `exclude`, `max_rate_hz` |

## Protokół peera (JSON lines)

Binarka peera czyta ze stdin linie `{"id":N,"cmd":...}` i odpowiada `{"id":N,"ok":true,"result":...}` (błąd: `"ok":false,"error"`).
Polecenia: `start` (`config`: `sysid`, `compid`, `links[]` {name, device|null, version, key_hex, link_id, accept_unsigned,
heartbeat_hz, state_file}, `instances[]` {instance, links, exclude, max_rate_hz, dedup_ttl_ms}, opcjonalnie `seq_start`,
`events_limit`), `emit` (`instance`, `element`, `values`), `call` (`instance`, `method`, `call_timeout`, `args` → `["ok", status, {…}]`
albo `["timeout"]`), `register_transport` (`name`, `device`), `stats` (liczniki), `rss`, `stop`.
Zdarzenia asynchroniczne: `{"ev":"event",…}` (event po filtrze RX i dedup) i `{"ev":"handled",…}` (REQ wykonany).

## Wymaganie → test

| Wymaganie | Test |
|-----------|------|
| `on: mavlink` w modelu | `test_mavlink_app.py` (`test_service_generates_proxy_skeleton_items`); `m3_examples_e2e_test.py` (modele przykładów) |
| XML dla GS (mavgen przyjmuje) | `test_mavgen_output.py`; `test_mavlink_dialect.py` (golden XML); `e2e_interop_test::test_peer_event_decoded_by_pymavlink` |
| Lua dla Wiresharka | `tshark_lua_test.py` (wszystkie); `e2e_interop_test::test_tshark_decodes_frames_captured_from_peer`; `tools/mavlink/serial2udp_test.py` |
| wersja v1 / v2 | `e2e_interop_test` (parametry `v1`/`v2`), `test_cpp_interop.py` (v1, v2, v2 signed) |
| podpis v2 + klucz | `e2e_interop_test::test_wrong_key_is_rejected`, `test_replayed_frame_is_rejected_and_executes_once`, `e2e_signing_persist_test.py`, `tools/mavlink/keygen_test.py` |
| wiele łączy / redundancja / dedup | `e2e_channel_test::test_one_dead_link_*`, `test_both_links_lossy_*`, `test_second_link_delayed_3s_*`, `m3_examples_e2e_test::test_redundant_instance_delivers_each_event_once` |
| kanał stratny | `e2e_channel_test::test_event_delivery_matches_channel_loss`, `test_method_timeout_rate_*` |
| `exclude` | `e2e_interop_test::test_excluded_method_answers_status_2_*`, `test_excluded_event_puts_zero_bytes_on_the_link`; `m3_examples_e2e_test::test_method_over_wifi_is_excluded_with_status_2` |
| `max_rate_hz` | `e2e_interop_test::test_max_rate_1hz_vs_20hz_on_two_instances`; `m3_examples_e2e_test::test_telemetry_rate_1hz_lora_vs_20hz_wifi` |
| łącze w kodzie (`RegisterLinkTransport`) | `e2e_interop_test::test_external_link_waits_for_registration_and_sends_latest_value` |
| fuzz parsera | `e2e_fuzz_test.py` |

Weryfikator podpisu `mav_helpers.StrictVerifier` działa wg spec (pymavlink 2.4.43 nie wykrywa replayu ramki starszej niż ostatnia).
