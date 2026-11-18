# MAVLink w srp_platform — transport radiowy dla usług ara::com

Trzecia opcja komunikacji obok `ipc` i `udp`: wpis `"on": "mavlink"` w modelu aplikacji. Ta sama usługa (ten sam kod skeletona/proxy)
może być wystawiona lokalnie (IPC/UDP) **i** przez radio. Generator tworzy dialekt MAVLink, kodek i model łączy; runtime (`ara/com/mavlink/`)
obsługuje łącza, redundancję, deduplikację, metody (seq/timeout), limity eventów i podpis (v2).

Działający przykład: `example_app` (skeleton) i `example_app2` (proxy) — usługa `EngineService` na dwóch łączach (`lora`, `wifi`).
Test E2E przykładów na hoście: `bazel test //tests/mavlink:m3_examples_e2e_test`.

## 1. Definicja usługi (`service.json`) i elementów

Każdy event/metoda ma **jawne ID MAVLink** w bloku `mavlink`. Trzy stany elementu:

| zapis | znaczenie |
|---|---|
| `"mavlink": { "msg_id": 42100 }` (event), `{ "req_id": …, "resp_id": … }` (metoda) | element idzie przez radio |
| `"mavlink": false` | element **poza radiem** (jawnie) |
| brak bloku `mavlink` | to samo co `false` — element poza radiem (generator wypisuje listę takich elementów i umieszcza ją w `<dialekt>_links.md`, żeby przeoczenie było widoczne) |

Klucz podobny do `mavlink` (literówka, np. `mavlnk`) albo blok niekompletny (`{}`, `true`, brak `msg_id`) to **błąd generatora**.
Metoda bez `out` jest fire-and-forget (tylko `req_id`); z `out` — `req_id` + `resp_id`, `timeout_ms` (domyślnie 1000), bez retry.
`max_rate_hz` przy evencie to limit usługi (nadmiar = ostatnia wartość w następnym oknie, nie kolejka). Dotyczy to też eventów bez limitu: kolejka TX łącza trzyma
**najnowszą wartość każdego eventu** (koalescencja, nie FIFO), więc przy szybkiej serii `Update` każda wartość nie musi wyjść na radio; REQ/RESP idą FIFO (limit 32 na łącze); `string` wymaga `max_len` w bloku `mavlink`.

<!-- file: models/engine_mode.json -->
```json
{
  "package": "srp.example",
  "data_structure": {
    "EngineMode": {
      "mode": "uint8"
    }
  }
}
```
<!-- file: models/engine_telemetry.json -->
```json
{
  "package": "srp.example",
  "data_structure": {
    "EngineTelemetry": {
      "rpm": "uint16",
      "temp": "float32"
    }
  }
}
```
<!-- file: models/engine_service.json -->
```json
{
  "include": [
    "models/engine_mode.json",
    "models/engine_telemetry.json"
  ],
  "package": "srp.example",
  "someip": {
    "EngineService": {
      "service_id": 101,
      "major_version": 1,
      "minor_version": 0,
      "methods": {
        "SetMode": {
          "id": 1,
          "mavlink": {
            "req_id": 42110,
            "resp_id": 42111,
            "timeout_ms": 500
          },
          "data_structure": {
            "in": {
              "type": "EngineMode"
            },
            "out": {
              "type": "EngineMode"
            }
          }
        },
        "DebugDump": {
          "id": 2,
          "mavlink": {
            "req_id": 42112
          },
          "data_structure": {
            "in": {
              "type": "void"
            },
            "out": {
              "type": "void"
            }
          }
        },
        "Reboot": {
          "id": 3,
          "data_structure": {
            "in": {
              "type": "void"
            },
            "out": {
              "type": "void"
            }
          }
        }
      },
      "events": {
        "CurrentMode": {
          "id": 32769,
          "mavlink": {
            "msg_id": 42100
          },
          "data_structure": {
            "out": {
              "type": "EngineMode"
            }
          }
        },
        "Telemetry": {
          "id": 32770,
          "mavlink": {
            "msg_id": 42101,
            "max_rate_hz": 2
          },
          "data_structure": {
            "out": {
              "type": "EngineTelemetry"
            }
          }
        },
        "Trace": {
          "id": 32771,
          "mavlink": false,
          "data_structure": {
            "out": {
              "type": "uint8"
            }
          }
        }
      }
    }
  }
}
```

Konwencja platformy: **jedna usługa na plik `service`** i **jedna struktura na plik** (parsery biorą pierwszy wpis z pliku); zależności przez `include`.

