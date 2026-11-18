# MavlinkDaemon

Jedna aplikacja platformy na CPU, która **posiada łącza MAVLink** i obsługuje wiele aplikacji z `on: mavlink`
naraz (wzór: `someip_demon`). Daemon: łącza do radiów, kolejki i priorytety, podpis v2, HEARTBEAT, dedup
kopii z wielu łączy, tablica `crc_extra` budowana z rejestracji aplikacji. Aplikacje to cienkie bindingi —
klienci IPC daemona (rejestracja instancji usług, przekazywanie wiadomości).

```
 aplikacja A ──┐                        ┌── driver radia (plugin .so albo serial_radio) ── eter
 aplikacja B ──┼─ gniazdo SEQPACKET ──  daemon ── łącza "lora" / "radio_b" / … (kolejki, podpis, HEARTBEAT, dedup)
 aplikacja C ──┘   (daemon_protocol.h)     └── driver radia …
```

## Konfiguracja (`mavlink_daemon_config.json`, w srp `deployment`)

```json
{
  "system_id": 1,
  "component_id": 1,
  "socket_path": "/run/srp_mavlink_daemon.sock",
  "socket_mode": "0660",
  "driver_dir": "/usr/lib/srp/mavlink_drivers",
  "restart_delay_ms": 30000,
  "links": {
    "lora": {
      "driver": "ebyte_e220",
      "driver_config": "/etc/srp/mavlink/e220.json",
      "version": 2,
      "heartbeat_hz": 1,
      "signing": { "enabled": true, "key_file": "/etc/srp/mavlink/lora.key", "link_id": 1,
                   "state_file": "/var/lib/srp/mavlink/lora.state", "accept_unsigned": false }
    },
    "radio_b": { "driver": "serial_radio", "driver_config": "/etc/srp/mavlink/radio_b_serial.json" }
  }
}
```

| pole | znaczenie |
|------|-----------|
| `system_id`, `component_id` | tożsamość CPU w MAVLinku (1..255); **jeden `component_id` na CPU**, używany w każdym łączu i HEARTBEAT |
| `socket_path`, `socket_mode` | gniazdo unix dla aplikacji (domyślnie `/run/srp_mavlink_daemon.sock`, prawa `0660`) |
| `driver_dir` | katalog z pluginami `*.so`; pusty/brak pola = tylko wbudowany `serial_radio` |
| `restart_delay_ms` | przerwa przed ponownym `Start()` drivera po błędzie fatalnym albo nieudanym starcie (domyślnie 30 s) |
| `links.<nazwa>.driver` | id drivera z fabryki (wbudowany albo z pluginu) |
| `links.<nazwa>.driver_config` | ścieżka do **własnego pliku drivera** — format zna tylko driver |
| `version` / `heartbeat_hz` / `signing` | wersja nadawania (1\|2, odbiór przyjmuje obie), HEARTBEAT (0 = wyłączony), podpis v2 |

Platforma nie zna żadnego modemu ani medium — nazwy łączy i pluginy dopisuje integrator (srp).

### Błędy startu (daemon nic nie uruchamia, jedna wiadomość + log `Error`)

- plugin ze złą wersją API, bez symboli pluginu albo niewczytywalny (`dlopen`),
- **duplikat id** drivera między pluginami (plugin o id `serial_radio` *zastępuje* wbudowany — wpis w logu),
- brak drivera użytego w łączu (komunikat zawiera nazwę łącza, id i listę znanych driverów),
- brak katalogu `driver_dir`, niemożliwe gniazdo IPC, błędy w configu (wszystkie naraz, z nazwą łącza):
  m.in. **powtórzone `signing.link_id`** na podpisanych łączach (odbiorca łączący redundantne radia ma jeden stan
  znaczników czasu na (link_id, sysid, compid) — kopie z drugiego radia byłyby odrzucane jako replay) oraz **ten sam
  niepusty `signing.state_file`** na dwóch łączach (dwa signery nadpisywałyby jeden plik),
- **działający daemon pod `socket_path`**: start robi próbny `connect`; ktoś nasłuchuje = błąd „daemon już działa”
  (martwe gniazdo po `SIGKILL` jest zastępowane).

`MavlinkDaemonCore` jest **jednorazowy**: po `Stop()` ponowny `Start()` na tym samym obiekcie zwraca błąd (restart
procesu = nowy obiekt).

## Drivery radia

API: `ara/com/mavlink/radio_driver.h` (`IRadioDriver`, `IRadioListener`; opis kontraktu w nagłówku).
Driver ma własny wątek (np. `poll()` na AUX/UART), `Send()` jest nieblokujące: `kBusy` = nic nie przyjęto,
daemon wstrzymuje tę ramkę do `OnTxReady()`; `OnError(fatal)` → daemon `Stop()`, po `restart_delay` `Start()`.

**Wbudowany `serial_radio`** — surowe bajty po UART, bez AT i konfiguracji modemu. Plik drivera:

```json
{ "device": "/dev/ttyS2", "baudrate": 57600 }
```

**Plugin `.so`** (w repo srp):

