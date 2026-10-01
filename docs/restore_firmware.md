# Restore HomePuck firmware

Use this after flashing the ESP32-2424S012 puck with another project.

## Requirements

- [PlatformIO](https://platformio.org/) installed
- USB connected (native serial; Espressif `303A:1001`)
- Project checkout at the tagged release you want (see below)

## Flash from source

```powershell
cd C:\Users\ian\projects\HomePuck
pio run -t upload -t monitor
```

Default upload port is **COM5** in `platformio.ini`. If the port changed:

```powershell
pio device list
pio run -t upload --upload-port COMx
```

## Tagged releases

After `git fetch --tags`, restore a known snapshot:

```powershell
git checkout homepuck-2026-10-01
pio run -t upload
```

Return to latest development:

```powershell
git checkout main
```

## Verify device

On upload, esptool should report ESP32-C3, 4 MB flash, and MAC `84:fc:e6:b4:03:d8` (dev unit).

WiFi and SignalK settings are in `include/config.h` (OpenPlotter AP, `10.42.0.1:3000`).

## OpenPlotter stale WiFi association

After reboot, reconnect may fail with `AUTH_EXPIRE` until the AP clears the old station entry:

```bash
ssh pi@openplotter.local "sudo wpa_cli -i wlan1 deauthenticate 84:FC:E6:B4:03:D8"
```

See [hardware.md](hardware.md) and [AGENTS.md](../AGENTS.md).