## 2. Aplikacja, łącza i instancje

Łącza definiuje się raz (`mavlink_links`), a każdy wpis `on: mavlink` w `provide`/`require` wybiera je listą `links`.
Wiele łączy w jednym wpisie = **redundancja**: każda ramka idzie na wszystkie łącza, odbiorca deduplikuje (okno `dedup_ttl_ms`, domyślnie 10000;
duplikat = to samo `seq` i CRC32 payloadu). **Różny zakres per radio = osobne instancje** tej samej usługi z `exclude` i `max_rate_hz`:

<!-- file: models/app_config.json -->
```json
{
  "include": [
    "models/engine_service.json"
  ],
  "package": "srp.example",
  "adaptive_application": {
    "ExampleApp": {
      "app": {
        "functional_groups": [
          "Running"
        ],
        "parms": "-t -10- 100",
        "logger": {
          "app_id": "EXA-",
          "app_des": "Example",
          "log_level": "kInfo",
          "log_mode": "kRemote",
          "ctx": [
            {
              "ctx_id": "ara",
              "log_level": "kInfo",
              "ctx_des": "Default ctx for ara"
            }
          ]
        }
      },
      "mavlink": {
        "system_id": 1,
        "component_id": 1,
        "dialect": "srp_example"
      },
      "mavlink_links": {
        "lora": {
          "version": 2,
          "heartbeat_hz": 1,
          "signing": {
            "enabled": true,
            "key_file": "/etc/srp/mavlink/lora.key",
            "link_id": 1,
            "accept_unsigned": false,
            "state_file": "/var/lib/srp/mavlink/lora.ts"
          }
        },
        "wifi": {
          "version": 2,
          "heartbeat_hz": 1,
          "signing": {
            "enabled": true,
            "key_file": "/etc/srp/mavlink/wifi.key",
            "link_id": 2,
            "accept_unsigned": false,
            "state_file": "/var/lib/srp/mavlink/wifi.ts"
          }
        }
      },
      "provide": [
        {
          "name": "EngineService as Engine_red",
          "on": "mavlink",
          "instance": 2,
          "links": [
            "lora",
            "wifi"
          ]
        },
        {
          "name": "EngineService as Engine_lora",
          "on": "mavlink",
          "instance": 3,
          "links": [
            "lora"
          ],
          "exclude": [
            "DebugDump"
          ],
          "max_rate_hz": {
            "Telemetry": 1
          }
        },
        {
          "name": "EngineService as Engine_wifi",
          "on": "mavlink",
          "instance": 4,
          "links": [
            "wifi"
          ],
          "exclude": [
            "SetMode"
          ],
          "max_rate_hz": {
            "Telemetry": 20
          }
        }
      ],
      "require": []
    }
  }
}
```

Pola wpisu `on: mavlink`: `instance` (różna dla każdej pary usługa/instancja na mavlinku), `links` (≥ 1), `exclude` (elementy wyłączone w tej instancji; nieznana nazwa = błąd),
`max_rate_hz` (nadpisanie limitu eventów dla tej instancji, `0` = bez limitu; metoda w `max_rate_hz` = błąd), `dedup_ttl_ms`, `target_system_id`/`target_component_id`
(0 = broadcast), a dla `require` także `source_system_id`/`source_component_id` (0 = dowolny). Pola `links`/`exclude`/`max_rate_hz`/`dedup_ttl_ms` przy `on: ipc|udp` to błąd.
Wiadomość każdego elementu ma w nagłówku `seq` i `instance` (metody także `target_system`/`target_component`, RESP dodatkowo `status`: 0 OK, 1 błąd metody,
2 metoda wykluczona/nieznana instancja) — **strona stacji naziemnej musi używać tych samych numerów `instance` i tych samych wykluczeń**.

## 3. Co idzie którym radiem

Generator wypisuje macierz element × łącze do `<dialekt>_links.md` (reguła `ara_mavlink_dialect`). Dla modelu powyżej:

