# User Screens Architecture

Canonical guide for extending HomePuck's multi-screen UI. Read this before adding screens or variations.

---

## Overview

HomePuck has two display layers:

1. **Status screens** — transient full-screen boot/WiFi/SignalK messages (`DisplayManager::showStatus()`). Gestures are disabled.
2. **User screens** — persistent instrument UI once first data renders. Swipe navigation applies here.

### Three-layer stack

```
Input (main.cpp)
    │ CST816S gesture → navigateMode / navigateVariation
    ▼
Navigation (user_screens.cpp)
    │ UserScreenNav state + kUserScreens[] registry
    ▼
Orchestration (display_manager.cpp)
    │ updateUserScreen(), dirty-check, connection badge
    ▼
Render (wind_widget.cpp, placeholder_screen.cpp, …)
    │ LovyanGFX draw per screen module
```

### Gesture model

| Gesture | Action |
|---------|--------|
| Swipe left | Next screen mode (cyclical) |
| Swipe right | Previous screen mode |
| Swipe up | Next variation within current screen |
| Swipe down | Previous variation within current screen |

Tap / single-click is ignored on user screens (prevents accidental mode changes).

Gestures are disabled until `DisplayManager::isUserScreenActive()` is true (same gate as the old wind-only screen).

---

## Module map

| File | Layer | Responsibility |
|------|-------|----------------|
| `src/main.cpp` | Input | CST816S init, gesture debounce, calls `navigateMode` / `navigateVariation` |
| `include/user_screens.h` | Navigation | `UserScreenMode` enum, `UserScreenNav`, descriptor types |
| `src/display/user_screens.cpp` | Navigation | `kUserScreens[]` registry, cycle helpers, draw wrappers |
| `include/display_manager.h` | Orchestration | Status vs user routing, dirty-check, badge overlay |
| `src/display/display_manager.cpp` | Orchestration | Registry dispatch via `updateUserScreen()` |
| `src/display/wind_widget.cpp` | Render | Wind dial, AWS/TWS pills |
| `src/display/sog_speedometer_screen.cpp` | Render | SOG dial |
| `src/display/energy_flow_screen.cpp` | Render | Energy hub diagram |
| `include/energy_data.h` | Data | SignalK path constants + `EnergyData` struct |
| `include/config.h` | Config | Per-screen variation counts, debounce interval |

**Never edit for UI features:** `src/ui/` (excluded from build — EEZ/LVGL artifacts).

---

## Screen registry

All user screens register in `kUserScreens[]` inside [`src/display/user_screens.cpp`](../src/display/user_screens.cpp). This is the **primary extension point**.

```cpp
struct UserScreenDrawContext {
    LGFX& display;
    const WindData* wind;   // nullptr when needs_wind_data == false
    bool wind_stale;
    uint8_t variation;      // opaque index for current screen
    unsigned long now_ms;
};

struct UserScreenDescriptor {
    UserScreenMode mode;
    const char* debug_name;
    uint8_t max_variations;
    bool needs_wind_data;
    UserScreenDrawFn draw;
};
```

`DisplayManager::updateUserScreen()` looks up the current descriptor and calls `desc->draw(ctx)`. It does **not** contain screen-specific layout logic.

Each screen module exposes a draw function (or class method). A thin `static void drawXxx(const UserScreenDrawContext& ctx)` wrapper in `user_screens.cpp` adapts the generic context to the module API.

---

## Variation model

Variations are **opaque `uint8_t` indices** (`0 … max_variations-1`) stored in `UserScreenNav::variation[USER_SCREEN_COUNT]`. Each screen interprets its own indices:

| Screen | Index | Constant | Meaning |
|--------|-------|----------|---------|
| Wind | 0 | `WIND_VAR_AWS` | AWS centre readout |
| Wind | 1 | `WIND_VAR_TWS` | TWS centre readout |
| Placeholder | 0 | `PLACEHOLDER_VAR_A` | Dummy label A |
| Placeholder | 1 | `PLACEHOLDER_VAR_B` | Dummy label B |

Variation indices **persist per screen** when swiping horizontally. Example: set wind to TWS (variation 1), swipe to Screen 2, swipe back — wind still shows TWS.

Define named constants in the screen's own header (`wind_widget.h`, `placeholder_screen.h`). Do **not** add screen-specific semantics to `UserScreenNav`.

---

## Dirty-check rules

`DisplayManager` skips redraw when nothing changed (WiFi-safe — reduces SPI traffic in the 500 ms loop).

Global checks (all screens):

- `_forceRedraw` — set on every navigation gesture
- `_lastMode != _nav.mode`
- `_lastVariation != _nav.variation[_nav.mode]`
- `_lastConnState != connState`

Screen-specific checks:

- Wind (`needs_wind_data == true`): `userScreenWindDataChanged()` in `user_screens.cpp` compares wind field deltas and SOG peak decay
- Placeholder: global checks suffice (static content)

Rule: **navigation always sets `_forceRedraw = true`**. The periodic loop relies on dirty-check otherwise.

---

## RAM constraints

ESP32-C3 has **no PSRAM**. Steady-state free heap is ~80–95 KB after connect.

| Allocation | Size | When |
|------------|------|------|
| Wind static sprite | ~115 KB | `WindWidget::ensureStatic()` — one instance only |
| Placeholder screens | 0 extra | Direct draw to framebuffer |
| New screen with sprite | ~115 KB+ | Avoid unless replacing wind screen; prefer direct draw |

