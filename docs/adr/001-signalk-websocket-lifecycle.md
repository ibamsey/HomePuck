# ADR-001: SignalK WebSocket Lifecycle on ESP32-C3

**Status:** Accepted  
**Date:** 2026-08-30  
**Deciders:** Ian  

## Context

HomePuck shares architectural lineage with OpenRemote's SignalK client (same `esp_websocket_client`, reconnect state machine, zombie-WiFi detection). OpenRemote runs on **ESP32-S3 with PSRAM** (~8 MB flash, large headroom). HomePuck runs on **ESP32-C3 with no PSRAM** and a **~115 KB** LovyanGFX static sprite for the wind dial.

OpenRemote intentionally **abandons** the WebSocket handle on reconnect (`_ws_client = nullptr` without `destroy`) because `stop()`/`destroy()` can block indefinitely when the server is unreachable, triggering the watchdog. That trade-off is acceptable on S3 where reconnects are rare.

On HomePuck, the abandon pattern caused **monotonic heap loss** (~20–40 KB per reconnect cycle). After ~20–30 minutes of PONG-timeout reconnect storms, heap fell to ~9 KB, `esp_websocket_client_init` failed with `Memory exhausted`, and WiFi remained connected while the display showed no wind data.

## Decision

### 1. Destroy-only teardown from the main loop

`_releaseWebSocketClient()` runs only from `loop()` paths (`_handleReconnection`, `_checkConnectionHealth`, `connect`, `disconnect`):

```cpp
esp_websocket_client_handle_t client = _ws_client;
_ws_client = nullptr;
esp_websocket_client_destroy(client);  // no stop() — see below
```

**Do not** call `stop()` before `destroy()` on an already-closed client — this caused `multi_heap_free` assertion failures (double-free) during testing.

**Do not** destroy from `_handleWebSocketEvent` — the ESP-IDF WS task may still be running.

### 2. WebSocket callbacks stay minimal

On DISCONNECTED / ERROR / CLOSED: update state to `SK_RECONNECTING`, bump counters, set `_last_connection_attempt`. No teardown, no `delay()`, no subscription send.

Subscription is deferred via `_subscription_pending` and sent from `loop()`.

### 3. Heap floor with restart

Before `connect()` and on recovery paths, if `ESP.getFreeHeap() < SIGNALK_MIN_HEAP_BYTES` (25 000), log and `esp_restart()`.

Better a clean reboot than limping with a broken client that cannot allocate.

### 4. SK_ERROR is not terminal

After max reconnect attempts, reset backoff and stay in `SK_RECONNECTING` instead of permanent `SK_ERROR`. `_handleErrorRecovery()` transitions `SK_ERROR` back to `SK_RECONNECTING` after delay.

### 5. Do not revert to OpenRemote abandon pattern

Agents must not "align" HomePuck with OpenRemote's abandon-without-destroy reconnect code when fixing reconnect issues. The platforms have different memory budgets.

## Consequences

**Positive:**

- Stable heap (~90 KB) over multi-hour sessions
- Auto-recovery from ERROR and health-check timeouts
- Clear serial diagnostics (health log, heap on connect/release)

**Negative:**

- `destroy()` may block briefly on main loop — acceptable on boat LAN where gateway is reachable
- If destroy hangs when server is truly unreachable, watchdog remains a risk (same as OpenRemote's stop/block concern, but less frequent than leak-induced death)

## Verification

After changes, serial should show:

```
[SIGNALK] Subscription sent (7 paths, 523 bytes)
[SIGNALK] Health: data_age=... heap=91220 fails=0 disc=0 err=0
```

Stress test: `sudo systemctl restart signalk` on Pi repeatedly — heap should not trend downward across reconnects.

## References

- `src/signalk/signalk_client.cpp` — implementation
- `include/config.h` — `SIGNALK_MIN_HEAP_BYTES`, timeouts
- OpenRemote `src/signalk/signalk_client.cpp` — abandon pattern (S3 only)
- [signalk_debug.md](../signalk_debug.md) — operational guide
