# Łącza MAVLink — srp.mav.MavApp (dialekt `srp_radio`)

system_id=1, component_id=1

## Łącza

| łącze | MAVLink | HEARTBEAT [Hz] | podpis |
|---|---|---|---|
| radio1 | v2 | 1 | tak (link_id=1, klucz: `/etc/srp/mavlink/radio1.key`, accept_unsigned=false, stan: `/var/lib/srp/mavlink/radio1.ts`) |
| radio2 | v1 | 0 | nie |
| wifi | v2 | 1 | nie |
| lora | v2 | 1 | tak (link_id=2, klucz: `/etc/srp/mavlink/lora.key`, accept_unsigned=false, stan: `/var/lib/app/lora.ts`) |
| spare | v2 | 1 | nie |

## Usługi (instancje)

| usługa | kierunek | instancja | łącza | target sys/comp | source sys/comp | dedup TTL [ms] | exclude | max_rate_hz |
|---|---|---|---|---|---|---|---|---|
| radio_out (srp.mav.Radio) | provide | 3 | radio1, lora | 0/0 | 0/0 | 10000 | - | - |
| radio_lora (srp.mav.Radio) | provide | 5 | lora | 0/0 | 0/0 | 10000 | Reboot | Position=0 |
| radio_mix (srp.mav.Radio) | provide | 6 | radio2, wifi | 0/0 | 0/0 | 10000 | - | - |
| radio_in (srp.mav.Radio) | require | 4 | wifi | 2/1 | 2/1 | 10000 | SetName | - |

## Macierz element × łącze

Komórka: `instancja` (+ limit Hz eventu); `-` = element nie jest nadawany tym łączem.

| element | radio1 | radio2 | wifi | lora | spare |
|---|---|---|---|---|---|
| srp.mav.Radio.DebugCounter (event) | - | - | - | - | - |
| srp.mav.Radio.Position (event) | 3 @ 2 Hz | 6 @ 2 Hz | 6 @ 2 Hz, 4 (proxy) @ 2 Hz | 3 @ 2 Hz, 5 | - |
| srp.mav.Radio.SetMode (method) | 3 | 6 | 6, 4 (proxy) | 3, 5 | - |
| srp.mav.Radio.LocalOnly (method) | - | - | - | - | - |
| srp.mav.Radio.Reboot (method) | 3 | 6 | 6, 4 (proxy) | 3 | - |
| srp.mav.Radio.SetName (method) | 3 | 6 | 6 | 3, 5 | - |

## Elementy poza MAVLinkiem (`"mavlink": false` albo brak bloku)

- srp.mav.Radio.DebugCounter
- srp.mav.Radio.LocalOnly

## Wykluczenia per instancja (`exclude`)

- radio_in (instancja 4): SetName
- radio_lora (instancja 5): Reboot

## Wiadomości

| ID | nazwa | payload [B] |
|---|---|---|
| 200 | SRP_RADIO_POSITION | 13 |
| 210 | SRP_RADIO_SET_MODE_REQ | 7 |
| 211 | SRP_RADIO_SET_MODE_RESP | 8 |
| 212 | SRP_RADIO_REBOOT_REQ | 6 |
| 213 | SRP_RADIO_SET_NAME_REQ | 38 |
| 214 | SRP_RADIO_SET_NAME_RESP | 11 |
