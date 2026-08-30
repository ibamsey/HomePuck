#ifndef DISPLAY_COMPOSITOR_H
#define DISPLAY_COMPOSITOR_H

#include <Arduino.h>
#include "LGFX_config.h"
#include "user_screens.h"
#include "display_connection.h"

#define DISPLAY_BADGE_STRIP_Y 210
#define DISPLAY_BADGE_STRIP_H 30
#define DISPLAY_BADGE_TEXT_Y 222

bool connectionBadgeVisible(DisplayConnectionState connState);

class DisplayStaticLayer {
public:
    bool ensure(LGFX& parent, UserScreenMode owner);
    void invalidate();
    lgfx::LGFX_Sprite* sprite();
    bool isReadyFor(UserScreenMode mode) const;
    UserScreenMode owner() const { return _owner; }
    void setOwner(UserScreenMode mode);
    void markReady();
    void patchRect(LGFX& display, int16_t x, int16_t y, int16_t w, int16_t h);
    void pushFull(LGFX& display);

private:
    lgfx::LGFX_Sprite* _sprite = nullptr;
    UserScreenMode _owner = USER_SCREEN_COUNT;
    bool _ready = false;
};

// True when two axis-aligned rectangles overlap.
bool displayRectsOverlap(int16_t ax, int16_t ay, int16_t aw, int16_t ah,
                         int16_t bx, int16_t by, int16_t bw, int16_t bh);

DisplayStaticLayer& displayStaticLayer();

class DisplayCompositor {
public:
    void beginFrame(LGFX& display);
    void endFrame(LGFX& display);
    void clearBadgeStrip(LGFX& display);
    void drawConnectionBadge(LGFX& display, DisplayConnectionState connState, bool badgeWasVisible);
};

DisplayCompositor& displayCompositor();

#endif // DISPLAY_COMPOSITOR_H