<!-- generated: links_md -->
```text
# Łącza MAVLink — srp.example.ExampleApp (dialekt `srp_example`)

system_id=1, component_id=1

## Łącza

| łącze | MAVLink | HEARTBEAT [Hz] | podpis |
|---|---|---|---|
| lora | v2 | 1 | tak (link_id=1, klucz: `/etc/srp/mavlink/lora.key`, accept_unsigned=false, stan: `/var/lib/srp/mavlink/lora.ts`) |
| wifi | v2 | 1 | tak (link_id=2, klucz: `/etc/srp/mavlink/wifi.key`, accept_unsigned=false, stan: `/var/lib/srp/mavlink/wifi.ts`) |

## Usługi (instancje)

| usługa | kierunek | instancja | łącza | target sys/comp | source sys/comp | dedup TTL [ms] | exclude | max_rate_hz |
|---|---|---|---|---|---|---|---|---|
| Engine_red (srp.example.EngineService) | provide | 2 | lora, wifi | 0/0 | 0/0 | 10000 | - | - |
| Engine_lora (srp.example.EngineService) | provide | 3 | lora | 0/0 | 0/0 | 10000 | DebugDump | Telemetry=1 |
| Engine_wifi (srp.example.EngineService) | provide | 4 | wifi | 0/0 | 0/0 | 10000 | SetMode | Telemetry=20 |

## Macierz element × łącze

Komórka: `instancja` (+ limit Hz eventu); `-` = element nie jest nadawany tym łączem.

| element | lora | wifi |
|---|---|---|
| srp.example.EngineService.CurrentMode (event) | 2, 3 | 2, 4 |
| srp.example.EngineService.Telemetry (event) | 2 @ 2 Hz, 3 @ 1 Hz | 2 @ 2 Hz, 4 @ 20 Hz |
| srp.example.EngineService.Trace (event) | - | - |
| srp.example.EngineService.SetMode (method) | 2, 3 | 2 |
| srp.example.EngineService.DebugDump (method) | 2 | 2, 4 |
| srp.example.EngineService.Reboot (method) | - | - |

## Elementy poza MAVLinkiem (`"mavlink": false` albo brak bloku)

- srp.example.EngineService.Reboot
- srp.example.EngineService.Trace

## Wykluczenia per instancja (`exclude`)

- Engine_lora (instancja 3): DebugDump
- Engine_wifi (instancja 4): SetMode

## Wiadomości

| ID | nazwa | payload [B] |
|---|---|---|
| 42100 | SRP_ENGINE_SERVICE_CURRENT_MODE | 5 |
| 42101 | SRP_ENGINE_SERVICE_TELEMETRY | 10 |
| 42110 | SRP_ENGINE_SERVICE_SET_MODE_REQ | 7 |
| 42111 | SRP_ENGINE_SERVICE_SET_MODE_RESP | 8 |
| 42112 | SRP_ENGINE_SERVICE_DEBUG_DUMP_REQ | 6 |
```

Instancje są niezależne (osobne skeletony/proxy, bez deduplikacji między nimi). REQ odebrane na wykluczoną metodę są odrzucane (RESP `status=2` i licznik
`rx_rejected`); wywołanie metody wykluczonej po stronie proxy kończy się natychmiast błędem (bez nadawania), subskrypcja wykluczonego eventu daje `kNotSubscribe`.

## 4. Łącza i transporty

Łącze w `mavlink_links` ma tylko pola: `version` (1|2, domyślnie 2), `heartbeat_hz` (0 = bez HEARTBEAT; domyślnie 1) i opcjonalny `signing`. `{}` to poprawne łącze.
Pola `transport`, `device`, `baudrate`, `bind`, `peer`, `max_bytes_per_s` (oraz stary blok `radio`/`gpio`) = błąd generatora z nazwą łącza — platforma **nie otwiera żadnego
urządzenia z modelu**: **każde** łącze bindujesz w kodzie przez `RegisterLinkTransport`. Tempo nadawania to sprawa transportu (np. driver modemu).
Gotowy port szeregowy to klasa `ara::com::mavlink::SerialTransport(nazwa_łącza, device, baudrate)` (termios raw 8N1, wyłączność portu `TIOCEXCL` + `flock`; także pty) —
tworzysz ją w kodzie i rejestrujesz.

**Rejestracja transportu** — kontrakt: `ara::com::mavlink::RegisterLinkTransport(nazwa_łącza, std::shared_ptr<IByteTransport>)`.
Kolejność: **`Initialize()` → `RegisterLinkTransport()` → `StartOffer()`** (albo użycie proxy). `Initialize()` robi `RunAdaptiveLifecycle` przed `Run()`; w `Run()` zarejestruj
transport, a dopiero potem wystaw/szukaj usługi. Bez rejestracji ruch czeka w kolejkach, a log Warn pojawia się po 5 s (rejestracja po `StartOffer()` też działa).
Błąd rejestracji: nieznana nazwa łącza, `nullptr`, duplikat. `UnregisterLinkTransport(nazwa)` zamyka transport.

