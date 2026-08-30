# HomePuck Overview

Round ESP32-C3 wind instrument for the OpenPlotter boat network.

## Architecture

```
OpenPlotter AP (10.42.0.1)
        │
        ▼ WiFi (wifi_manager — OpenRemote port)
HomePuck (10.42.0.x)
        │
        ▼ WebSocket stream
SignalKClient (7 paths, WindData struct)
        │
        ▼ 500 ms timer (main loop)
DisplayManager → user screen registry → render modules (LovyanGFX)
        │
        ▼ CST816S swipe gestures
Screen modes (←→) and variations (↑↓)
```

## User screen navigation

| Gesture | Action |
|---------|--------|
| Swipe left | Next screen: Wind → Screen 2 → Screen 3 → Wind |
| Swipe right | Previous screen (reverse) |
| Swipe up | Next variation within current screen |
| Swipe down | Previous variation within current screen |

Wind screen variations: **AWS** (default) and **TWS** centre readout.

For adding screens or variations, see [user_screens_architecture.md](user_screens_architecture.md).

## Source layout

```
HomePuck/
├── include/           config.h, client/manager headers, user_screens.h
├── src/
│   ├── main.cpp       loop, touch gestures, WiFi/SignalK callbacks
│   ├── LGFX_config.h  GC9A01 driver
│   ├── wifi/          WiFi manager (shared lineage with OpenRemote)
│   ├── signalk/       WebSocket client + WindData
│   ├── display/       wind_widget, placeholder_screen, user_screens, display_manager
│   └── ui/            EXCLUDED from build — do not edit for features
├── docs/
└── platformio.ini     env esp32-2424s012, COM5
```

## Wind display

| Element | Source |
|---------|--------|
| AWS / TWS (centre, large) | Up/down swipe on wind screen; pills at bottom show mode |
| AWA pointer | White triangle |
| TWD pointer | Cyan triangle (+ white outline) |
| SOG bar | Under "kts"; cyan fill, amber peak marker (10 s decay) |
| Connection badge | Bottom centre: OK / SK... / No SK / WiFi |

Bow-up dial: 0° = top, clockwise positive. Scale arc 30°–330°.

## Connection states

| Display | Meaning |
|---------|---------|
| Full-screen "WiFi..." | WiFi connecting |
| Full-screen "No WiFi" | WiFi lost |
| Full-screen "SignalK..." | Initial SK connect only (user screen not yet shown) |
| User screen + badge "SK..." | SignalK reconnecting (WiFi OK) |
| User screen + badge "No SK" | SignalK lost |
| User screen + badge "OK" | Data flowing |

## Configuration

All tunables in `include/config.h`: WiFi SSID, SignalK host, pin defines, screen variation counts, heap floor (`SIGNALK_MIN_HEAP_BYTES`), debug flags.

## Related documentation

- [user_screens_architecture.md](user_screens_architecture.md) — screen registry, extension checklists
- [hardware.md](hardware.md) — pins, chip, firmware size, touch gestures
- [wifi_debug.md](wifi_debug.md) — WiFi serial log guide
- [signalk_debug.md](signalk_debug.md) — SignalK serial log guide
- [adr/001-signalk-websocket-lifecycle.md](adr/001-signalk-websocket-lifecycle.md) — reconnect memory policy