```python
load("@srp_platform//ara/com/mavlink:mavlink_driver_plugin.bzl", "mavlink_driver_plugin")
mavlink_driver_plugin(name = "ebyte_e220", srcs = ["e220_plugin.cc"], deps = [":e220_driver"])  # libebyte_e220.so
```

```cpp
#include "ara/com/mavlink/radio_driver.h"
void RegisterDrivers(ara::com::mavlink::IRadioDriverRegistry& registry) {
  registry.Register("ebyte_e220", [](const std::string& config_path) { return std::make_shared<E220Driver>(config_path); });
}
SRP_MAVLINK_DRIVER_PLUGIN(RegisterDrivers)   // wersja API + punkt wejścia (extern "C")
```

Jeden `.so` może zarejestrować kilka driverów. Biblioteka nie jest zdejmowana z pamięci (bez `dlclose`).
**Testy kontraktu** dla własnego drivera: `ara/com/mavlink/testing/radio_driver_contract_test.h`
(zero callbacków po `Stop()`, `Send` nieblokujące, `kBusy` → `OnTxReady` bez zapisu częściowego, `Start` po `Stop`,
callbacki z wątku drivera); wzór użycia: `ara/com/mavlink/ut/test_serial_radio.cc` (pty) i
`test_radio_plugin_contract.cc` (plugin ładowany przez `dlopen`).

## Protokół IPC aplikacja ↔ daemon

`ara/com/mavlink/daemon_protocol.h` (+ `daemon_ipc.h`: gniazdo `SOCK_SEQPACKET`). Jedno połączenie = jedna
aplikacja; na nim wiele instancji (`RegisterInstance` z tabelą elementów: msg_id, crc_extra, długość, offsety
pól seq/instance/target/status, timeout, limit częstotliwości). Daemon odrzuca (z powodem w `RegisterReply`):
duplikat `(usługa, instancja, rola)`, msg_id zajęty przez inną definicję/usługę, nieznane łącze, zły opis.
Aplikacje wymieniają **pełne payloady MAVLink** (kodek SOME/IP ↔ MAVLink zostaje po stronie aplikacji).
Rozłączenie aplikacji (także awaria) sprząta jej rejestracje i eventy w kolejkach; restart daemona = aplikacja
łączy się i rejestruje wszystkie instancje od nowa. Metoda bez gotowego łącza kończy się natychmiast
(`SendError{kNoLink}`), pełne FIFO REQ (32 na łącze) = `kQueueFull`.

## Zachowanie łącza

- Kolejność nadawania: HEARTBEAT → REQ/RESP (FIFO, REQ starsze niż timeout metody są porzucane) → eventy
  (koalescencja do najnowszej wartości, `max_rate_hz`).
- `kBusy` wstrzymuje dokładnie zbudowaną (i podpisaną) ramkę; nic jej nie wyprzedza, więc znaczniki czasu podpisu
  rosną w kolejności nadawania; brak `OnTxReady` przez 5 s = Warn w logu (ramka zostaje).
- Błąd fatalny drivera: `Stop()`, status łącza „brak” dla aplikacji, po `restart_delay` ponowny `Start()`;
  ramka wstrzymana w chwili błędu przepada (REQ i tak by wygasł).
- RX: kopiowanie z `OnReceive` i parsowanie w osobnym wątku; ramka trafia do instancji tylko łączem z jej `links`.

### Znane ograniczenia (świadomie, do ewentualnego D4)

- **Wolna aplikacja:** dostarczenie do aplikacji (`Deliver`) odbywa się z wątku RX łącza (ramki) albo wątku nadzorczego
  (zmiana stanu łącza, pod `status_mutex`) i blokuje do `deliver_timeout_ms` (500 ms); po przekroczeniu aplikacja jest
  rozłączana. W tym czasie RX/nadzór tego łącza stoi — akceptowalne przy kilku lokalnych aplikacjach na CPU, ale
  nie projektować aplikacji, która potrafi przez setki ms nie czytać gniazda.
- **`SerialPort::Poll()`** tworzy `eventfd` i `stop_callback` przy każdym wywołaniu (dodatkowe syscalle na każdy kawałek
  RX); TODO: trzymać jeden w stanie portu.

## Testy

```bash
bazel test //platform/common/mavlink_daemon/... //ara/com/mavlink/... //core/serial/... //tests/mavlink:e2e_daemon_test
```

- `code:test_radio_link` — łącze na `MemoryRadio` (stany, restart, HEARTBEAT, kolejki, kBusy, podpis),
- `code:test_mavlink_daemon_core` — daemon + klienci IPC (rejestracja i konflikty, eventy/metody w obu kierunkach,
  redundancja i dedup, sprzątanie po rozłączeniu, błędy startu),
- `code:test_daemon_config` — parser configu,
- `//tests/mavlink:e2e_daemon_test` — prawdziwy proces (`//tests/mavlink:daemon_host`, ten sam rdzeń bez Execution
  Management), pty + pymavlink po stronie radia i niezależny klient IPC w Pythonie (`tests/mavlink/daemon_client.py`).
