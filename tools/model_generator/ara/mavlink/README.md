# Generator MAVLink (`"on": "mavlink"`)

Trzecia opcja komunikacji obok `ipc` i `udp` (SOME/IP po UDP — opcja `on`). Specyfikacja: SecBrain `projects/srp-mavlink/README.md` (rev. 10).

## Co generujemy

| Wejście | Wyjście |
|---|---|
| `app_config.json` **wszystkich aplikacji CPU** (wpisy `on: mavlink`) + `service.json` (bloki `mavlink`) + opcjonalnie `mavlink_daemon_config.json` CPU | `<dialect>.xml`, `<dialect>_links.md` (macierz łączy CPU), `<dialect>.lua` (WLua), `<dialect>.py` (pymavlink), `<dialect>_c/{v1,v2}` (C) — **per CPU** |
| `app_config.json` jednej aplikacji | `initialization.cc` z modelem C++ instancji (`ara/core/model/mavlink_model_item.h`) |

Reguła bazela: `ara_mavlink_dialect(name, apps, daemon_config, data, dialect, versions)` w `mavlink.bzl`
(przykłady: `:example_dialect` z `testdata/app/`, `//test_cpu/cpu/ec:dialect` z `example_app*`). `apps` = modele wszystkich aplikacji
z `on: mavlink` jednego CPU, `daemon_config` (opcjonalny) = config MavlinkDaemona tego CPU, `versions` = wersje MAVLink, dla których generujemy C
(łącza daemona muszą się w nich mieścić). CLI: `mavlink_gen <out.xml> --dialect N [--daemon-config cfg.json] [--links-md out.md] modele...`.

## Model

* **Aplikacja** opisuje tylko instancje usług. Bloki `mavlink` i `mavlink_links` w aplikacji = błąd z podpowiedzią „konfiguracja łączy jest w MavlinkDaemon”.
* **Instancja** (`on: mavlink`): `instance`, `links` (>= 1; **nazwy łączy z configu daemona**; wiele = redundancja), `target_system_id`,
  `target_component_id`, `source_system_id`/`source_component_id` (tylko `require`, 0 = dowolny), `dedup_ttl_ms` (domyślnie 10000), `exclude`
  (elementy wyłączone w tej instancji), `max_rate_hz` (nadpisanie limitów eventów, 0 = bez limitu). Nieznana nazwa w `exclude`/`max_rate_hz` = błąd; limit dla
  wyłączonego eventu = ostrzeżenie. Te pola przy `on: ipc|udp` = błąd.
* **Config daemona** (`mavlink_daemon_config.json`, schemat jak w `platform/common/mavlink_daemon`): `system_id`, `component_id`, `links.<nazwa>` =
  `driver` (id), `driver_config` (ścieżka do pliku drivera), `version` 1|2, `heartbeat_hz`, `signing` (`enabled`, `key_file`, `link_id`, `accept_unsigned`, `state_file`;
  tylko v2, `link_id` różny dla łączy). Walidacja w `mavlink_daemon_config.py`; stare pola łącza (`transport`, `device`, `baudrate`, `bind`, `peer`, `max_bytes_per_s`) = błąd.
* **Element usługi** (`service.json`): `"mavlink": {"msg_id"|"req_id"/"resp_id", ...}`. Brak bloku albo `"mavlink": false` = element poza MAVLinkiem (posortowana
  lista w logu i w `_links.md`). Klucz podobny do `mavlink` (literówka, odległość <= 2) albo blok niekompletny/błędny = błąd.
* **Wiadomości**: `SRP_<SERVICE>_<EVENT>`, `SRP_<SERVICE>_<METHOD>_REQ|_RESP`. Nagłówek: event `seq`, `instance`; metoda `seq`, `instance`, `target_system`,
  `target_component`; RESP dodatkowo `status`.
* **Tożsamość usługi dla daemona** (`opts.service_name` w modelu C++) = nazwa usługi: wszystkie instancje i role tej usługi dzielą `msg_id`, a daemon liczy konflikt instancji
  per (usługa, instancja, rola).

## Walidacja (błąd generatora z nazwą aplikacji/usługi/elementu)

Per aplikacja: kolizje ID w dialekcie (z HEARTBEAT=0), payload <= 255 B, string wymaga `max_len`, nieznany typ, powtórzona instancja tej samej usługi, pola payloadu
kolidujące z nagłówkiem lub słowami zastrzeżonymi C/Lua/Python, bloki `mavlink`/`mavlink_links` w aplikacji.
Per CPU (wszystkie aplikacje razem): **kolizja `msg_id` między różnymi usługami/aplikacjami**, ta sama (usługa, instancja, rola) w dwóch aplikacjach, a z configiem daemona także
nieznane łącze, v1 → ID <= 255, v1 + signing, powtórzony `link_id`, łącza jednej instancji z różnym `signing.enabled`/`accept_unsigned`.
Ostrzeżenia: łącze z configu daemona bez usługi, usługa bez elementu na MAVLinku, kolizja ID z dialektami pymavlink.

Uwagi: `dedup_ttl_ms` = 0 jest odrzucane; `max_rate_hz` = 0 w `service.json` jest odrzucane (0 = „bez limitu” działa tylko jako nadpisanie w instancji). NaN/Infinity
w limitach i `heartbeat_hz` = błąd. Nazwy elementów usług (po `mavlink`) muszą być ASCII `[A-Za-z][A-Za-z0-9_]*`; nazwy pól po spłaszczeniu `[a-z][a-z0-9_]*` i nie mogą
być składowymi klasy `MAVLink_message` z pymavlink (lista pilnowana testem `test_python_reserved_names` względem zainstalowanego pymavlink).
Wspólne `include` kilku aplikacji CPU są wczytywane raz (`CommonParser.LoadJson(path, loaded)`).

## Testy

`bazel test //tools/model_generator/ara/mavlink:all` — golden (`testdata/`), walidacja aplikacji (`test_mavlink_app`), generacja per CPU i config daemona
(`test_mavlink_cpu`), wyjście mavgen (round-trip pymavlink). Golden aktualizujemy świadomie po zmianie spec.

## Uwaga

Platforma nie zna modemów radiowych ani medium transmisji: łącza i drivery (id) opisuje config daemona; własne drivery to pluginy `.so` dopisywane przez użytkownika.
