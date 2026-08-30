#include "sog_speedometer_screen.h"
#include "display_compositor.h"
#include "config.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const uint16_t C_BG = 0x0000;
static const uint16_t C_WHITE = 0xFFFF;
static const uint16_t C_GREY = 0x8410;
static const uint16_t C_DIM = 0x4208;
static const uint16_t C_CYAN = 0x07FF;
static const uint16_t C_GREEN = 0x07E0;
static const uint16_t C_YELLOW = 0xFFE0;
static const uint16_t C_AMBER = 0xFD20;
static const uint16_t C_RED = 0xF800;

static const int16_t CX = DISPLAY_WIDTH / 2;
static const int16_t CY = 128;
static const int16_t OUTER_R = 98;
static const int16_t INNER_R = 78;
static const int16_t NEEDLE_INNER_R = 24;
static const float ARC_START_DEG = 225.0f;
static const float ARC_SWEEP_DEG = 270.0f;
static const int16_t PATCH_MARGIN = 4;

static float s_peakKnots = 0.0f;
static unsigned long s_peakLastMs = 0;
static float s_lastSog = -1.0f;
static float s_lastPeak = -1.0f;
static bool s_lastValid = false;
static float s_lastNeedleAngle = -1.0f;
static float s_lastPeakAngle = -1.0f;
static bool s_cacheValid = false;

static float normalizeAngle(float angle) {
    while (angle < 0.0f) angle += 360.0f;
    while (angle >= 360.0f) angle -= 360.0f;
    return angle;
}

static void ourAngleToPoint(int16_t cx, int16_t cy, int16_t radius, float ourAngle, int16_t& x, int16_t& y) {
    float rad = (90.0f - ourAngle) * (float)M_PI / 180.0f;
    x = cx + (int16_t)(cosf(rad) * radius);
    y = cy - (int16_t)(sinf(rad) * radius);
}

static float speedToAngle(float kts) {
    float t = kts / SOG_SPEEDO_MAX_KTS;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return normalizeAngle(ARC_START_DEG + t * ARC_SWEEP_DEG);
}

static uint16_t speedZoneColor(float kts) {
    if (kts < 5.0f) return C_GREEN;
    if (kts < 10.0f) return C_YELLOW;
    if (kts < 12.5f) return C_AMBER;
    return C_RED;
}

static void drawArcBand(lgfx::LovyanGFX& gfx, float startKts, float endKts, uint16_t color) {
    float startAngle = speedToAngle(startKts);
    int steps = (int)((endKts - startKts) * 8.0f);
    if (steps < 4) steps = 4;

    float prevRad = (90.0f - startAngle) * (float)M_PI / 180.0f;

    for (int i = 1; i <= steps; i++) {
        float kts = startKts + (endKts - startKts) * ((float)i / (float)steps);
        float angle = speedToAngle(kts);
        float rad = (90.0f - angle) * (float)M_PI / 180.0f;

        int16_t xo0 = CX + (int16_t)(cosf(prevRad) * OUTER_R);
        int16_t yo0 = CY - (int16_t)(sinf(prevRad) * OUTER_R);
        int16_t xo1 = CX + (int16_t)(cosf(rad) * OUTER_R);
        int16_t yo1 = CY - (int16_t)(sinf(rad) * OUTER_R);
        gfx.drawLine(xo0, yo0, xo1, yo1, color);

        int16_t xi0 = CX + (int16_t)(cosf(prevRad) * INNER_R);
        int16_t yi0 = CY - (int16_t)(sinf(prevRad) * INNER_R);
        int16_t xi1 = CX + (int16_t)(cosf(rad) * INNER_R);
        int16_t yi1 = CY - (int16_t)(sinf(rad) * INNER_R);
        gfx.drawLine(xi0, yi0, xi1, yi1, color);

        prevRad = rad;
    }
}

static void drawTick(lgfx::LovyanGFX& gfx, float kts, bool major) {
    float angle = speedToAngle(kts);
    int16_t x0, y0, x1, y1;
    ourAngleToPoint(CX, CY, OUTER_R + 2, angle, x0, y0);
    ourAngleToPoint(CX, CY, OUTER_R - (major ? 14 : 8), angle, x1, y1);
    gfx.drawLine(x0, y0, x1, y1, major ? C_WHITE : C_DIM);

    if (major) {
        int16_t lx, ly;
        ourAngleToPoint(CX, CY, INNER_R - 16, angle, lx, ly);
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", (int)kts);
        gfx.setTextSize(1);
        gfx.setTextColor(C_WHITE, C_BG);
        gfx.setTextDatum(MC_DATUM);
        gfx.drawString(buf, lx, ly);
    }
}