Before adding a large sprite, check `ESP.getFreeHeap()` after connect. See [AGENTS.md](../AGENTS.md) RAM budget table.

---

## Screen inventory

Maintain this table when adding or replacing screens:

| Mode | Debug name | Variations | Needs wind | Render module | Status |
|------|------------|------------|------------|---------------|--------|
| `USER_SCREEN_WIND` | Wind | 2 (AWS/TWS) | yes | `wind_widget.cpp` | live |
| `USER_SCREEN_SOG` | SOG | 1 | yes | `sog_speedometer_screen.cpp` | live |
| `USER_SCREEN_ENERGY` | Energy | 1 | energy | `energy_flow_screen.cpp` | live |

---

## Checklist: add a new screen mode

1. Add `USER_SCREEN_XXX` to `UserScreenMode` enum in `include/user_screens.h` (before `USER_SCREEN_COUNT`)
2. Create `include/xxx_screen.h` + `src/display/xxx_screen.cpp` with draw function or class
3. Define variation count in `include/config.h` (e.g. `#define USER_SCREEN_XXX_VARIATIONS 1`)
4. Add named variation constants in the screen header if semantics are non-trivial
5. Implement `static void drawXxx(const UserScreenDrawContext& ctx)` wrapper in `user_screens.cpp`
6. Add row to `kUserScreens[]` with correct `max_variations`, `needs_wind_data`, and `draw` fn
7. Implement dirty-check helper if the screen has changing data (or rely on global checks for static screens)
8. Wire SignalK data in the draw wrapper if `needs_wind_data == true` (read from `ctx.wind`)
9. Build, flash, verify horizontal swipe reaches new screen and connection badge overlays correctly
10. Update the **Screen inventory** table in this document

---

## Checklist: add a variation to an existing screen

1. Increment `USER_SCREEN_XXX_VARIATIONS` in `include/config.h`
2. Add named constant for the new variation index in the screen header
3. Handle the new index in the screen's `draw()` method
4. Registry row `max_variations` picks up the config define automatically — no nav code changes
5. Test up/down swipe wraps correctly; verify index persists after swiping away and back horizontally

---

## Energy flow screen (screen 3)

Home Assistant [Power Flow Card Plus](https://github.com/flixlix/power-flow-card-plus) inspired layout: **battery hub** centre, solar sources upper corners, loads lower corners. Animated flow lines show watts; line weight scales with power.

SignalK paths (defined in `include/energy_data.h`):

| Source | Path | Field |
|--------|------|-------|
| Victron A | `electrical.solar.0.power` | Fixed solar (W) |
| Victron A | `electrical.consumption.0.power` | Boat load (W) |
| Victron B | `electrical.solar.1.power` | Portable solar (W) |
| Victron B | `electrical.consumption.1.power` | Fridge load (W) |
| Ecoworthy BMS | `electrical.batteries.0.power` | Battery ±W (+ charge) |
| Ecoworthy BMS | `electrical.batteries.0.capacity.stateOfCharge` | SOC (0–1 or %) |

When no energy data is received, `ENERGY_FLOW_DEMO_WHEN_STALE` in `config.h` enables an animated demo labelled **DEMO**.

---

Goal: swap `USER_SCREEN_PLACEHOLDER_A` for a screen showing SOG max/avg.

1. Create `include/sog_stats_screen.h` and `src/display/sog_stats_screen.cpp`
2. Add `#define USER_SCREEN_SOG_STATS_VARIATIONS 2` to `config.h` (e.g. current vs session max)
3. Define `SOG_VAR_CURRENT = 0`, `SOG_VAR_SESSION = 1` in the header
4. Implement `sogStatsScreenDraw(LGFX&, uint8_t variation, const WindData& wind)` using `wind.sog_knots`
5. Add `static void drawSogStats(const UserScreenDrawContext& ctx)` in `user_screens.cpp`
6. Replace the placeholder A row in `kUserScreens[]`:
   ```cpp
   { USER_SCREEN_PLACEHOLDER_A, "SOG", USER_SCREEN_SOG_STATS_VARIATIONS, true, drawSogStats },
   ```
   Or rename the enum to `USER_SCREEN_SOG_STATS` if removing the placeholder entirely
7. Add `userScreenSogStatsChanged()` if data updates frequently; otherwise global dirty-check may suffice
8. Flash, swipe L from wind to reach SOG screen, verify badge and variations
9. Update screen inventory table above

Touch handler and `UserScreenNav` cycle logic require **no changes**.

---

## Anti-patterns

| Anti-pattern | Why it breaks |
|--------------|---------------|
| Screen draw logic in `main.cpp` | Violates layer separation; untestable, grows loop() |
| Per-screen tap handlers in `main.cpp` | Navigation belongs in `UserScreenNav`; gestures are global |
| New enum value without registry row | Screen unreachable; cycle may hit undefined draw |
| Wind-specific logic in `UserScreenNav` | Variation semantics belong in render modules |
| Monolithic `switch` in `display_manager.cpp` | Use registry; keeps orchestration screen-agnostic |
| LVGL / `src/ui/` edits | Excluded from build; regenerated by EEZ |
| Extra 115 KB sprite without heap check | OOM on ESP32-C3; may break WiFi/SignalK |
| Full-screen status on SK reconnect | User screens stay visible; badge only (see AGENTS.md) |

---

## Related documentation

- [overview.md](overview.md) — high-level architecture and data flow
- [hardware.md](hardware.md) — CST816S gestures and pins
- [AGENTS.md](../AGENTS.md) — agent quick reference and RAM rules
