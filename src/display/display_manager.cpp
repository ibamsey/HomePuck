#include "display_manager.h"
#include "LGFX_config.h"
#include "config.h"
#include <math.h>

DisplayManager displayManager;

static const uint16_t C_BG = 0x0000;
static const uint16_t C_GREEN = 0x07E0;
static const uint16_t C_WHITE = 0xFFFF;
static const uint16_t C_GREY = 0x8410;
static const uint16_t C_AMBER = 0xFD20;
static const uint16_t C_RED = 0xF800;
static const uint16_t C_CYAN = 0x07FF;

static LGFX display;

void DisplayManager::begin() {
    display.init();
    display.setRotation(0);
    display.setBrightness(255);
    display.setTextSize(2);
    _ready = true;
    showStatus(DISPLAY_CONN_BOOT, "Starting...");
}

void DisplayManager::drawHeader(const char* title, uint16_t color) {
    display.setTextColor(color, C_BG);
    display.setTextDatum(TC_DATUM);
    display.drawString(title, DISPLAY_WIDTH / 2, 18);
}

void DisplayManager::drawConnectionBadge(DisplayConnectionState connState) {
    const char* status = "OK";
    uint16_t status_color = C_GREEN;

    if (connState == DISPLAY_CONN_WIFI_LOST) {
        status = "WiFi";
        status_color = C_RED;
    } else if (connState == DISPLAY_CONN_SIGNALK_LOST || connState == DISPLAY_CONN_SIGNALK_CONNECTING) {
        status = connState == DISPLAY_CONN_SIGNALK_CONNECTING ? "SK..." : "No SK";
        status_color = C_AMBER;
    }

    display.setTextSize(1);
    display.setTextColor(status_color, C_BG);
    display.setTextDatum(TC_DATUM);
    display.drawString(status, DISPLAY_WIDTH / 2, 222);
    display.setTextSize(2);
}

void DisplayManager::showStatus(DisplayConnectionState state, const char* detail) {
    if (!_ready) return;

    _windScreenActive = false;
    display.fillScreen(C_BG);
    display.drawCircle(DISPLAY_WIDTH / 2, DISPLAY_HEIGHT / 2, 112, C_GREY);

    const char* title = "HomePuck";
    uint16_t color = C_CYAN;

    switch (state) {
        case DISPLAY_CONN_BOOT:
            title = "HomePuck";
            color = C_CYAN;
            break;
        case DISPLAY_CONN_WIFI_CONNECTING:
            title = "WiFi...";
            color = C_AMBER;
            break;
        case DISPLAY_CONN_WIFI_OK:
            title = "WiFi OK";
            color = C_GREEN;
            break;
        case DISPLAY_CONN_SIGNALK_CONNECTING:
            title = "SignalK...";
            color = C_AMBER;
            break;
        case DISPLAY_CONN_SIGNALK_OK:
            title = "SignalK OK";
            color = C_GREEN;
            break;
        case DISPLAY_CONN_SIGNALK_LOST:
            title = "No SignalK";
            color = C_RED;
            break;
        case DISPLAY_CONN_WIFI_LOST:
            title = "No WiFi";
            color = C_RED;
            break;
    }

    drawHeader(title, color);

    if (detail && detail[0]) {
        display.setTextColor(C_WHITE, C_BG);
        display.setTextSize(1);
        display.setTextDatum(TC_DATUM);
        display.drawString(detail, DISPLAY_WIDTH / 2, 120);
        display.setTextSize(2);
    }
}

bool DisplayManager::windDisplayChanged(const WindWidgetData& prev, const WindWidgetData& next,
                                        DisplayConnectionState prevConn, DisplayConnectionState nextConn) {
    if (prevConn != nextConn) return true;
    if (prev.show_tws != next.show_tws) return true;
    if (prev.awa_valid != next.awa_valid || prev.twd_valid != next.twd_valid ||
        prev.speed_valid != next.speed_valid || prev.sog_valid != next.sog_valid) {
        return true;
    }
    if (prev.awa_valid && fabsf(prev.awa_degrees - next.awa_degrees) > 0.5f) return true;
    if (prev.twd_valid && fabsf(prev.twd_degrees - next.twd_degrees) > 0.5f) return true;
    if (prev.speed_valid) {
        float prevSpeed = prev.show_tws ? prev.tws_knots : prev.aws_knots;
        float nextSpeed = next.show_tws ? next.tws_knots : next.aws_knots;
        if (fabsf(prevSpeed - nextSpeed) > 0.05f) return true;
    }
    if (next.sog_valid) {
        if (!prev.sog_valid || fabsf(prev.sog_knots - next.sog_knots) > 0.05f) return true;
        if (_windWidget.sogBarNeedsRedraw(next.sog_knots, next.sog_valid)) return true;
    }
    return false;
}

void DisplayManager::showWindInstrument(const WindData& wind, bool stale, DisplayConnectionState connState) {
    if (!_ready) return;

    WindWidgetData next = _windDisplay;
    next.awa_degrees = wind.awa_degrees;
    next.twd_degrees = wind.twdDialDegrees();
    next.aws_knots = wind.aws_knots;
    next.tws_knots = wind.tws_knots;
    next.sog_knots = wind.sog_knots;
    next.awa_valid = wind.awa_valid && !stale;
    next.twd_valid = wind.twd_valid && !stale;
    next.speed_valid = (next.show_tws ? wind.tws_valid : wind.aws_valid) && !stale;
    next.sog_valid = wind.sog_valid && !stale;
    next.now_ms = millis();

    if (_windScreenActive && !_forceWindRedraw && !windDisplayChanged(_windDisplay, next, _lastConnState, connState)) {
        return;
    }

    _forceWindRedraw = false;

    _windDisplay = next;
    _lastConnState = connState;
    _windScreenActive = true;

    _windWidget.draw(display, _windDisplay);
    drawConnectionBadge(connState);
}

void DisplayManager::toggleWindMode() {
    _windWidget.toggleMode(_windDisplay);
    _forceWindRedraw = true;
}