static void updatePeak(float sog_knots, bool valid, unsigned long now_ms) {
    if (!valid) return;

    if (s_peakLastMs == 0) {
        s_peakLastMs = now_ms;
        s_peakKnots = sog_knots;
        return;
    }

    float dt_sec = (float)(now_ms - s_peakLastMs) / 1000.0f;
    s_peakLastMs = now_ms;
    if (dt_sec <= 0.0f) return;

    if (sog_knots >= s_peakKnots) {
        s_peakKnots = sog_knots;
    } else {
        float alpha = 1.0f - expf(-dt_sec / SOG_SPEEDO_PEAK_TAU_SEC);
        s_peakKnots += (sog_knots - s_peakKnots) * alpha;
    }
}

static void needleBounds(float angle, int16_t& minX, int16_t& minY, int16_t& maxX, int16_t& maxY) {
    angle = normalizeAngle(angle);
    int16_t tipX, tipY;
    ourAngleToPoint(CX, CY, OUTER_R - 6, angle, tipX, tipY);

    float rad = (90.0f - angle) * (float)M_PI / 180.0f;
    const int16_t width = 8;
    float halfWidthAngle = atanf((float)width / 2.0f / (float)NEEDLE_INNER_R);

    int16_t blX = CX + (int16_t)(cosf(rad + halfWidthAngle) * NEEDLE_INNER_R);
    int16_t blY = CY - (int16_t)(sinf(rad + halfWidthAngle) * NEEDLE_INNER_R);
    int16_t brX = CX + (int16_t)(cosf(rad - halfWidthAngle) * NEEDLE_INNER_R);
    int16_t brY = CY - (int16_t)(sinf(rad - halfWidthAngle) * NEEDLE_INNER_R);

    minX = tipX;
    maxX = tipX;
    minY = tipY;
    maxY = tipY;

    int16_t xs[] = {tipX, blX, brX, CX};
    int16_t ys[] = {tipY, blY, brY, CY};
    for (int i = 0; i < 4; i++) {
        if (xs[i] < minX) minX = xs[i];
        if (xs[i] > maxX) maxX = xs[i];
        if (ys[i] < minY) minY = ys[i];
        if (ys[i] > maxY) maxY = ys[i];
    }

    minX -= PATCH_MARGIN;
    minY -= PATCH_MARGIN;
    maxX += PATCH_MARGIN;
    maxY += PATCH_MARGIN;
}

static void peakBounds(float peakKts, int16_t& minX, int16_t& minY, int16_t& maxX, int16_t& maxY) {
    float angle = speedToAngle(peakKts);
    int16_t x0, y0, x1, y1, mx, my;
    ourAngleToPoint(CX, CY, INNER_R - 4, angle, x0, y0);
    ourAngleToPoint(CX, CY, OUTER_R + 4, angle, x1, y1);
    ourAngleToPoint(CX, CY, OUTER_R + 8, angle, mx, my);

    minX = x0;
    maxX = x0;
    minY = y0;
    maxY = y0;

    int16_t xs[] = {x0, x1, (int16_t)(mx - 3), (int16_t)(mx + 3)};
    int16_t ys[] = {y0, y1, (int16_t)(my - 3), (int16_t)(my + 3)};
    for (int i = 0; i < 4; i++) {
        if (xs[i] < minX) minX = xs[i];
        if (xs[i] > maxX) maxX = xs[i];
        if (ys[i] < minY) minY = ys[i];
        if (ys[i] > maxY) maxY = ys[i];
    }

    minX -= PATCH_MARGIN;
    minY -= PATCH_MARGIN;
    maxX += PATCH_MARGIN;
    maxY += PATCH_MARGIN;
}

