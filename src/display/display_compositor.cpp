#include "display_compositor.h"
#include "config.h"

static const uint16_t C_BG = 0x0000;
static const uint16_t C_AMBER = 0xFD20;
static const uint16_t C_RED = 0xF800;

static DisplayStaticLayer s_staticLayer;
static DisplayCompositor s_compositor;

bool connectionBadgeVisible(DisplayConnectionState connState) {
    return connState == DISPLAY_CONN_WIFI_LOST ||
           connState == DISPLAY_CONN_SIGNALK_LOST ||
           connState == DISPLAY_CONN_SIGNALK_CONNECTING;
}

DisplayStaticLayer& displayStaticLayer() {
    return s_staticLayer;
}

DisplayCompositor& displayCompositor() {
    return s_compositor;
}

bool displayRectsOverlap(int16_t ax, int16_t ay, int16_t aw, int16_t ah,
                         int16_t bx, int16_t by, int16_t bw, int16_t bh) {
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

bool DisplayStaticLayer::ensure(LGFX& parent, UserScreenMode owner) {
    if (_owner != owner) {
        _ready = false;
        _owner = owner;
    }

    if (_sprite == nullptr) {
        _sprite = new lgfx::LGFX_Sprite(&parent);
    }
    if (_sprite->getBuffer() == nullptr) {
        if (!_sprite->createSprite(DISPLAY_WIDTH, DISPLAY_HEIGHT)) {
            return false;
        }
    }
    return true;
}

void DisplayStaticLayer::invalidate() {
    _ready = false;
    _owner = USER_SCREEN_COUNT;
}

lgfx::LGFX_Sprite* DisplayStaticLayer::sprite() {
    return _sprite;
}

bool DisplayStaticLayer::isReadyFor(UserScreenMode mode) const {
    return _ready && _owner == mode && _sprite != nullptr && _sprite->getBuffer() != nullptr;
}

void DisplayStaticLayer::setOwner(UserScreenMode mode) {
    if (_owner != mode) {
        _ready = false;
        _owner = mode;
    }
}

void DisplayStaticLayer::markReady() {
    _ready = true;
}

void DisplayStaticLayer::patchRect(LGFX& display, int16_t x, int16_t y, int16_t w, int16_t h) {
    if (!_ready || _sprite == nullptr || _sprite->getBuffer() == nullptr) return;

    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > DISPLAY_WIDTH) w = DISPLAY_WIDTH - x;
    if (y + h > DISPLAY_HEIGHT) h = DISPLAY_HEIGHT - y;
    if (w <= 0 || h <= 0) return;

    // Blit row-by-row with correct sprite stride (single pushImage corrupts multi-row rects).
    const int32_t sprite_w = _sprite->width();
    uint16_t* buf = (uint16_t*)_sprite->getBuffer();
    for (int16_t row = 0; row < h; row++) {
        display.pushImage(x, y + row, w, 1, buf + (y + row) * sprite_w + x);
    }
}

void DisplayStaticLayer::pushFull(LGFX& display) {
    if (!_ready || _sprite == nullptr) return;
    _sprite->pushSprite(&display, 0, 0);
}

void DisplayCompositor::beginFrame(LGFX& display) {
    display.startWrite();
}

void DisplayCompositor::endFrame(LGFX& display) {
    display.endWrite();
}

void DisplayCompositor::clearBadgeStrip(LGFX& display) {
    display.fillRect(0, DISPLAY_BADGE_STRIP_Y, DISPLAY_WIDTH, DISPLAY_BADGE_STRIP_H, C_BG);
}

void DisplayCompositor::drawConnectionBadge(LGFX& display, DisplayConnectionState connState,
                                            bool badgeWasVisible) {
    if (connectionBadgeVisible(connState)) {
        clearBadgeStrip(display);

        const char* status = "No SK";
        uint16_t status_color = C_AMBER;

        if (connState == DISPLAY_CONN_WIFI_LOST) {
            status = "WiFi";
            status_color = C_RED;
        } else if (connState == DISPLAY_CONN_SIGNALK_CONNECTING) {
            status = "SK...";
        }

        display.setTextSize(1);
        display.setTextColor(status_color, C_BG);
        display.setTextDatum(TC_DATUM);
        display.drawString(status, DISPLAY_WIDTH / 2, DISPLAY_BADGE_TEXT_Y);
        display.setTextSize(2);
        return;
    }

    // Healthy state: leave screen pixels alone. Restore once when clearing a badge overlay.
    if (badgeWasVisible) {
        displayStaticLayer().patchRect(display, 0, DISPLAY_BADGE_STRIP_Y,
                                       DISPLAY_WIDTH, DISPLAY_BADGE_STRIP_H);
    }
}
