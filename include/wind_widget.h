#ifndef WIND_WIDGET_H
#define WIND_WIDGET_H

#include <Arduino.h>
#include <LGFX_config.h>
#include "user_screens.h"

#define WIND_WIDGET_SIZE 240
#define WIND_WIDGET_CENTER_X (WIND_WIDGET_SIZE / 2)
#define WIND_WIDGET_CENTER_Y (WIND_WIDGET_SIZE / 2)
#define WIND_WIDGET_OUTER_RADIUS 108
#define WIND_WIDGET_INNER_RADIUS 98
#define WIND_WIDGET_SCALE_START 30
#define WIND_WIDGET_SCALE_END 330

#define WIND_VAR_AWS 0
#define WIND_VAR_TWS 1

struct WindWidgetData {
    float awa_degrees = 0.0f;
    float twd_degrees = 0.0f;
    float aws_knots = 0.0f;
    float tws_knots = 0.0f;
    bool awa_valid = false;
    bool twd_valid = false;
    bool speed_valid = false;
    bool show_tws = false;
};

class WindWidget {
public:
    void draw(LGFX& display, DisplayUpdateKind kind, const WindWidgetData& data);
    void toggleMode(WindWidgetData& data);

private:
    void buildStatic(lgfx::LGFX_Sprite& sprite);
    void drawDynamicFull(LGFX& display, const WindWidgetData& data);
    void drawDynamicPartial(LGFX& display, const WindWidgetData& data);
    void drawScale(lgfx::LovyanGFX& gfx);
    void drawTick(lgfx::LovyanGFX& gfx, int16_t angle, bool major);
    void drawPointer(lgfx::LovyanGFX& gfx, float angle, int16_t width, uint16_t fill, uint16_t outline, bool outlineOnly);
    void drawBoat(lgfx::LovyanGFX& gfx);
    void drawCenter(lgfx::LovyanGFX& gfx, const WindWidgetData& data, bool restoreFromStatic = false);
    void drawModeIndicator(lgfx::LovyanGFX& gfx, bool show_tws);
    void drawNoGoLines(lgfx::LovyanGFX& gfx);
    void pointerBounds(float angle, int16_t width, bool large,
                       int16_t& minX, int16_t& minY, int16_t& maxX, int16_t& maxY) const;
    void patchPointerRegion(LGFX& display, float angle, int16_t width, bool large, bool valid);
    void resetCache(const WindWidgetData& data);

    static float normalizeAngle(float angle);
    static void ourAngleToPoint(int16_t cx, int16_t cy, int16_t radius, float ourAngle, int16_t& x, int16_t& y);

    bool _cacheValid = false;
    float _lastAwa = 0.0f;
    float _lastTwd = 0.0f;
    bool _lastAwaValid = false;
    bool _lastTwdValid = false;
    bool _lastSpeedValid = false;
    bool _lastShowTws = false;
    float _lastSpeed = 0.0f;
};

#endif // WIND_WIDGET_H