static void drawNeedle(lgfx::LovyanGFX& gfx, float angle, int16_t innerR, int16_t outerR, uint16_t color, int16_t width) {
    angle = normalizeAngle(angle);
    int16_t tipX, tipY;
    ourAngleToPoint(CX, CY, outerR, angle, tipX, tipY);

    float rad = (90.0f - angle) * (float)M_PI / 180.0f;
    float halfWidthAngle = atanf((float)width / 2.0f / (float)innerR);

    int16_t blX = CX + (int16_t)(cosf(rad + halfWidthAngle) * innerR);
    int16_t blY = CY - (int16_t)(sinf(rad + halfWidthAngle) * innerR);
    int16_t brX = CX + (int16_t)(cosf(rad - halfWidthAngle) * innerR);
    int16_t brY = CY - (int16_t)(sinf(rad - halfWidthAngle) * innerR);

    gfx.fillTriangle(tipX, tipY, blX, blY, brX, brY, color);
}

static void drawPeakMarker(lgfx::LovyanGFX& gfx, float peakKts) {
    if (peakKts < 0.05f) return;

    float angle = speedToAngle(peakKts);
    int16_t x0, y0, x1, y1;
    ourAngleToPoint(CX, CY, INNER_R - 4, angle, x0, y0);
    ourAngleToPoint(CX, CY, OUTER_R + 4, angle, x1, y1);
    gfx.drawLine(x0, y0, x1, y1, C_AMBER);
    gfx.drawLine(x0 + 1, y0, x1 + 1, y1, C_AMBER);

    int16_t mx, my;
    ourAngleToPoint(CX, CY, OUTER_R + 8, angle, mx, my);
    gfx.fillCircle(mx, my, 3, C_AMBER);
}

static void buildStaticLayer(lgfx::LGFX_Sprite& sprite) {
    sprite.fillScreen(C_BG);
    sprite.drawCircle(CX, CY, OUTER_R + 6, C_GREY);

    drawArcBand(sprite, 0.0f, 5.0f, C_GREEN);
    drawArcBand(sprite, 5.0f, 10.0f, C_YELLOW);
    drawArcBand(sprite, 10.0f, 12.5f, C_AMBER);
    drawArcBand(sprite, 12.5f, SOG_SPEEDO_MAX_KTS, C_RED);

    for (int kts = 0; kts <= 15; kts += 3) {
        drawTick(sprite, (float)kts, true);
    }

    sprite.setTextSize(1);
    sprite.setTextColor(C_CYAN, C_BG);
    sprite.setTextDatum(TC_DATUM);
    sprite.drawString("SOG", CX, 18);
    sprite.drawString("kts", CX, 198);
}

static void drawCenterDynamic(lgfx::LovyanGFX& gfx, bool valid, float sog) {
    gfx.fillRect(CX - 50, CY - 10, 100, 52, C_BG);

    if (valid) {
        float angle = speedToAngle(sog);
        drawNeedle(gfx, angle, NEEDLE_INNER_R, OUTER_R - 6, C_WHITE, 8);
        gfx.fillCircle(CX, CY, 6, C_CYAN);
        gfx.fillCircle(CX, CY, 3, C_BG);
    } else {
        gfx.drawCircle(CX, CY, 6, C_GREY);
    }

    char speedBuf[16];
    if (valid) {
        snprintf(speedBuf, sizeof(speedBuf), "%.1f", sog);
    } else {
        snprintf(speedBuf, sizeof(speedBuf), "--.-");
    }

    gfx.setTextSize(3);
    gfx.setTextColor(valid ? C_WHITE : C_GREY, C_BG);
    gfx.setTextDatum(MC_DATUM);
    gfx.drawString(speedBuf, CX, CY + 8);

    if (valid && s_peakKnots > sog + 0.05f) {
        char peakBuf[16];
        snprintf(peakBuf, sizeof(peakBuf), "max %.1f", s_peakKnots);
        gfx.setTextSize(1);
        gfx.setTextColor(C_AMBER, C_BG);
        gfx.drawString(peakBuf, CX, CY + 34);
    }
}

static void drawDynamicFull(lgfx::LovyanGFX& gfx, bool valid, float sog) {
    if (valid && s_peakKnots > 0.05f) {
        drawPeakMarker(gfx, s_peakKnots);
    }
    drawCenterDynamic(gfx, valid, sog);
}

static void resetCache(bool valid, float sog) {
    s_cacheValid = true;
    s_lastValid = valid;
    s_lastSog = sog;
    s_lastPeak = s_peakKnots;
    s_lastNeedleAngle = valid ? speedToAngle(sog) : -1.0f;
    s_lastPeakAngle = (valid && s_peakKnots > 0.05f) ? speedToAngle(s_peakKnots) : -1.0f;
}

