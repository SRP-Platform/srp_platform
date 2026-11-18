# Generator MAVLink (`"on": "mavlink"`)

Trzecia opcja komunikacji obok `ipc` i `udp` (SOME/IP po UDP — opcja `on`). Specyfikacja: SecBrain `projects/srp-mavlink/README.md`.

## Co generujemy

| Wejście | Wyjście |
|---|---|
| `app_config.json` (`mavlink`, `mavlink_links`, wpisy `on: mavlink`) + `service.json` (bloki `mavlink`) | `<dialect>.xml`, `<dialect>_links.md`, `<dialect>.lua` (WLua), `<dialect>.py` (pymavlink), `<dialect>_c/{v1,v2}` (C) |
| to samo | `initialization.cc` z modelem C++ (`ara/core/model/mavlink_model_item.h`) — tylko model, runtime w osobnym pakiecie |

Reguła bazela: `ara_mavlink_dialect(name, model, data, dialect, versions)` w `mavlink.bzl`
(przykład: `:example_dialect`, model z `testdata/app/`). `versions` = wersje MAVLink, dla których
generujemy C; łącza aplikacji muszą się w nich mieścić.

## Model

* **Łącza** (`mavlink_links`): tylko `version` 1|2, `heartbeat_hz` i `signing` (`enabled`, `key_file`,
  `link_id`, `accept_unsigned`, `state_file`; tylko v2, `link_id` różny dla łączy); `{}` jest poprawne.
  Transportu w modelu nie ma: **każde** łącze bindujesz w kodzie przez `RegisterLinkTransport`
  (np. `SerialTransport(device, baudrate)`). Pola `transport`, `device`, `baudrate`, `bind`, `peer`,
  `max_bytes_per_s` oraz inne (w tym stary blok `radio`/`gpio`) = błąd generatora z nazwą łącza.
* **Usługa** (`on: mavlink`): `instance`, `links` (>= 1; wiele = redundancja), `target_system_id`,
  `target_component_id`, `source_system_id`/`source_component_id` (tylko `require`, 0 = dowolny),
  `dedup_ttl_ms` (domyślnie 10000), `exclude` (elementy wyłączone w tej instancji),
  `max_rate_hz` (nadpisanie limitów eventów, 0 = bez limitu). Nieznana nazwa w
  `exclude`/`max_rate_hz` = błąd; limit dla wyłączonego eventu = ostrzeżenie. Łącza jednej instancji
  muszą mieć identyczne `signing.enabled` i `accept_unsigned`. Te pola przy `on: ipc|udp` = błąd.
* **Element usługi** (`service.json`): `"mavlink": {"msg_id"|"req_id"/"resp_id", ...}`.
  Brak bloku albo `"mavlink": false` = element poza MAVLinkiem (posortowana lista w logu i w
  `_links.md`). Klucz podobny do `mavlink` (literówka, odległość <= 2) albo blok niekompletny/błędny
  (`{}`, `true`, zły typ, nieznane pole) = błąd.
* **Wiadomości**: `SRP_<SERVICE>_<EVENT>`, `SRP_<SERVICE>_<METHOD>_REQ|_RESP`. Nagłówek zawsze:
  event `seq`, `instance`; metoda `seq`, `instance`, `target_system`, `target_component`;
  RESP dodatkowo `status`.

## Walidacja (błąd generatora z nazwą aplikacji/usługi/elementu)

kolizje ID w dialekcie (z HEARTBEAT=0), v1 → ID <= 255 (dla usług na łączach v1), payload <= 255 B,
string wymaga `max_len`, nieznany typ, nieznane łącze, v1 + signing, powtórzony `link_id`,
powtórzona instancja tej samej usługi (provide/require), pola payloadu kolidujące z nagłówkiem
(`seq`, `instance`, `target_*`, `status`) lub słowami zastrzeżonymi C/Lua/Python. Ostrzeżenia: łącze
bez usługi, usługa bez elementu na MAVLinku, kolizja ID z dialektami pymavlink.

Uwagi: `dedup_ttl_ms` = 0 jest odrzucane; `max_rate_hz` = 0 w `service.json` jest odrzucane (0 = „bez
limitu” działa tylko jako nadpisanie w instancji). NaN/Infinity w limitach i `heartbeat_hz` = błąd.
Nazwy elementów usług (po `mavlink`) muszą być ASCII `[A-Za-z][A-Za-z0-9_]*`; nazwy pól po spłaszczeniu
`[a-z][a-z0-9_]*` i nie mogą być składowymi klasy `MAVLink_message` z pymavlink (lista pilnowana
testem `test_python_reserved_names` względem zainstalowanego pymavlink).

## Testy

`bazel test //tools/model_generator/ara/mavlink:all` — golden (`testdata/`), walidacja, wyjście mavgen
(round-trip pymavlink). Golden aktualizujemy świadomie po zmianie spec.

## Uwaga

Platforma nie zna modemów radiowych; łącze bindujesz w kodzie przez `RegisterLinkTransport`.
Spec: rozstrzygnięcia rev. 4 i 5 w SecBrain (runtime — pakiet M2a).
