# Łącza MAVLink CPU — dialekt `srp_radio`

Aplikacje: srp.mav.MavApp

system_id=1, component_id=1

## Łącza (config MavlinkDaemona)

| łącze | driver | plik drivera | MAVLink | HEARTBEAT [Hz] | podpis |
|---|---|---|---|---|---|
| radio1 | serial_radio | `-` | v2 | 1 | tak (link_id=1, klucz: `/etc/srp/mavlink/radio1.key`, accept_unsigned=false, stan: `(domyślny daemona)`) |
| radio2 | serial_radio | `-` | v1 | 0 | nie |
| radio3 | serial_radio | `-` | v2 | 1 | nie |
| lora | serial_radio | `-` | v2 | 1 | tak (link_id=2, klucz: `/etc/srp/mavlink/lora.key`, accept_unsigned=false, stan: `/var/lib/app/lora.ts`) |
| spare | serial_radio | `-` | v2 | 1 | nie |

## Usługi (instancje)

| aplikacja | usługa | kierunek | instancja | łącza | target sys/comp | source sys/comp | dedup TTL [ms] | exclude | max_rate_hz |
|---|---|---|---|---|---|---|---|---|---|
| MavApp | radio_out (srp.mav.Radio) | provide | 3 | radio1, lora | 0/0 | 0/0 | 10000 | - | - |
| MavApp | radio_lora (srp.mav.Radio) | provide | 5 | lora | 0/0 | 0/0 | 10000 | Reboot | Position=0 |
| MavApp | radio_mix (srp.mav.Radio) | provide | 6 | radio2, radio3 | 0/0 | 0/0 | 10000 | - | - |
| MavApp | radio_in (srp.mav.Radio) | require | 4 | radio3 | 2/1 | 2/1 | 10000 | SetName | - |

## Macierz element × łącze

Komórka: `aplikacja.instancja` (+ limit Hz eventu); `-` = element nie jest nadawany tym łączem.

| element | radio1 | radio2 | radio3 | lora | spare |
|---|---|---|---|---|---|
| srp.mav.Radio.DebugCounter (event) | - | - | - | - | - |
| srp.mav.Radio.Position (event) | MavApp.3 @ 2 Hz | MavApp.6 @ 2 Hz | MavApp.6 @ 2 Hz, MavApp.4 (proxy) @ 2 Hz | MavApp.3 @ 2 Hz, MavApp.5 | - |
| srp.mav.Radio.SetMode (method) | MavApp.3 | MavApp.6 | MavApp.6, MavApp.4 (proxy) | MavApp.3, MavApp.5 | - |
| srp.mav.Radio.LocalOnly (method) | - | - | - | - | - |
| srp.mav.Radio.Reboot (method) | MavApp.3 | MavApp.6 | MavApp.6, MavApp.4 (proxy) | MavApp.3 | - |
| srp.mav.Radio.SetName (method) | MavApp.3 | MavApp.6 | MavApp.6 | MavApp.3, MavApp.5 | - |

## Elementy poza MAVLinkiem (`"mavlink": false` albo brak bloku)

- srp.mav.Radio.DebugCounter
- srp.mav.Radio.LocalOnly

## Wykluczenia per instancja (`exclude`)

- MavApp.radio_in (instancja 4): SetName
- MavApp.radio_lora (instancja 5): Reboot

## Wiadomości

| ID | nazwa | payload [B] |
|---|---|---|
| 200 | SRP_RADIO_POSITION | 13 |
| 210 | SRP_RADIO_SET_MODE_REQ | 7 |
| 211 | SRP_RADIO_SET_MODE_RESP | 8 |
| 212 | SRP_RADIO_REBOOT_REQ | 6 |
| 213 | SRP_RADIO_SET_NAME_REQ | 38 |
| 214 | SRP_RADIO_SET_NAME_RESP | 11 |
