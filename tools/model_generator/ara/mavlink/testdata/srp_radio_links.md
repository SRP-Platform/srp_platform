# MAVLink links of the CPU: dialect `srp_radio`

## Element x link matrix

Cell: `application.instance` (+ the event rate limit of a sender); `-` = the link does not carry the element. Only elements listed in `elements` of some instance appear.

| element | radio1 | radio2 | radio3 | lora | spare |
|---|---|---|---|---|---|
| srp.mav.Radio.Position (event) | MavApp.3 @ 2 Hz | MavApp.6 @ 2 Hz | MavApp.6 @ 0.5 Hz, MavApp.4 (proxy) | MavApp.3 @ 2 Hz, MavApp.5 | - |
| srp.mav.Radio.SetMode (method) | MavApp.3 | MavApp.6 | MavApp.4 (proxy) | MavApp.3, MavApp.5 | - |
| srp.mav.Radio.Reboot (method) | MavApp.3 | - | MavApp.6, MavApp.4 (proxy) | MavApp.3 | - |
| srp.mav.Radio.SetName (method) | MavApp.3 | MavApp.6 | MavApp.6 | MavApp.3, MavApp.5 | - |
