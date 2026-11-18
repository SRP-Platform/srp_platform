# MAVLink links of the CPU: dialect `srp_radio`

## Element x link matrix

Cell: `application.instance` (+ the event rate limit of a sender); `-` = the link does not carry the element.

| element | radio1 | radio2 | radio3 | lora | spare |
|---|---|---|---|---|---|
| srp.mav.Radio.DebugCounter (event) | - | - | - | - | - |
| srp.mav.Radio.Position (event) | MavApp.3 @ 2 Hz | MavApp.6 @ 2 Hz | MavApp.6 @ 2 Hz, MavApp.4 (proxy) | MavApp.3 @ 2 Hz, MavApp.5 | - |
| srp.mav.Radio.SetMode (method) | MavApp.3 | MavApp.6 | MavApp.6, MavApp.4 (proxy) | MavApp.3, MavApp.5 | - |
| srp.mav.Radio.LocalOnly (method) | - | - | - | - | - |
| srp.mav.Radio.Reboot (method) | MavApp.3 | MavApp.6 | MavApp.6, MavApp.4 (proxy) | MavApp.3 | - |
| srp.mav.Radio.SetName (method) | MavApp.3 | MavApp.6 | MavApp.6 | MavApp.3, MavApp.5 | - |

## Elements outside MAVLink (`"mavlink": false` or no block)

- srp.mav.Radio.DebugCounter
- srp.mav.Radio.LocalOnly

## Exclusions per instance (`exclude`)

- MavApp.radio_in (instance 4): SetName
- MavApp.radio_lora (instance 5): Reboot
