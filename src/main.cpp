#include <Arduino.h>
#include <WiFi.h>
#include <CST816S.h>

#include "config.h"
#include "wifi_manager.h"
#include "signalk_client.h"
#include "display_manager.h"
#include "user_screens.h"

static CST816S touch(PIN_TP_SDA, PIN_TP_SCL, PIN_TP_RST, PIN_TP_INT);

static bool signalk_initialized = false;
static bool signalk_connect_pending = false;
static unsigned long signalk_connect_at_ms = 0;
static unsigned long last_display_update_ms = 0;
static unsigned long last_touch_ms = 0;
static unsigned long last_anchor_tap_ms = 0;
static unsigned long last_anchor_single_tap_ms = 0;

static DisplayConnectionState mapConnectionState() {
    if (!wifiManager.isConnected()) {
        return DISPLAY_CONN_WIFI_LOST;
    }
    if (!signalkClient.isConnected()) {
        SignalKState sk = signalkClient.getState();
        if (sk == SK_CONNECTING || sk == SK_RECONNECTING) {
            return DISPLAY_CONN_SIGNALK_CONNECTING;
        }
        return DISPLAY_CONN_SIGNALK_LOST;
    }
    return DISPLAY_CONN_SIGNALK_OK;
}

static void updateUserDisplay() {
    WindData wind = signalkClient.getWindData();
    EnergyData energy = signalkClient.getEnergyData();
    EnvironmentData environment = signalkClient.getEnvironmentData();
    AttitudeData attitude = signalkClient.getAttitudeData();
    PositionData position = signalkClient.getPositionData();
    DisplayConnectionState conn = mapConnectionState();
    bool wind_stale = signalkClient.isWindDataStale();
    bool energy_stale = signalkClient.isEnergyDataStale();
    bool environment_stale = signalkClient.isEnvironmentDataStale();
    bool attitude_stale = signalkClient.isAttitudeDataStale();
    bool position_stale = signalkClient.isPositionDataStale();
    displayManager.updateUserScreen(wind, wind_stale, energy, energy_stale,
                                    environment, environment_stale,
                                    attitude, attitude_stale,
                                    position, position_stale, conn);
}

static void onWiFiStateChange(WiFiConnectionState state, const char* message) {
    (void)message;

    switch (state) {
        case WIFI_STATE_CONNECTING:
        case WIFI_STATE_RECONNECTING:
            displayManager.showStatus(DISPLAY_CONN_WIFI_CONNECTING, WIFI_SSID);
            break;

        case WIFI_STATE_CONNECTED:
            Serial.println("\n[APP] WiFi connected");
            Serial.printf("  IP: %s\n", wifiManager.getIP().toString().c_str());
            displayManager.showStatus(DISPLAY_CONN_WIFI_OK, wifiManager.getIP().toString().c_str());

            if (signalk_initialized && !signalkClient.isConnected()) {
                signalk_connect_pending = true;
                signalk_connect_at_ms = millis() + 500;
            }
            break;

        case WIFI_STATE_DISCONNECTED:
        case WIFI_STATE_ERROR:
            displayManager.showStatus(DISPLAY_CONN_WIFI_LOST, "Reconnecting...");
            break;

        default:
            break;
    }
}

static void onSignalKStateChange(SignalKState state, const String& message) {
    (void)message;

    switch (state) {
        case SK_CONNECTING:
        case SK_RECONNECTING:
            if (!displayManager.isUserScreenActive()) {
                displayManager.showStatus(DISPLAY_CONN_SIGNALK_CONNECTING, SIGNALK_HOST);
            }
            break;

        case SK_CONNECTED:
            if (!displayManager.isUserScreenActive()) {
                displayManager.showStatus(DISPLAY_CONN_SIGNALK_OK, "Waiting for wind...");
            }
            break;

        case SK_DISCONNECTED:
        case SK_ERROR:
            if (wifiManager.isConnected() && !displayManager.isUserScreenActive()) {
                displayManager.showStatus(DISPLAY_CONN_SIGNALK_LOST, "Reconnecting...");
            }
            break;

        default:
            break;
    }
}

static void initAfterWiFiUp() {
    if (signalk_initialized) return;

    signalkClient.begin();
    signalkClient.onStateChange(onSignalKStateChange);
    signalk_initialized = true;

    displayManager.showStatus(DISPLAY_CONN_SIGNALK_CONNECTING, SIGNALK_HOST);
    signalk_connect_pending = true;
    signalk_connect_at_ms = millis() + 500;
}

static bool touch_ready = false;

