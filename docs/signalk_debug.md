# SignalK Serial Debug Guide

HomePuck uses a simplified SignalK WebSocket client with verbose logging when `DEBUG_SIGNALK=1` in `include/config.h`.

## Monitor

```powershell
cd C:\Users\ian\projects\HomePuck
pio device monitor -b 115200
```

## Healthy connection sequence

```
[SIGNALK] Initializing HomePuck client...
[SIGNALK] Server: ws://10.42.0.1:3000/signalk/v1/stream
[SIGNALK] 7 paths subscribed
[SIGNALK] State: CONNECTING - Connecting...
[SIGNALK] Connecting to: ws://... (heap=205000)
[SIGNALK] WebSocket connected heap=198000
[SIGNALK] State: CONNECTED - Connected to SignalK server
[SIGNALK] Subscription sent (7 paths, 523 bytes)
[SIGNALK] Health: data_age=64ms heap=91220 fails=0 disc=0 err=0
```

Expect steady-state heap **~80–95 KB** after the wind sprite is allocated (first wind-screen draw). Connect-time heap is higher if the sprite has not yet been created. WiFi `STATUS` lines also report heap every 10 s.

## Distinguishing WiFi loss vs SignalK loss

| Symptom | WiFi | SignalK |
|---------|------|---------|
| `[WiFi] STATUS connected` continues | OK | — |
| `[WiFi] EVENT=STA_DISCONNECTED` | Lost | — |
| `[SIGNALK] DISCONNECT/ERROR/CLOSED` | Often OK | Reconnecting |
| Display badge "WiFi" (red) | Lost | — |
| Display badge "No SK" / "SK..." | Usually OK | Problem |
| Pi has no TCP from puck :3000 | Check WiFi first | SK client dead |

SignalK problems on a healthy WiFi link are usually **client-side heap exhaustion** from bad WebSocket teardown — see [ADR-001](adr/001-signalk-websocket-lifecycle.md).

## Key failure patterns

| Log pattern | Likely cause |
|-------------|--------------|
| `Memory exhausted` / `Client creation failed` | Heap too low — leaked WebSocket tasks; needs reboot or fix teardown |
| `Heap too low ... restarting` | Guard in `_checkHeapFloor()` — intentional recovery |
| `Health check failed #N: no data for X ms` | No deltas for 30 s — server stall or zombie socket |
| `no PONG received` | Ping timeout — often precedes reconnect |
| `Releasing client heap_before=... heap_after=...` | Normal reconnect teardown (destroy-only) |
| `Max attempts reached - resetting backoff` | Backoff reset; stays in RECONNECTING (not permanent ERROR) |
| `Recovering from SK_ERROR` | Auto-recovery from ERROR state |

## Periodic health log (every 60 s when connected)

```
[SIGNALK] Health: data_age=64ms heap=91220 fails=0 disc=0 err=0
```

- **data_age** — ms since last delta (should stay low when server is publishing)
- **fails** — health-check timeouts forcing reconnect
- **disc / err** — WebSocket disconnect and error event counts

## Pi-side verification

On OpenPlotter (WiFi OK but no wind on puck):

```bash
ssh pi@openplotter.local "curl -s http://localhost:3000/signalk/v1/api/vessels/self/environment/wind/speedApparent"
ssh pi@openplotter.local "ss -tn | grep 10.42.0"
```

REST should return fresh JSON. An established connection to `:3000` from the puck IP confirms the WebSocket is up.

## Disabling debug

```c
#define DEBUG_SIGNALK 0
```

in `include/config.h`. TWD periodic logs (`TWD=... deg`) are gated on this flag.

## Related docs

- [adr/001-signalk-websocket-lifecycle.md](adr/001-signalk-websocket-lifecycle.md)
- [wifi_debug.md](wifi_debug.md)
- OpenRemote true wind setup: `5.79-inch-E-ink-dashboard-with-LVGL-9/docs/openplotter_true_wind_setup.md`
