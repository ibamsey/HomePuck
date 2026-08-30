# Agent Guidelines for HomePuck

Guidance for AI assistants working on the HomePuck round-display wind instrument firmware.

---

## Project summary

HomePuck is an **ESP32-C3** puck (240×240 GC9A01) that shows a bow-up wind compass from SignalK over the OpenPlotter WiFi AP. UI is **LovyanGFX** code in `src/display/` — not LVGL/EEZ.

Related project (shared WiFi stack origin): `crowPanelWeather/5.79-inch-E-ink-dashboard-with-LVGL-9` (OpenRemote).

---

## Critical: do not edit `src/ui/`

`src/ui/` contains leftover EEZ/LVGL artifacts and is **excluded from the build** (`platformio.ini`: `-<ui/>`). All live UI is in:

```
src/display/
  ├── wind_widget.cpp/h       ← Wind dial, AWS/TWS pills
  ├── sog_speedometer_screen.cpp/h ← SOG dial (screen 2)
  ├── energy_flow_screen.cpp/h ← Energy hub diagram (screen 3)
  ├── user_screens.cpp/h      ← Screen registry, nav state, draw wrappers
  └── display_manager.cpp/h   ← Status screens, connection badge, dispatch

src/LGFX_config.h             ← GC9A01 panel + SPI bus
```

| Task | Do this | NOT this |
|------|---------|----------|
| Add wind UI element | `src/display/wind_widget.cpp` | `src/ui/screens.c` |
| Add new user screen | Registry row in `user_screens.cpp` + render module | Logic in `main.cpp` |
| Add screen variation | Named constant in screen header + `draw()` branch | Logic in `UserScreenNav` |
| Change layout/colours | Screen render module (`wind_widget.cpp`, etc.) | EEZ Studio |
| Connection overlay | `display_manager.cpp` badge | Full-screen takeover on SK reconnect |

---

## Critical: ESP32-C3 RAM budget

| Consumer | Approx. size |
|----------|----------------|
| Wind static sprite | ~115 KB (240×240 RGB565) |
| WiFi + TCP stack | tens of KB |
| WebSocket client | tens of KB per instance |

**There is no PSRAM.** Steady-state free heap after connect is ~80–95 KB. Do not copy OpenRemote's "abandon WebSocket without destroy" reconnect pattern — see [ADR-001](docs/adr/001-signalk-websocket-lifecycle.md).

Main-loop rules (same rationale as OpenRemote):

- No LVGL/WebSocket work in WiFi-sensitive tight loops without `yield()`
- SignalK callbacks must stay minimal (no `delay()`, no destroy)
- Use deferred flags (`_subscription_pending`) for work that belongs in `loop()`

---

## SignalK: hardcoded paths (not metadata binding)

Unlike OpenRemote, paths are listed in `SignalKClient::begin()` in `src/signalk/signalk_client.cpp`. To add a path:

1. Add to `_subscribed_paths` in `begin()`
2. Handle in `_storeWindValue()` (or extend `WindData`)
3. Wire through `display_manager` / screen render module as needed

Do **not** port OpenRemote's `signalk_bindings.cpp` metadata table unless explicitly migrating architecture.

Current subscriptions:

| Path | Field |
|------|-------|
| `environment.wind.speedApparent` | AWS |
| `environment.wind.angleApparent` | AWA |
| `environment.wind.speedOverGround` | TWS (primary on this boat) |
| `environment.wind.speedTrue` | TWS fallback |
| `environment.wind.directionTrue` | TWD |
| `navigation.headingTrue` | Bow-relative TWD dial |
| `navigation.speedOverGround` | SOG speedometer |
| `electrical.solar.0.power` | Victron A — fixed solar (W) |
| `electrical.consumption.0.power` | Victron A — boat load (W) |
| `electrical.solar.1.power` | Victron B — portable solar (W) |
| `electrical.consumption.1.power` | Victron B — fridge load (W) |
| `electrical.batteries.0.power` | Ecoworthy LiFePO — charge/discharge (W) |
| `electrical.batteries.0.capacity.stateOfCharge` | Ecoworthy LiFePO — SOC |

---

## WiFi stack

WiFi manager is a port of OpenRemote's module (`src/wifi/wifi_manager.cpp`). Strategy is documented in OpenRemote [ADR-001 WiFi Connection Strategy](../crowPanelWeather/5.79-inch-E-ink-dashboard-with-LVGL-9/docs/adr/001-wifi-connection-strategy.md).

HomePuck-specific serial debug: [docs/wifi_debug.md](docs/wifi_debug.md) (`DEBUG_WIFI=1` in `config.h`).

**HomePuck dev MAC:** `84:FC:E6:B4:03:D8` — use for Pi `deauthenticate` after reboot (AUTH_EXPIRE).

---

## Display behaviour

- **User screens** stay visible during SignalK reconnect; badge shows `SK...` / `No SK` (not full-screen status).
- **Touch navigation** (CST816S, `touch.disable_auto_sleep()` required):
  - Swipe **left/right** — cycle screen modes (Wind → SOG → Energy → Wind)
  - Swipe **up/down** — cycle variations within current screen (Wind: AWS/TWS)
- **SOG speedometer** on screen 2 — 0–15 kts colour dial, amber VU peak marker (10 s decay)
- **Energy flow** on screen 3 — hub-and-spoke diagram (HA-style): fixed/portable solar → battery → boat/fridge loads; demo animation until SignalK paths live
- **Dirty-check** in `display_manager` skips redraw when values unchanged; SOG peak decay forces periodic updates.

### Adding screens or variations

Follow [docs/user_screens_architecture.md](docs/user_screens_architecture.md). All new screens register in `kUserScreens[]` in `src/display/user_screens.cpp`. Do not add screen logic to `main.cpp`.

---

## Build and flash

```powershell
cd C:\Users\ian\projects\HomePuck
pio run -t upload -t monitor
```

Upload port: **COM5** (`platformio.ini`). OpenPlotter SSH: `pi@openplotter.local` (home LAN) or `pi@10.42.0.1` (on AP).

---

## Documentation index

| Doc | Purpose |
|-----|---------|
| [docs/overview.md](docs/overview.md) | Architecture and data flow |
| [docs/user_screens_architecture.md](docs/user_screens_architecture.md) | Multi-screen UI: registry, gestures, extension checklists |
| [docs/hardware.md](docs/hardware.md) | Board, pins, libraries |
| [docs/wifi_debug.md](docs/wifi_debug.md) | WiFi serial patterns |
| [docs/signalk_debug.md](docs/signalk_debug.md) | SignalK serial patterns, heap |
| [docs/adr/001-signalk-websocket-lifecycle.md](docs/adr/001-signalk-websocket-lifecycle.md) | WebSocket teardown on ESP32-C3 |

OpenRemote docs (true wind, Pi setup): `5.79-inch-E-ink-dashboard-with-LVGL-9/docs/openplotter_true_wind_setup.md`