static void initTouch() {
    touch.begin();
    touch.disable_auto_sleep();
    touch.enable_double_click();
    touch_ready = true;
    Serial.printf("[TOUCH] CST816S ready (I2C SDA=%d SCL=%d), fw %u.%u.%u\n",
                  PIN_TP_SDA, PIN_TP_SCL,
                  touch.data.versionInfo[0],
                  touch.data.versionInfo[1],
                  touch.data.versionInfo[2]);
}

static bool isAnchorTapPulse() {
    byte gesture = touch.data.gestureID;
    if (gesture == SINGLE_CLICK) {
        return true;
    }
    // CST816S often reports taps as finger-up with gesture NONE.
    if (gesture == NONE && touch.data.event == 1) {
        return true;
    }
    return false;
}

static bool isAnchorDoubleTapEvent(unsigned long now) {
    if (touch.data.gestureID == DOUBLE_CLICK) {
        last_anchor_single_tap_ms = 0;
        return true;
    }

    if (!isAnchorTapPulse()) {
        return false;
    }

    if (last_anchor_single_tap_ms != 0 &&
        (now - last_anchor_single_tap_ms) <= ANCHOR_DOUBLE_TAP_WINDOW_MS) {
        last_anchor_single_tap_ms = 0;
        return true;
    }

    last_anchor_single_tap_ms = now;
    return false;
}

static void handleAnchorTap(unsigned long now) {
    if (now - last_anchor_tap_ms < ANCHOR_TAP_DEBOUNCE_MS) {
        return;
    }

    const UserScreenDescriptor* desc = userScreenDescriptor(displayManager.getScreenMode());
    if (!desc || !desc->handles_tap) {
        return;
    }

    PositionData position = signalkClient.getPositionData();
    if (!position.valid) {
        Serial.println("[APP] Anchor tap ignored - no GPS fix");
        return;
    }

    if (displayManager.handleScreenTap(position)) {
        last_anchor_tap_ms = now;
        last_anchor_single_tap_ms = 0;
        last_touch_ms = now;
        updateUserDisplay();
        Serial.printf("[APP] Anchor double-tap zero lat=%.6f lon=%.6f (gesture=%u event=%u)\n",
                      position.latitude, position.longitude,
                      (unsigned)touch.data.gestureID, (unsigned)touch.data.event);
    }
}

static void handleTouch() {
    if (!touch_ready) return;
    if (!displayManager.isUserScreenActive()) return;
    if (!touch.available()) return;

    unsigned long now = millis();

    if (displayManager.getScreenMode() == USER_SCREEN_ANCHOR) {
        if (isAnchorDoubleTapEvent(now)) {
            handleAnchorTap(now);
        }
        return;
    }

    last_anchor_single_tap_ms = 0;

    if (now - last_touch_ms < TOUCH_GESTURE_DEBOUNCE_MS) return;

    int delta_mode = 0;
    int delta_variation = 0;

    switch (touch.data.gestureID) {
        case SWIPE_LEFT:
            delta_mode = 1;
            break;
        case SWIPE_RIGHT:
            delta_mode = -1;
            break;
        case SWIPE_UP:
            delta_variation = 1;
            break;
        case SWIPE_DOWN:
            delta_variation = -1;
            break;
        default:
            return;
    }

    last_touch_ms = now;

    if (delta_mode != 0) {
        displayManager.navigateMode(delta_mode);
    } else {
        displayManager.navigateVariation(delta_variation);
    }

    updateUserDisplay();
    Serial.printf("[APP] Gesture %s -> %s var %u\n",
                  touch.gesture().c_str(),
                  displayManager.getScreenDebugName(),
                  displayManager.getVariation());
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n[APP] HomePuck starting...");

    displayManager.begin();
    displayManager.showStatus(DISPLAY_CONN_BOOT, "WiFi starting...");

    initTouch();

    wifiManager.setConnectionCallback(onWiFiStateChange);
    wifiManager.begin();
}

void loop() {
    wifiManager.loop();
    handleTouch();

    if (wifiManager.isConnected() && !signalk_initialized) {
        initAfterWiFiUp();
    }

    if (signalk_initialized) {
        signalkClient.loop();

        if (signalk_connect_pending && millis() >= signalk_connect_at_ms) {
            signalk_connect_pending = false;
            if (wifiManager.isConnected() && !signalkClient.isConnected()) {
                signalkClient.connect();
            }
        }
    }

    unsigned long now = millis();
    bool wifi_up = wifiManager.isConnected();
    bool signalk_up = signalkClient.isConnected();

    if (wifi_up && (signalk_up || displayManager.isUserScreenActive()) &&
        (now - last_display_update_ms >= DISPLAY_UPDATE_INTERVAL_MS)) {
        last_display_update_ms = now;
        updateUserDisplay();
    }

    delay(10);
}
