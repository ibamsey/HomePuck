#ifndef WIND_WIDGET_H
#define WIND_WIDGET_H

#include <Arduino.h>
#include <LGFX_config.h>

// Puck-sized wind instrument (scaled from OpenRemote 272px widget)
#define WIND_WIDGET_SIZE 240
#define WIND_WIDGET_CENTER_X (WIND_WIDGET_SIZE / 2)
#define WIND_WIDGET_CENTER_Y (WIND_WIDGET_SIZE / 2)
#define WIND_WIDGET_OUTER_RADIUS 108
#define WIND_WIDGET_INNER_RADIUS 98
#define WIND_WIDGET_SCALE_START 30
#define WIND_WIDGET_SCALE_END 330

struct WindWidgetData {
    float awa_degrees = 0.0f;
    float twd_degrees = 0.0f;
    float aws_knots = 0.0f;
    float tws_knots = 0.0f;
    float sog_knots = 0.0f;
    bool awa_valid = false;
    bool twd_valid = false;
    bool speed_valid = false;
    bool sog_valid = false;
    bool show_tws = false;
    unsigned long now_ms = 0;
};

class WindWidget {
public:
    WindWidget() = default;
    ~WindWidget();
    void draw(LGFX& display, const WindWidgetData& data);
    void toggleMode(WindWidgetData& data);
    bool sogBarNeedsRedraw(float sog_knots, bool valid) const;

private:
    lgfx::LGFX_Sprite* _staticLayer = nullptr;
    bool _staticReady = false;

    void ensureStatic(LGFX& display);
    void drawDynamic(LGFX& display, const WindWidgetData& data);
    void drawScale(lgfx::LovyanGFX& gfx);
    void drawTick(lgfx::LovyanGFX& gfx, int16_t angle, bool major);
    void drawPointer(lgfx::LovyanGFX& gfx, float angle, int16_t width, uint16_t fill, uint16_t outline, bool outlineOnly);
    void drawBoat(lgfx::LovyanGFX& gfx);
    void drawCenter(lgfx::LovyanGFX& gfx, const WindWidgetData& data);
    void drawSogBar(lgfx::LovyanGFX& gfx, const WindWidgetData& data);
    void drawModeIndicator(lgfx::LovyanGFX& gfx, bool show_tws);
    void drawNoGoLines(lgfx::LovyanGFX& gfx);
    void updateSogPeak(float sog_knots, bool valid, unsigned long now_ms);

    float _sogPeakKnots = 0.0f;
    unsigned long _sogPeakLastMs = 0;

    static float normalizeAngle(float angle);
    static void ourAngleToPoint(int16_t cx, int16_t cy, int16_t radius, float ourAngle, int16_t& x, int16_t& y);
};

#endif // WIND_WIDGET_H
