# HomePuck Hardware

Hardware notes for the round display puck used in this project, as identified during bring-up on Ian's dev machine (August 2026).

## Board summary

| Item | Value |
|------|-------|
| **Board name** | ESP32-2424S012 (touch variant) |
| **Form factor** | 1.28″ round display module (“puck”) |
| **MCU** | Espressif **ESP32-C3** (QFN32, rev v0.4) |
| **Module** | ESP32-C3-MINI-1U class (on-board) |
| **Flash** | 4 MB embedded (XMC) |
| **Crystal** | 40 MHz |
| **Display** | 240×240 round IPS, **GC9A01** over SPI |
| **Touch** | Capacitive, **CST816S** (I2C address `0x15`) |
| **USB** | USB-C, native USB Serial/JTAG (no external UART chip) |
| **WiFi** | 2.4 GHz (used for OpenPlotter boat network) |

### Product references

- [AliExpress listing 1005006140751675](https://www.aliexpress.com/item/1005006140751675.html) — source unit for this project
- Similar commercial board: [CrowPanel ESP32-C3 1.28″ round touch](https://www.elecrow.com/crowpanel-esp32-display-1-28-r-inch-240-240-round-ips-display-capacitive-touch-spi-screen.html)
- Community docs: [HomeDing JCZN ESP32-2424S012](https://homeding.github.io/boards/esp32c3/jczn-esp32-2424s012.htm)

### Variant note

Shenzhen Jingcai (JCZN) sells two closely related modules:

| SKU suffix | Touch | Notes |
|------------|-------|-------|
| **2424S012** (or **2424S012C**) | Yes | CST816S/CST816D on I2C — **this project uses this variant** |
| **2424S012N** | No | Display only; touch code will not apply |

Touch was confirmed working on the development unit (tap toggles AWS/TWS on the wind display).

---

## USB and programming (dev machine)

Values below are from the Windows dev PC used for initial bring-up. COM port may change if other serial devices are connected.

| Item | Value |
|------|-------|
| **Serial port** | `COM5` (configured in `platformio.ini`) |
| **USB VID:PID** | `303A:1001` (Espressif USB JTAG/serial) |
| **WiFi MAC** | `84:FC:E6:B4:03:D8` |
| **Monitor / upload baud** | 115200 |
| **Boot mode** | USB CDC on boot (`ARDUINO_USB_CDC_ON_BOOT=1`) |

Flash and monitor:

```powershell
cd C:\Users\ian\projects\HomePuck
pio run -t upload -t monitor
```

Chip identity is reported by `esptool` on upload, for example:

```
Chip is ESP32-C3 (QFN32) (revision v0.4)
Features: WiFi, BLE, Embedded Flash 4MB (XMC)
USB mode: USB-Serial/JTAG
MAC: 84:fc:e6:b4:03:d8
```

---

## Pin map

Pin assignments match `include/config.h`, `src/LGFX_config.h`, and `platformio.ini` build flags.

### Display (GC9A01, SPI)

| Signal | GPIO | Notes |
|--------|------|-------|
| SCLK | **6** | SPI clock |
| MOSI | **7** | SPI data (board silk may label “SDA”) |
| DC | **2** | Data/command |
| CS | **10** | Chip select |
| BL | **3** | Backlight (PWM via LovyanGFX `Light_PWM`) |
| RST | — | Not routed (`-1` in driver; uses MCU reset) |
| MISO | — | Not connected |

**SPI host:** `SPI2_HOST`  
**Write clock:** 40 MHz  
**Panel:** 240×240, `invert = true`, not readable

### Touch (CST816S, I2C)

| Signal | GPIO | Notes |
|--------|------|-------|
| SDA | **4** | I2C data |
| SCL | **5** | I2C clock |
| INT | **0** | Touch interrupt (active on event) |
| RST | **1** | Touch controller reset |

**I2C address:** `0x15`  
**Library:** [fbiego/CST816S](https://github.com/fbiego/CST816S)

Firmware calls `touch.disable_auto_sleep()` after init so the controller stays responsive between taps.

Serial log on successful touch init:

```
[TOUCH] CST816S ready (I2C SDA=4 SCL=5), fw x.y.z
```

---

## Display coordinate system (wind instrument)

The round wind dial uses a **bow-up** convention (same as the OpenRemote e-ink dashboard):

- **0°** = bow (top of screen, 12 o'clock)
- Angles increase **clockwise** (starboard = positive)
- Scale arc: **30°–330°** (60° no-go zone at the bow)
- **AWA** pointer: large white triangle (`environment.wind.angleApparent`)
- **TWD** pointer: smaller cyan triangle with white outline (`environment.wind.directionTrue`, converted relative to heading when available)

**Touch:** tap anywhere on the live wind screen to toggle **AWS** / **TWS** speed readout. Mode pills at the bottom of the dial show the active selection.

---

## PlatformIO target

| Setting | Value |
|---------|-------|
| **Environment** | `esp32-2424s012` |
| **PlatformIO board** | `esp32-c3-devkitm-1` (closest match; custom pins via `build_flags`) |
| **Framework** | Arduino (ESP32 core ~2.0.x via platform 6.x) |

### Main libraries

| Library | Purpose |
|---------|---------|
| LovyanGFX | GC9A01 display driver |
| ArduinoJson | SignalK WebSocket delta parsing |
| CST816S | Capacitive touch |

### Typical firmware size (release build)

| Resource | Usage |
|----------|-------|
| **Flash** | ~76% (~1.0 MB of 1.25 MB app partition) |
| **RAM** | ~13% (~41 KB) |
| **Static sprite** | 240×240 RGB565 (~115 KB) for flicker-free dial background |

---

## Network role (HomePuck firmware)

This puck is configured as a **SignalK wind instrument client** on the boat WiFi AP:

| Setting | Default |
|---------|---------|
| WiFi SSID | `OpenPlotter` |
| SignalK host | `10.42.0.1:3000` |
| Hostname | `HomePuck` |

See `include/config.h` for credentials and timeouts. WiFi stack is ported from the OpenRemote e-ink dashboard project (`5.79-inch-E-ink-dashboard-with-LVGL-9` in the crowPanelWeather workspace).

---

## SignalK paths subscribed

| Path | Use |
|------|-----|
| `environment.wind.speedApparent` | AWS |
| `environment.wind.angleApparent` | AWA pointer |
| `environment.wind.speedOverGround` | TWS (primary on this boat) |
| `environment.wind.speedTrue` | TWS fallback |
| `environment.wind.directionTrue` | TWD pointer |
| `navigation.headingTrue` | Convert TWD to bow-relative dial angle |

TWD/TWS are produced on the Pi by SignalK **Derived Data** (`groundWind`), not on the ESP32.

---

## Files referencing hardware

| File | Contents |
|------|----------|
| `include/config.h` | Pin defines, display/touch/WiFi/SignalK constants |
| `src/LGFX_config.h` | LovyanGFX GC9A01 panel + SPI bus setup |
| `platformio.ini` | Board env, upload port, pin `build_flags` |
| `src/main.cpp` | CST816S touch init and tap handler |
| `src/wifi/wifi_manager.cpp` | WiFi connect/reconnect with serial debug (`DEBUG_WIFI`) |

See [wifi_debug.md](wifi_debug.md) for serial monitor patterns and troubleshooting.

---

## Bring-up history

1. Unit arrived with non-working or silent stock firmware over USB.
2. Identified as ESP32-C3 + GC9A01 on **COM5** via USB native serial.
3. Minimal LovyanGFX demo confirmed display.
4. Full HomePuck firmware added: WiFi, SignalK, wind dial, touch AWS/TWS toggle.
5. Touch confirmed working; CST816S requires `disable_auto_sleep()` for reliable taps.

---

*Last updated: 2026-08-30*
