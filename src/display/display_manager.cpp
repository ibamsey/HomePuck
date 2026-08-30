#include "display_manager.h"
#include "display_compositor.h"
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

LGFX& displayManagerPanel() {
    return display;
}

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

void DisplayManager::showStatus(DisplayConnectionState state, const char* detail) {
    if (!_ready) return;

    _userScreenActive = false;
    displayStaticLayer().invalidate();
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

DisplayUpdateKind DisplayManager::classifyUpdate(const UserScreenDescriptor* desc, const WindData& wind, bool wind_stale,
                                                 const EnergyData& energy, bool energy_stale,
                                                 DisplayConnectionState connState) {
    bool badgeVisible = connectionBadgeVisible(connState);
    bool needsBadge = (_lastConnState != connState) || (_lastBadgeVisible != badgeVisible);

    bool needsFull = !_userScreenActive || _forceRedraw ||
                     (_lastMode != _nav.mode) ||
                     (_lastVariation != _nav.variation[_nav.mode]);

    bool needsPartial = false;
    if (desc && (desc->needs_wind_data || desc->needs_energy_data)) {
        needsPartial = userScreenDataChanged(_nav.mode, wind, wind_stale, energy, energy_stale,
                                             _nav.variation[_nav.mode]);
    }

    if (!needsFull && !needsPartial && !needsBadge) {
        return DisplayUpdateKind::None;
    }
    if (!needsFull && !needsPartial && needsBadge) {
        return DisplayUpdateKind::BadgeOnly;
    }
    if (needsFull) {
        return DisplayUpdateKind::Full;
    }
    return DisplayUpdateKind::Partial;
}

void DisplayManager::updateUserScreen(const WindData& wind, bool wind_stale,
                                      const EnergyData& energy, bool energy_stale,
                                      DisplayConnectionState connState) {
    if (!_ready) return;

    const UserScreenDescriptor* desc = _nav.currentDescriptor();
    if (!desc) return;

    DisplayUpdateKind kind = classifyUpdate(desc, wind, wind_stale, energy, energy_stale, connState);
    if (kind == DisplayUpdateKind::None) {
        return;
    }

    bool badgeVisible = connectionBadgeVisible(connState);

    if (kind == DisplayUpdateKind::BadgeOnly) {
        displayCompositor().beginFrame(display);
        displayCompositor().drawConnectionBadge(display, connState, _lastBadgeVisible);
        displayCompositor().endFrame(display);
        _lastConnState = connState;
        _lastBadgeVisible = badgeVisible;
        return;
    }

    if (_lastMode != _nav.mode) {
        displayStaticLayer().invalidate();
    }

    _forceRedraw = false;

#if DEBUG_DISPLAY
    unsigned long frame_start = micros();
#endif
    displayCompositor().beginFrame(display);

    UserScreenDrawContext ctx = {
        display,
        kind,
        desc->needs_wind_data ? &wind : nullptr,
        wind_stale,
        desc->needs_energy_data ? &energy : nullptr,
        energy_stale,
        _nav.variation[desc->mode],
        millis()
    };

    desc->draw(ctx);
    displayCompositor().drawConnectionBadge(display, connState, _lastBadgeVisible);
    displayCompositor().endFrame(display);

#if DEBUG_DISPLAY
    Serial.printf("[Display] kind=%u screen=%s us=%lu\n",
                  (unsigned)kind, desc->debug_name, micros() - frame_start);
#endif

    _userScreenActive = true;
    _lastMode = _nav.mode;
    _lastVariation = _nav.variation[_nav.mode];
    _lastConnState = connState;
    _lastBadgeVisible = badgeVisible;
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