```cpp
#include "ara/com/mavlink/link_transport_registry.h"
#include "ara/com/mavlink/serial_transport.h"

// w Run(), po Initialize() i PRZED StartOffer():
const auto reg = ara::com::mavlink::RegisterLinkTransport(
    "lora", std::make_shared<ara::com::mavlink::SerialTransport>("lora", "/dev/ttyS1", 57600));
if (!reg.HasValue()) {
  ara::log::LogError() << "RegisterLinkTransport(lora) failed";
}
service.StartOffer();
```

**Przykłady `example_app`/`example_app2`:** oba łącza (`lora`, `wifi`) są rejestrowane w kodzie jako `SerialTransport`. `lora` tylko wtedy, gdy ustawiono zmienną
`SRP_EXAMPLE_LORA_DEVICE` (bez niej łącze czeka na transport i żaden UART nie jest zajęty); `wifi` z `SRP_EXAMPLE_WIFI_DEVICE` (domyślnie `/tmp/srp_example/wifi_a` / `wifi_b` —
**ścieżki testowe**, symlinki do pty w teście E2E na hoście; na urządzeniu ustaw prawdziwy port, np. `/dev/ttyS2`).

Własny transport implementuje `ara::com::mavlink::IByteTransport` (`Open`, `Read`, `Write`, `Close`; kontrakt i wspólny zestaw testów w `ara/com/mavlink/testing/`).
`Open` jest wołany w wątku łącza i może blokować (tu driver konfiguruje modem); błąd `Open`/`Read`/`Write` = stan `kOpenError`, ponowienie co 30 s, pozostałe łącza działają.

## 5. Podpis (MAVLink v2)

* **Klucz:** 32 bajty jako 64 znaki hex w pliku wskazanym przez `signing.key_file`. Wygeneruj: `bazel run //tools/mavlink:keygen -- /etc/srp/mavlink/lora.key`
  (plik 0600, narzędzie odmawia zapisu w drzewie git). Ten sam plik musi mieć stacja naziemna. **Klucz nigdy nie trafia do repozytorium.**
* `link_id` musi być **różny dla każdego łącza**; v1 + `signing.enabled` = błąd generatora; wszystkie łącza jednej instancji muszą mieć identyczne
  `signing.enabled` i `accept_unsigned` (mieszany signing = błąd).
* **`state_file`:** znacznik czasu (jednostka 10 µs od 2015-01-01) i ostatni znacznik widziany od peera są zapisywane co ≥ 10 s (i przy zatrzymaniu). Po restarcie
  pierwszy znacznik nadawcy to `max(now, persisted + 60 s, last_valid_peer_ts + 1, last_tx + 1)` — podłoga +60 s jest **jednorazowa**, po starcie rośnie monotonicznie.
  Brak katalogu/pliku: ostrzeżenie, działa bez persist. BBB nie ma RTC: przy zegarze < 2020 i braku stanu nadawca startuje od zera (log Warn).
* **Ograniczenie (R1):** gdy stacja naziemna też startuje od nowa po długiej przerwie BBB, odrzuca podpisane ramki BBB do pierwszej podpisanej ramki od siebie
  (BBB przejmuje jej znacznik) — dlatego **stacja naziemna musi nadawać podpisany HEARTBEAT** (`heartbeat_hz` ≥ 1; QGC/MAVProxy robią to same).
* **Ograniczenie (rev. 6, zgodnie z runtime):** po restarcie *nowy* strumień (sysid, compid, link_id) jest odrzucany, gdy `ts + 60 s < max(last_peer, last_tx)`, a po starcie
  `last_tx` = `persisted + 60 s`. W praktyce odrzucane są więc ramki starsze niż **własny zapisany znacznik** (ściślej niż samo „60 s od ostatniej ramki peera”). Skutek: stacja
  naziemna z **zegarem cofniętym** po restarcie BBB (znaczniki GS < własny zapisany stan) jest odrzucana (`rx_sig_err`), aż jej znaczniki przekroczą próg — **zsynchronizuj zegar GS**
  (albo odczekaj; podpisany HEARTBEAT z GS po zsynchronizowaniu zegara przywraca łączność). Ramka nagrana <= 60 s przed ostatnim znacznikiem peera jest jeszcze przyjmowana
  (okno replay po restarcie odbiorcy). Replay w obrębie znanego strumienia jest zawsze odrzucany.
* `accept_unsigned: true` przepuszcza ramki bez podpisu (np. łącze mieszane); domyślnie `false` — ramka niepodpisana jest odrzucana.

## 6. Artefakty dla stacji naziemnej i Wireshark

