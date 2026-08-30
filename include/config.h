#ifndef CONFIG_H
#define CONFIG_H

// ============================================================================
// WiFi Configuration (OpenPlotter boat network)
// ============================================================================

#define WIFI_SSID "OpenPlotter"
#define WIFI_PASSWORD "12345678"
#define WIFI_HOSTNAME "HomePuck"

#define WIFI_CONNECT_TIMEOUT_MS 15000
#define WIFI_RECONNECT_DELAY_MS 2000
#define WIFI_RECONNECT_MAX_DELAY_MS 30000
#define WIFI_RECONNECT_BACKOFF_MULTIPLIER 1.5f
#define WIFI_MAX_RECONNECT_ATTEMPTS 5
#define WIFI_STATUS_CHECK_INTERVAL_MS 10000

// ============================================================================
// SignalK Configuration
// ============================================================================

#define SIGNALK_HOST "10.42.0.1"
#define SIGNALK_PORT 3000
#define SIGNALK_PATH "/signalk/v1/stream"

#define SIGNALK_RECONNECT_DELAY_MS 2000
#define SIGNALK_RECONNECT_MAX_DELAY_MS 30000
#define SIGNALK_MAX_RECONNECT_ATTEMPTS 10

#define SIGNALK_SUBSCRIPTION_PERIOD_MS 500
#define SIGNALK_MIN_SUBSCRIPTION_PERIOD_MS 100
#define SIGNALK_DATA_STALE_THRESHOLD_MS 60000
#define SIGNALK_CONNECTION_TIMEOUT_MS 30000
#define SIGNALK_MIN_HEAP_BYTES 25000
#define SIGNALK_HEAP_LOG_INTERVAL_MS 60000

// ============================================================================
// Display Configuration (ESP32-2424S012 round GC9A01)
// ============================================================================

#define DISPLAY_WIDTH 240
#define DISPLAY_HEIGHT 240
#define DISPLAY_UPDATE_INTERVAL_MS 500

#define USER_SCREEN_WIND_VARIATIONS 2
#define USER_SCREEN_SOG_VARIATIONS 1
#define USER_SCREEN_PLACEHOLDER_VARIATIONS 2
#define TOUCH_GESTURE_DEBOUNCE_MS 300

#ifndef PIN_LCD_SCLK
#define PIN_LCD_SCLK 6
#define PIN_LCD_MOSI 7
#define PIN_LCD_DC 2
#define PIN_LCD_CS 10
#define PIN_LCD_BL 3
#endif

// Capacitive touch (CST816S on ESP32-2424S012C)
#define PIN_TP_SDA 4
#define PIN_TP_SCL 5
#define PIN_TP_RST 1
#define PIN_TP_INT 0

// ============================================================================
// Debug Configuration
// ============================================================================

#define DEBUG_WIFI 1
#define DEBUG_SIGNALK 1
#define DEBUG_DISPLAY 0

#endif // CONFIG_H
