# MAVLink w srp_platform — transport radiowy dla usług ara::com

Trzecia opcja komunikacji obok `ipc` i `udp`: wpis `"on": "mavlink"` w modelu aplikacji. Ta sama usługa (ten sam kod skeletona/proxy)
może być wystawiona lokalnie (IPC/UDP) **i** przez radio.

Radiem zajmuje się **jedna aplikacja platformy na CPU — MavlinkDaemon** (`platform/common/mavlink_daemon`): łącza, drivery radiów, kolejki
i priorytety, podpis, HEARTBEAT, deduplikacja. Aplikacje z `on: mavlink` są cienkimi klientami daemona:

```text
 ExampleApp ─┐  rejestracja instancji, eventy, REQ/RESP     ┌─ driver serial_radio ── UART ── radio A
             ├── gniazdo unix SOCK_SEQPACKET ── MavlinkDaemon ┤
 ExampleApp2 ┘  (kodek SOME/IP BE <-> MAVLink LE w aplikacji) └─ driver z pluginu .so ── radio B
```

Wiele aplikacji może więc dzielić to samo radio. Generator tworzy dialekt MAVLink (XML, Lua do Wiresharka, moduł pymavlink) **dla całego CPU**
— ze wszystkich aplikacji z `on: mavlink` — oraz model instancji dla każdej aplikacji.

Działający przykład: `example_app` i `example_app2` na jednym daemonie CPU `test_cpu` (usługa `EngineService`, dwa łącza `radio_a`, `radio_b`).
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
`max_rate_hz` przy evencie to limit usługi (nadmiar = ostatnia wartość w następnym oknie, nie kolejka). Dotyczy to też eventów bez limitu: daemon trzyma
**najnowszą wartość każdego eventu** (koalescencja, nie FIFO), więc przy szybkiej serii `Update` każda wartość nie musi wyjść na radio; REQ/RESP idą FIFO
(limit 32 na łącze); `string` wymaga `max_len` w bloku `mavlink`.

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

## 2. Aplikacja i instancje