Reguła bazela `ara_mavlink_dialect(name, model, data, dialect, versions)` generuje: `<dialekt>.xml`, `<dialekt>_links.md`, `<dialekt>.lua` (dissector Wiresharka,
`mavgen --lang=WLua`), `<dialekt>.py` (moduł pymavlink) i nagłówki C (`<dialekt>_c/{v1,v2}`). Przykład użycia z pymavlink:

```python
import serial
import srp_example  # wygenerowany moduł <dialekt>.py (w PYTHONPATH)

port = serial.Serial("/dev/ttyUSB0", 57600)
mav = srp_example.MAVLink(port)
mav.robust_parsing = True
while True:
    for msg in mav.parse_buffer(port.read(256)) or []:
        if msg.get_type() == "SRP_ENGINE_SERVICE_TELEMETRY":
            print(msg.instance, msg.rpm, msg.temp)
```

Uwaga: `mavutil.mavlink_connection(..., dialect="srp_example")` ładuje moduł `pymavlink.dialects.v20.srp_example`, a **nie** lokalny plik — żeby go użyć, skopiuj
wygenerowany `srp_example.py` do katalogu `pymavlink/dialects/v20/` zainstalowanego pakietu. Droga z `MAVLink(port)` powyżej działa bez instalowania (testowana w `readme_models_test`).

Wireshark na żywo ze strumienia UART: `serial2udp` składa ramki i wysyła **jedną ramkę na datagram** na UDP 14550:

```sh
bazel run //tools/mavlink:serial2udp -- /dev/ttyUSB0 --baud 57600 --dialect srp_example.py
wireshark -k -i lo -f "udp port 14550" -X lua_script:srp_example.lua
```

`--dialect` włącza walidację CRC (bez niego ramka jest akceptowana, gdy po niej zaczyna się kolejny magic albo kończy strumień).

## 7. Limity i walidacje generatora

* payload ≤ **255 B** (liczy nagłówek: `seq` 2 B + `instance` 2 B, dla metod +2 B target, RESP +1 B status); pola `string` mają stałe `max_len`;
* łącze **v1**: wszystkie ID ≤ **255**; kolizje ID w obrębie dialektu aplikacji (z HEARTBEAT = 0) = błąd; kolizja z dialektem z `pymavlink/dialects` (common, ardupilotmega…) = ostrzeżenie;
* nazwy pól po spłaszczeniu struktur (`a.b` → `a_b`) nie mogą kolidować z nagłówkiem (`seq`, `instance`, `target_system`, `target_component`, `status`) ani ze słowami
  zastrzeżonymi C/Lua/Python; **identyfikatory SOME/IP elementów usługi muszą być unikalne**;
* nieznane łącze w `links`, ta sama (usługa, `instance`) dwa razy na mavlinku, `exclude`/`max_rate_hz` z nieznaną nazwą = błąd; łącze bez usługi i usługa bez elementu na radiu = ostrzeżenie.

## 8. Liczniki diagnostyczne

Łącze (`MavlinkLink::counters()`): `rx_ok`, `rx_crc_err`, `rx_sig_err` (zły podpis, replay, niepodpisane), `rx_dropped` (nieznany msgid/flagi/śmieci), `tx`, `tx_bytes`,
`tx_partial` (porzucona reszta ramki po timeoucie zapisu), `tx_dropped` (pełna kolejka RESP), `heartbeats_tx`, `heartbeats_rx`, `opens`, `open_errors`; stan łącza:
`kStopped`, `kWaitingForTransport` (brak rejestracji transportu), `kOpening`, `kRunning`, `kOpenError`, `kInitError` (np. zły klucz — łącze nie nadaje).

Endpoint (instancja usługi, `MavlinkEndpoint::counters()`): `tx_events`, `tx_excluded` (Update wyłączonego eventu — no-op), `tx_requests`, `tx_responses`, `rx_events`, `rx_requests`,
`rx_responses`, `rx_rejected` (wykluczony element/nieznana instancja), `rx_duplicate`, `call_timeouts`, `call_unavailable` (metoda wykluczona / brak łącza); `LinkManager::duplicates()`
liczy odrzucone duplikaty z wielu łączy.

## 9. Testy

`bazel test //tests/mavlink/...` (symulator kanału radiowego, interop z pymavlink, tshark/Lua, podpisy, redundancja, fuzz, soak — opis w `tests/mavlink/README.md`),
`bazel test //tools/mavlink:all` (keygen, serial2udp), `bazel test //tools/mavlink:readme_models_test` (fragmenty JSON z tego pliku generują model i zgadzają się z macierzą z punktu 3).
