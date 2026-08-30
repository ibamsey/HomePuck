# WiFi Serial Debug Guide

HomePuck uses the OpenRemote WiFi manager with verbose serial logging enabled via `DEBUG_WIFI=1` in `include/config.h`.

## Monitor

```powershell
cd C:\Users\ian\projects\HomePuck
pio device monitor -b 115200
```

Or build, flash, and monitor together:

```powershell
pio run -t upload -t monitor
```

## Log format

All WiFi debug lines use this prefix:

```
[WiFi][00001234] EVENT_NAME optional details...
```

The number is milliseconds since WiFi manager boot.

## Healthy connection sequence

```
[WiFi] ========== HomePuck WiFi Manager ==========
[WiFi] SSID=OpenPlotter hostname=HomePuck
[WiFi] MAC=84:fc:e6:b4:03:d8 connect_timeout=15000ms max_retries=5
[WiFi][00000001] INIT_START Configuring WiFi...
[WiFi][00001502] INIT_COMPLETE WiFi configured
[WiFi][00001503] SCAN_START Scanning for networks...
[WiFi][00001850] SCAN_DONE networks=3 duration=347ms
[WiFi][00001851] SCAN_RESULT ssid=OpenPlotter rssi=-45 channel=6 bssid=...
[WiFi][00001852] TARGET_FOUND ssid=OpenPlotter rssi=-45 channel=6 bssid=...
[WiFi][00001853] CONNECT_START ssid=OpenPlotter attempt=1 reconnect=false
[WiFi][00001854] CONNECT_METHOD bssid_locked channel=6 bssid=...
[WiFi][00001900] EVENT=STA_CONNECTED
[WiFi][00001901] AP_INFO ssid=OpenPlotter channel=6 rssi=-45 authmode=3 bssid=...
[WiFi][00002100] EVENT=GOT_IP ip=10.42.0.123
[WiFi][00002150] CONNECT_SUCCESS duration=1100ms rssi=-45 ip=10.42.0.123 gateway=10.42.0.1
```

Expect `CONNECT_SUCCESS duration=~1000ms` within about 1 second of `CONNECT_START`.

## Key events when connection fails

| Log pattern | Likely cause |
|-------------|--------------|
| `SCAN_EMPTY` | No APs visible — out of range, antenna, or RF issue |
| `TARGET_MISSING ssid=OpenPlotter not in scan` | OpenPlotter AP not broadcasting or wrong SSID in `config.h` |
| `CONNECT_WAIT status=6 (DISCONNECTED)` repeating | Association failing; watch for following `STA_DISCONNECTED reason=` |
| `CONNECT_FAILED ... final_status=6 (DISCONNECTED)` | Timed out after 15 s without getting IP |
| `EVENT=STA_DISCONNECTED reason=2 (AUTH_EXPIRE)` | Stale AP association after ESP32 reboot — not a firmware bug |
| `STATE_DESYNC manager=CONNECTED wifi_status=6` | Zombie WiFi — stack thinks connected but is not |
| `GATEWAY_UNREACHABLE gateway=10.42.0.1` | Associated but cannot reach Pi (zombie WiFi) |
| `RECONNECT_EXHAUSTED` | Max retries hit; rescanning networks |
| Repeated `STA_DISCONNECTED` without AUTH_EXPIRE | Main loop may be blocking WiFi task — check for heavy work in `loop()` |

## AUTH_EXPIRE after reboot

When the ESP32 reboots (firmware upload, power cycle), the AP may hold a stale association for up to 300 seconds. Reconnection fails with `reason=2 (AUTH_EXPIRE)` until the AP clears it.

During development on OpenPlotter, clear immediately from the Pi:

```bash
ssh pi@openplotter.local "sudo wpa_cli -i wlan1 deauthenticate 84:FC:E6:B4:03:D8"
```

Replace the MAC with your puck's WiFi MAC if different (serial banner shows `MAC=`). OpenRemote e-ink unit uses `D8:3B:DA:42:7D:64`.

**Note:** WiFi can remain connected while SignalK fails (heap exhaustion). If wind data stops but `[WiFi] STATUS connected` continues, see [signalk_debug.md](signalk_debug.md).

## Periodic status (every 10 s when connected)

```
[WiFi][00060000] STATUS connected uptime=60s session=58s rssi=-45 ip=10.42.0.123 heap=91200
```

Steady-state heap on ESP32-C3 is **~80–95 KB** once the wind sprite is allocated (see [signalk_debug.md](signalk_debug.md)). Higher values appear briefly before the first wind-screen draw.

## Disabling debug

Set in `include/config.h`:

```c
#define DEBUG_WIFI 0
```

Rebuild and flash. Connection behaviour is unchanged; only serial output is suppressed.

## Related docs

- OpenRemote WiFi guide: `5.79-inch-E-ink-dashboard-with-LVGL-9/docs/wifi_client_guide.md`
- OpenRemote WiFi ADR: `5.79-inch-E-ink-dashboard-with-LVGL-9/docs/adr/001-wifi-connection-strategy.md`
- Hardware / network defaults: [hardware.md](hardware.md)
- SignalK (distinct from WiFi loss): [signalk_debug.md](signalk_debug.md)