Aplikacja opisuje **tylko instancje** usług na `on: mavlink`. Bloki `mavlink` i `mavlink_links` w aplikacji to **błąd generatora** („konfiguracja łączy jest w MavlinkDaemon”):
`system_id`, `component_id`, łącza, drivery, podpis i HEARTBEAT są w konfiguracji daemona (pkt 3). Wpis wybiera łącza listą `links` — **nazwami łączy z konfiguracji daemona**.
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
      "provide": [
        {
          "name": "EngineService as Engine_red",
          "on": "mavlink",
          "instance": 2,
          "links": [
            "radio_a",
            "radio_b"
          ]
        },
        {
          "name": "EngineService as Engine_a",
          "on": "mavlink",
          "instance": 3,
          "links": [
            "radio_a"
          ],
          "exclude": [
            "DebugDump"
          ],
          "max_rate_hz": {
            "Telemetry": 1
          }
        },
        {
          "name": "EngineService as Engine_b",
          "on": "mavlink",
          "instance": 4,
          "links": [
            "radio_b"
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

Druga aplikacja tego samego CPU (ten sam daemon, to samo radio) wymaga usługi od zdalnego systemu 2:

<!-- file: models/app2_config.json -->
```json
{
  "include": [
    "models/engine_service.json"
  ],
  "package": "srp.example",
  "adaptive_application": {
    "ExampleApp2": {
      "app": {
        "functional_groups": [
          "Running"
        ],
        "parms": "-t -10- 100",
        "logger": {
          "app_id": "EXA2",
          "app_des": "Example 2",
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
      "provide": [],
      "require": [
        {
          "name": "EngineService as Engine_red_in",
          "on": "mavlink",
          "instance": 2,
          "links": [
            "radio_a",
            "radio_b"
          ],
          "target_system_id": 2,
          "target_component_id": 1,
          "source_system_id": 2,
          "source_component_id": 1
        }
      ]
    }
  }
}
```

Pola wpisu `on: mavlink`: `instance` (różna dla każdej pary usługa/instancja/rola na CPU — **w obrębie jednego daemona**), `links` (≥ 1), `exclude` (elementy wyłączone w tej instancji;
nieznana nazwa = błąd), `max_rate_hz` (nadpisanie limitu eventów dla tej instancji, `0` = bez limitu; metoda w `max_rate_hz` = błąd), `dedup_ttl_ms`,
`target_system_id`/`target_component_id` (0 = broadcast), a dla `require` także `source_system_id`/`source_component_id` (0 = dowolny). Pola `links`/`exclude`/`max_rate_hz`/`dedup_ttl_ms`
przy `on: ipc|udp` to błąd. Wiadomość każdego elementu ma w nagłówku `seq` i `instance` (metody także `target_system`/`target_component`, RESP dodatkowo `status`: 0 OK, 1 błąd metody,
2 metoda wykluczona/nieznana instancja) — **strona stacji naziemnej musi używać tych samych numerów `instance` i tych samych wykluczeń**.

## 3. Konfiguracja MavlinkDaemona

Jeden plik na CPU, `mavlink_daemon_config.json` (w srp: `deployment`). Opisuje to, czego aplikacje nie znają:

<!-- file: daemon/mavlink_daemon_config.json -->
```json
{
  "system_id": 1,
  "component_id": 1,
  "links": {
    "radio_a": {
      "driver": "serial_radio",
      "driver_config": "/etc/srp/mavlink/radio_a.json",
      "version": 2,
      "heartbeat_hz": 1,
      "signing": {
        "enabled": true,
        "key_file": "/etc/srp/mavlink/radio_a.key",
        "link_id": 1,
        "accept_unsigned": false,
        "state_file": "/var/lib/srp/mavlink/radio_a.ts"
      }
    },
    "radio_b": {
      "driver": "serial_radio",
      "driver_config": "/etc/srp/mavlink/radio_b.json",
      "version": 2,
      "heartbeat_hz": 1,
      "signing": {
        "enabled": true,
        "key_file": "/etc/srp/mavlink/radio_b.key",
        "link_id": 2,
        "accept_unsigned": false,
        "state_file": "/var/lib/srp/mavlink/radio_b.ts"
      }
    }
  }
}
```

* `system_id` / `component_id` — tożsamość CPU w sieci MAVLink (komponent 1 na CPU); daemon podaje je aplikacjom przy rejestracji.
* `links.<nazwa>`: `driver` (id drivera radia z fabryki), `driver_config` (ścieżka do **własnego** pliku drivera — format zna tylko driver), `version` (1|2, domyślnie 2),
  `heartbeat_hz` (0 = bez HEARTBEAT; domyślnie 1), `signing` (pkt 6). Pola `transport`, `device`, `baudrate`, `bind`, `peer`, `max_bytes_per_s` nie należą do łącza (to sprawa drivera);
  nieznane pole = błąd. Opcjonalnie: `socket_path`, `socket_mode`, `driver_dir`, `restart_delay_ms`.
* **Drivery** to biblioteki dynamiczne (`.so`) ładowane przez daemon z katalogu `driver_dir`; jedna biblioteka rejestruje jeden lub kilka driverów po id (reguła bazela
  `mavlink_driver_plugin`, kontrakt `IRadioDriver`/`IRadioListener` i zestaw testów kontraktu w `ara/com/mavlink`). Duplikat id między pluginami, zła wersja API albo brak drivera użytego w łączu = błąd startu daemona.
* Wbudowany driver **`serial_radio`**: surowe ramki MAVLink po UART, bez AT i bez konfiguracji modemu; plik drivera to `{ "device": "/dev/ttyS2", "baudrate": 57600 }`.
  Plugin o tym samym id zastępuje wbudowany (z wpisem w logu).
* Platforma zna **tylko id driverów**: nazwy łączy i ewentualne `.so` pod konkretny modem (Wi‑Fi, LoRa…) dopisuje użytkownik we własnym repozytorium.

Generator sprawdza konfigurację daemona tym samym schematem co daemon (`--daemon-config`) i krzyżowo z aplikacjami CPU: nieznane łącze w `links`, v1 z ID > 255,
łącza jednej instancji z różnym trybem podpisu, powtórzony `link_id` to błędy generacji, a nie dopiero startu na urządzeniu.

## 4. Co idzie którym radiem

Generator wypisuje macierz element × łącze **dla całego CPU** do `<dialekt>_links.md` (reguła `ara_mavlink_dialect`). Dla modeli powyżej:

<!-- generated: links_md -->
```text
# Łącza MAVLink CPU — dialekt `srp_example`

Aplikacje: srp.example.ExampleApp, srp.example.ExampleApp2

system_id=1, component_id=1

## Łącza (config MavlinkDaemona)

| łącze | driver | plik drivera | MAVLink | HEARTBEAT [Hz] | podpis |
|---|---|---|---|---|---|
| radio_a | serial_radio | `/etc/srp/mavlink/radio_a.json` | v2 | 1 | tak (link_id=1, klucz: `/etc/srp/mavlink/radio_a.key`, accept_unsigned=false, stan: `/var/lib/srp/mavlink/radio_a.ts`) |
| radio_b | serial_radio | `/etc/srp/mavlink/radio_b.json` | v2 | 1 | tak (link_id=2, klucz: `/etc/srp/mavlink/radio_b.key`, accept_unsigned=false, stan: `/var/lib/srp/mavlink/radio_b.ts`) |

## Usługi (instancje)

| aplikacja | usługa | kierunek | instancja | łącza | target sys/comp | source sys/comp | dedup TTL [ms] | exclude | max_rate_hz |
|---|---|---|---|---|---|---|---|---|---|
| ExampleApp | Engine_red (srp.example.EngineService) | provide | 2 | radio_a, radio_b | 0/0 | 0/0 | 10000 | - | - |
| ExampleApp | Engine_a (srp.example.EngineService) | provide | 3 | radio_a | 0/0 | 0/0 | 10000 | DebugDump | Telemetry=1 |
| ExampleApp | Engine_b (srp.example.EngineService) | provide | 4 | radio_b | 0/0 | 0/0 | 10000 | SetMode | Telemetry=20 |
| ExampleApp2 | Engine_red_in (srp.example.EngineService) | require | 2 | radio_a, radio_b | 2/1 | 2/1 | 10000 | - | - |

## Macierz element × łącze

Komórka: `aplikacja.instancja` (+ limit Hz eventu); `-` = element nie jest nadawany tym łączem.

| element | radio_a | radio_b |
|---|---|---|
| srp.example.EngineService.CurrentMode (event) | ExampleApp.2, ExampleApp.3, ExampleApp2.2 (proxy) | ExampleApp.2, ExampleApp.4, ExampleApp2.2 (proxy) |
| srp.example.EngineService.Telemetry (event) | ExampleApp.2 @ 2 Hz, ExampleApp.3 @ 1 Hz, ExampleApp2.2 (proxy) @ 2 Hz | ExampleApp.2 @ 2 Hz, ExampleApp.4 @ 20 Hz, ExampleApp2.2 (proxy) @ 2 Hz |
| srp.example.EngineService.Trace (event) | - | - |
| srp.example.EngineService.SetMode (method) | ExampleApp.2, ExampleApp.3, ExampleApp2.2 (proxy) | ExampleApp.2, ExampleApp2.2 (proxy) |
| srp.example.EngineService.DebugDump (method) | ExampleApp.2, ExampleApp2.2 (proxy) | ExampleApp.2, ExampleApp.4, ExampleApp2.2 (proxy) |
| srp.example.EngineService.Reboot (method) | - | - |

## Elementy poza MAVLinkiem (`"mavlink": false` albo brak bloku)

- srp.example.EngineService.Reboot
- srp.example.EngineService.Trace

## Wykluczenia per instancja (`exclude`)

- ExampleApp.Engine_a (instancja 3): DebugDump
- ExampleApp.Engine_b (instancja 4): SetMode

## Wiadomości

| ID | nazwa | payload [B] |
|---|---|---|
| 42100 | SRP_ENGINE_SERVICE_CURRENT_MODE | 5 |
| 42101 | SRP_ENGINE_SERVICE_TELEMETRY | 10 |
| 42110 | SRP_ENGINE_SERVICE_SET_MODE_REQ | 7 |
| 42111 | SRP_ENGINE_SERVICE_SET_MODE_RESP | 8 |
| 42112 | SRP_ENGINE_SERVICE_DEBUG_DUMP_REQ | 6 |
```

Instancje są niezależne (osobne skeletony/proxy, bez deduplikacji między nimi). REQ odebrane na wykluczoną metodę są odrzucane przez daemon (RESP `status=2`);
wywołanie metody wykluczonej po stronie proxy kończy się natychmiast błędem (bez nadawania), subskrypcja wykluczonego eventu daje `kNotSubscribe`.

## 5. Aplikacja jako klient daemona

* **Rejestracja:** przy `StartOffer()`/`StartFind()` aplikacja łączy się z gniazdem daemona (`SRP_MAVLINK_DAEMON_SOCKET`, domyślnie `/run/srp_mavlink_daemon.sock`) i rejestruje instancję
  (rola, `instance`, łącza, filtry, tabela elementów z `msg_id`, `crc_extra` i układem nagłówka). **Konflikt** — ta sama (usługa, instancja, rola) albo ten sam `msg_id` z inną definicją,
  nieznane łącze, zła wersja protokołu — to **odrzucenie rejestracji** (błąd w logu aplikacji; instancja pozostaje niedostępna).
* **Kodek** SOME/IP (big endian) ↔ MAVLink (little endian) zostaje w aplikacji; daemon dostaje pełne payloady MAVLink.
* **Bez daemona albo bez gotowego łącza:** wywołanie metody kończy się błędem **natychmiast** (`kCommunicationLinkError`), `Update()` eventu nic nie wysyła (licznik `tx_dropped`).
* **Restart daemona:** gniazdo się zrywa, wywołania w toku kończą się błędem, a aplikacja sama łączy się ponownie i rejestruje wszystkie instancje od nowa (≤ 5 s od startu daemona).
  **Restart aplikacji:** daemon widzi zamknięcie gniazda i sprząta jej rejestracje.
* Zdarzenia i REQ są wykonywane w wątku instancji (jeden komunikat naraz); handler eventu może wołać metody, ale nie może zatrzymywać (`StopFind`/`StopOffer`) własnej instancji.

Kod przykładów nie zawiera już żadnej konfiguracji łączy: `example_app` wystawia trzy instancje `EngineService`, `example_app2` wymaga ich od zdalnego systemu.
Parametry uruchomienia E2E (daemon, pty, GS na pymavlink) opisuje `tests/mavlink/README.md`.

## 6. Podpis (MAVLink v2)

Podpis jest własnością **łącza w konfiguracji daemona** (`links.<nazwa>.signing`).

* **Klucz:** 32 bajty jako 64 znaki hex w pliku wskazanym przez `signing.key_file`. Wygeneruj: `bazel run //tools/mavlink:keygen -- /etc/srp/mavlink/radio_a.key`
  (plik 0600, narzędzie odmawia zapisu w drzewie git). Ten sam plik musi mieć stacja naziemna. **Klucz nigdy nie trafia do repozytorium.**
* `link_id` musi być **różny dla każdego łącza**; v1 + `signing.enabled` = błąd generatora; wszystkie łącza jednej instancji muszą mieć identyczne
  `signing.enabled` i `accept_unsigned` (mieszany signing = błąd).
* **`state_file`:** znacznik czasu (jednostka 10 µs od 2015-01-01) i ostatni znacznik widziany od peera są zapisywane co ≥ 10 s (i przy zatrzymaniu). Po restarcie
  pierwszy znacznik nadawcy to `max(now, persisted + 60 s, last_valid_peer_ts + 1, last_tx + 1)` — podłoga +60 s jest **jednorazowa**, po starcie rośnie monotonicznie.
  Brak katalogu/pliku: ostrzeżenie, działa bez persist. BBB nie ma RTC: przy zegarze < 2020 i braku stanu nadawca startuje od zera (log Warn).
* **Ograniczenie (R1):** gdy stacja naziemna też startuje od nowa po długiej przerwie BBB, odrzuca podpisane ramki BBB do pierwszej podpisanej ramki od siebie
  (BBB przejmuje jej znacznik) — dlatego **stacja naziemna musi nadawać podpisany HEARTBEAT** (`heartbeat_hz` ≥ 1; QGC/MAVProxy robią to same).
* **Ograniczenie:** po restarcie *nowy* strumień (sysid, compid, link_id) jest odrzucany, gdy `ts + 60 s < max(last_peer, last_tx)`, a po starcie
  `last_tx` = `persisted + 60 s`. Skutek: stacja naziemna z **zegarem cofniętym** po restarcie BBB jest odrzucana (`rx_sig_err`), aż jej znaczniki przekroczą próg — **zsynchronizuj zegar GS**
  (albo odczekaj; podpisany HEARTBEAT z GS po zsynchronizowaniu zegara przywraca łączność). Ramka nagrana <= 60 s przed ostatnim znacznikiem peera jest jeszcze przyjmowana
  (okno replay po restarcie odbiorcy). Replay w obrębie znanego strumienia jest zawsze odrzucany.
* `accept_unsigned: true` przepuszcza ramki bez podpisu (np. łącze mieszane); domyślnie `false` — ramka niepodpisana jest odrzucana.

## 7. Artefakty dla stacji naziemnej i Wireshark

Reguła bazela `ara_mavlink_dialect(name, apps, daemon_config, data, dialect, versions)` generuje **per CPU**: `<dialekt>.xml`, `<dialekt>_links.md`, `<dialekt>.lua` (dissector Wiresharka,
`mavgen --lang=WLua`), `<dialekt>.py` (moduł pymavlink) i nagłówki C (`<dialekt>_c/{v1,v2}`). `apps` = modele **wszystkich** aplikacji CPU z `on: mavlink`
(kolizja `msg_id` między aplikacjami = błąd generacji), `daemon_config` = konfiguracja daemona tego CPU. Przykład: `//test_cpu/cpu/ec:dialect`.
Przykład użycia z pymavlink:

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

## 8. Limity i walidacje generatora

* payload ≤ **255 B** (liczy nagłówek: `seq` 2 B + `instance` 2 B, dla metod +2 B target, RESP +1 B status); pola `string` mają stałe `max_len`;
* łącze **v1**: wszystkie ID ≤ **255**; kolizje ID w obrębie dialektu CPU (z HEARTBEAT = 0) = błąd, także **między aplikacjami**; kolizja z dialektem z `pymavlink/dialects`
  (common, ardupilotmega…) = ostrzeżenie;
* nazwy pól po spłaszczeniu struktur (`a.b` → `a_b`) nie mogą kolidować z nagłówkiem (`seq`, `instance`, `target_system`, `target_component`, `status`) ani ze słowami
  zastrzeżonymi C/Lua/Python; **identyfikatory SOME/IP elementów usługi muszą być unikalne**;
* nieznane łącze w `links` (względem konfiguracji daemona), ta sama (usługa, `instance`, rola) dwa razy na CPU, `exclude`/`max_rate_hz` z nieznaną nazwą, blok `mavlink`/`mavlink_links`
  w aplikacji = błąd; łącze z konfiguracji daemona bez usługi i usługa bez elementu na radiu = ostrzeżenie.

## 9. Liczniki diagnostyczne

Instancja usługi (`MavlinkEndpoint::counters()`): `tx_events`, `tx_excluded` (Update wyłączonego eventu — no-op), `tx_dropped` (brak daemona/rejestracji), `tx_requests`, `tx_responses`, `rx_events`,
`rx_requests`, `rx_responses`, `rx_dropped`, `call_timeouts`, `call_unavailable` (metoda wykluczona / brak daemona / brak łącza), `call_rejected` (daemon odrzucił REQ).
Liczniki łączy (`rx_ok`, `rx_crc_err`, `rx_sig_err`, `tx`, `tx_partial`, `heartbeats_tx/rx`…) i duplikatów z wielu łączy prowadzi daemon.

## 10. Testy

`bazel test //tests/mavlink/...` (daemon + aplikacje + GS na pymavlink przez pty, interop v1/v2/v2 signed, tshark/Lua, restart daemona — opis w `tests/mavlink/README.md`),
`bazel test //tools/mavlink:all` (keygen, serial2udp), `bazel test //tools/mavlink:readme_models_test` (fragmenty JSON z tego pliku generują model i zgadzają się z macierzą z punktu 4).