bool sogSpeedometerDataChanged(const WindData& wind, bool stale) {
    bool valid = wind.sog_valid && !stale;
    float sog = valid ? wind.sog_knots : 0.0f;

    if (valid) {
        updatePeak(sog, true, millis());
    }

    if (valid != s_lastValid) return true;
    if (valid && fabsf(sog - s_lastSog) > 0.05f) return true;
    if (valid && s_peakKnots > sog + 0.03f) return true;
    return false;
}

void sogSpeedometerScreenDraw(LGFX& display, DisplayUpdateKind kind, const WindData& wind, bool stale,
                              unsigned long now_ms) {
    bool valid = wind.sog_valid && !stale;
    float sog = valid ? wind.sog_knots : 0.0f;

    updatePeak(sog, valid, now_ms);

    DisplayStaticLayer& layer = displayStaticLayer();
    layer.ensure(display, USER_SCREEN_SOG);

    if (kind == DisplayUpdateKind::Full || !layer.isReadyFor(USER_SCREEN_SOG)) {
        buildStaticLayer(*layer.sprite());
        layer.markReady();
        layer.pushFull(display);
        drawDynamicFull(display, valid, sog);
        resetCache(valid, sog);
        s_lastSog = sog;
        s_lastPeak = s_peakKnots;
        s_lastValid = valid;
        return;
    }

    float needleAngle = valid ? speedToAngle(sog) : -1.0f;
    float peakAngle = (valid && s_peakKnots > 0.05f) ? speedToAngle(s_peakKnots) : -1.0f;

    bool needleChanged = valid != s_lastValid ||
                         (valid && fabsf(needleAngle - s_lastNeedleAngle) > 0.05f);
    bool peakChanged = fabsf(s_peakKnots - s_lastPeak) > 0.03f ||
                       (peakAngle < 0.0f) != (s_lastPeakAngle < 0.0f) ||
                       (peakAngle >= 0.0f && s_lastPeakAngle >= 0.0f &&
                        fabsf(peakAngle - s_lastPeakAngle) > 0.05f);
    bool centerChanged = valid != s_lastValid ||
                         (valid && fabsf(sog - s_lastSog) > 0.05f) ||
                         (valid && ((s_peakKnots > sog + 0.05f) != (s_lastPeak > s_lastSog + 0.05f)));
    bool clusterChanged = needleChanged || peakChanged || centerChanged;

    if (needleChanged || peakChanged) {
        int16_t minX = DISPLAY_WIDTH - 1;
        int16_t minY = DISPLAY_HEIGHT - 1;
        int16_t maxX = 0;
        int16_t maxY = 0;

        auto expandNeedle = [&](float angle) {
            if (angle < 0.0f) return;
            int16_t bx0, by0, bx1, by1;
            needleBounds(angle, bx0, by0, bx1, by1);
            if (bx0 < minX) minX = bx0;
            if (by0 < minY) minY = by0;
            if (bx1 > maxX) maxX = bx1;
            if (by1 > maxY) maxY = by1;
        };

        auto expandPeak = [&](float angle, float kts) {
            if (kts < 0.05f) return;
            int16_t bx0, by0, bx1, by1;
            peakBounds(kts, bx0, by0, bx1, by1);
            if (bx0 < minX) minX = bx0;
            if (by0 < minY) minY = by0;
            if (bx1 > maxX) maxX = bx1;
            if (by1 > maxY) maxY = by1;
        };

        if (needleChanged) {
            expandNeedle(s_lastNeedleAngle);
            expandNeedle(needleAngle);
        }
        if (peakChanged) {
            if (s_lastPeak > 0.05f) expandPeak(s_lastPeakAngle, s_lastPeak);
            if (s_peakKnots > 0.05f) expandPeak(peakAngle, s_peakKnots);
        }

        if (maxX >= minX && maxY >= minY) {
            layer.patchRect(display, minX, minY, maxX - minX + 1, maxY - minY + 1);
        }
    } else if (centerChanged) {
        layer.patchRect(display, CX - 50, CY - 10, 100, 52);
    }

    if (clusterChanged) {
        if (valid && s_peakKnots > 0.05f) {
            drawPeakMarker(display, s_peakKnots);
        }
        drawCenterDynamic(display, valid, sog);
    }

    resetCache(valid, sog);
    s_lastSog = sog;
    s_lastPeak = s_peakKnots;
    s_lastValid = valid;
}
