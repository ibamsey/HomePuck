#include "display_manager.h"
#include "LGFX_config.h"
#include "config.h"

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

    _userScreenActive = false;
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

bool DisplayManager::userScreenNeedsRedraw(const UserScreenDescriptor* desc, const WindData& wind, bool stale,
                                           DisplayConnectionState connState) {
    if (_forceRedraw) return true;
    if (_lastMode != _nav.mode) return true;
    if (_lastVariation != _nav.variation[_nav.mode]) return true;
    if (_lastConnState != connState) return true;

    if (desc && desc->needs_wind_data) {
        return userScreenDataChanged(_nav.mode, wind, stale, _nav.variation[_nav.mode]);
    }

    return false;
}

void DisplayManager::updateUserScreen(const WindData& wind, bool stale, DisplayConnectionState connState) {
    if (!_ready) return;

    const UserScreenDescriptor* desc = _nav.currentDescriptor();
    if (!desc) return;

    if (_userScreenActive && !userScreenNeedsRedraw(desc, wind, stale, connState)) {
        return;
    }

    _forceRedraw = false;

    UserScreenDrawContext ctx = {
        display,
        desc->needs_wind_data ? &wind : nullptr,
        stale,
        _nav.variation[desc->mode],
        millis()
    };

    desc->draw(ctx);
    drawConnectionBadge(connState);

    _userScreenActive = true;
    _lastMode = _nav.mode;
    _lastVariation = _nav.variation[_nav.mode];
    _lastConnState = connState;
}

void DisplayManager::navigateMode(int delta) {
    _nav.cycleMode(delta);
    _forceRedraw = true;
}

void DisplayManager::navigateVariation(int delta) {
    _nav.cycleVariation(delta);
    _forceRedraw = true;
}

const char* DisplayManager::getScreenDebugName() const {
    const UserScreenDescriptor* desc = _nav.currentDescriptor();
    return desc ? desc->debug_name : "?";
}
