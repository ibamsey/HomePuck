#include "bme_env_screen.h"
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
static const uint16_t C_RED = 0xF800;
static const uint16_t C_BLUE = 0x001F;

static const int16_t CX = DISPLAY_WIDTH / 2;
static const int16_t CY = 112;
static const int16_t OUTER_R = 104;
static const int16_t INNER_R = 86;
static const int16_t LABEL_R = 78;
static const int16_t NEEDLE_INNER_R = 24;
static const int16_t PATCH_MARGIN = 4;
static const int16_t GAP_READOUT_Y = 188;

static const float ARC_START_DEG = 225.0f;
static const float ARC_SWEEP_DEG = 270.0f;
static const float ARC_END_DEG = ARC_START_DEG + ARC_SWEEP_DEG;

static float s_lastTemp = -999.0f;
static float s_lastPressure = -999.0f;
static float s_lastNeedleAngle = -1.0f;
static bool s_lastPressureValid = false;
static bool s_lastTempValid = false;

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

static float pressureToAngle(float hpa) {
    float t = (hpa - BME_PRESSURE_MIN_HPA) / (BME_PRESSURE_MAX_HPA - BME_PRESSURE_MIN_HPA);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return normalizeAngle(ARC_START_DEG + t * ARC_SWEEP_DEG);
}

static void drawArcLine(lgfx::LovyanGFX& gfx, float startDeg, float endDeg, int16_t radius, uint16_t color) {
    float startRad = (90.0f - startDeg) * (float)M_PI / 180.0f;
    int steps = (int)(fabsf(endDeg - startDeg) * 0.8f);
    if (steps < 12) steps = 12;

    for (int i = 1; i <= steps; i++) {
        float deg = startDeg + (endDeg - startDeg) * ((float)i / (float)steps);
        float rad = (90.0f - deg) * (float)M_PI / 180.0f;
        int16_t x0 = CX + (int16_t)(cosf(startRad) * radius);
        int16_t y0 = CY - (int16_t)(sinf(startRad) * radius);
        int16_t x1 = CX + (int16_t)(cosf(rad) * radius);
        int16_t y1 = CY - (int16_t)(sinf(rad) * radius);
        gfx.drawLine(x0, y0, x1, y1, color);
        startRad = rad;
    }
}

static void drawArcBand(lgfx::LovyanGFX& gfx, float startHpa, float endHpa, uint16_t color) {
    int steps = (int)((endHpa - startHpa) * 2.5f);
    if (steps < 8) steps = 8;

    float startAngle = pressureToAngle(startHpa);
    float prevRad = (90.0f - startAngle) * (float)M_PI / 180.0f;

    for (int i = 1; i <= steps; i++) {
        float hpa = startHpa + (endHpa - startHpa) * ((float)i / (float)steps);
        float angle = pressureToAngle(hpa);
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

static void drawBandTick(lgfx::LovyanGFX& gfx, float hpa, bool major) {
    float angle = pressureToAngle(hpa);
    int16_t x0, y0, x1, y1;
    ourAngleToPoint(CX, CY, OUTER_R, angle, x0, y0);
    ourAngleToPoint(CX, CY, major ? INNER_R : INNER_R + 5, angle, x1, y1);
    gfx.drawLine(x0, y0, x1, y1, major ? C_WHITE : C_DIM);
    if (major) {
        gfx.drawLine(x0 + 1, y0, x1 + 1, y1, C_WHITE);
    }
}

static void drawBandLabel(lgfx::LovyanGFX& gfx, float hpa) {
    float angle = pressureToAngle(hpa);
    int16_t lx, ly;
    ourAngleToPoint(CX, CY, LABEL_R, angle, lx, ly);

    char buf[8];
    snprintf(buf, sizeof(buf), "%d", (int)hpa);
    gfx.setTextSize(1);
    gfx.setTextColor(C_WHITE, C_BG);
    gfx.setTextDatum(MC_DATUM);
    gfx.drawString(buf, lx, ly);
}

static void drawGapScale(lgfx::LovyanGFX& gfx) {
    const float gapEdgeAngles[] = { ARC_START_DEG, normalizeAngle(ARC_END_DEG) };
    for (float angle : gapEdgeAngles) {
        int16_t xOuter, yOuter, xInner, yInner;
        ourAngleToPoint(CX, CY, OUTER_R + 2, angle, xOuter, yOuter);
        ourAngleToPoint(CX, CY, INNER_R - 4, angle, xInner, yInner);
        gfx.drawLine(xOuter, yOuter, xInner, yInner, C_WHITE);
        gfx.drawLine(xOuter + 1, yOuter, xInner + 1, yInner, C_GREY);
    }

    gfx.setTextSize(1);
    gfx.setTextColor(C_DIM, C_BG);
    gfx.setTextDatum(MC_DATUM);
    gfx.drawString("960", 52, GAP_READOUT_Y + 18);
    gfx.drawString("1060", 188, GAP_READOUT_Y + 18);
}

static void drawNeedle(lgfx::LovyanGFX& gfx, float angle, uint16_t color) {
    angle = normalizeAngle(angle);
    int16_t tipX, tipY;
    ourAngleToPoint(CX, CY, OUTER_R - 3, angle, tipX, tipY);

    float rad = (90.0f - angle) * (float)M_PI / 180.0f;
    const int16_t width = 6;
    float halfWidthAngle = atanf((float)width / 2.0f / (float)NEEDLE_INNER_R);

    int16_t blX = CX + (int16_t)(cosf(rad + halfWidthAngle) * NEEDLE_INNER_R);
    int16_t blY = CY - (int16_t)(sinf(rad + halfWidthAngle) * NEEDLE_INNER_R);
    int16_t brX = CX + (int16_t)(cosf(rad - halfWidthAngle) * NEEDLE_INNER_R);
    int16_t brY = CY - (int16_t)(sinf(rad - halfWidthAngle) * NEEDLE_INNER_R);

    gfx.fillTriangle(tipX, tipY, blX, blY, brX, brY, color);
}

static void needleBounds(float angle, int16_t& minX, int16_t& minY, int16_t& maxX, int16_t& maxY) {
    angle = normalizeAngle(angle);
    int16_t tipX, tipY;
    ourAngleToPoint(CX, CY, OUTER_R - 3, angle, tipX, tipY);

    float rad = (90.0f - angle) * (float)M_PI / 180.0f;
    const int16_t width = 6;
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

static void drawCenteredValue(lgfx::LovyanGFX& gfx, bool valid, float value, int decimals,
                              const char* unit, int16_t valueY, uint8_t valueSize, uint16_t valueColor) {
    char valueBuf[16];
    if (valid) {
        if (decimals == 0) {
            snprintf(valueBuf, sizeof(valueBuf), "%.0f", value);
        } else {
            snprintf(valueBuf, sizeof(valueBuf), "%.*f", decimals, value);
        }
    } else {
        snprintf(valueBuf, sizeof(valueBuf), "--");
    }

    gfx.setTextSize(valueSize);
    gfx.setTextColor(valid ? valueColor : C_GREY, C_BG);
    gfx.setTextDatum(MC_DATUM);
    gfx.drawString(valueBuf, CX, valueY);

    gfx.setTextSize(1);
    gfx.setTextColor(valid ? C_DIM : C_GREY, C_BG);
    gfx.setTextDatum(TC_DATUM);
    gfx.drawString(unit, CX, valueY + (valueSize >= 4 ? 22 : 16));
}

static void drawCenterReadings(lgfx::LovyanGFX& gfx, bool tempValid, float tempC,
                               bool pressureValid, float pressureHpa) {
    gfx.fillRect(CX - 72, CY - 30, 144, 52, C_BG);
    gfx.fillRect(CX - 56, GAP_READOUT_Y - 18, 112, 40, C_BG);

    drawCenteredValue(gfx, tempValid, tempC, 1, "°C", CY - 6, 3, C_WHITE);
    drawCenteredValue(gfx, pressureValid, pressureHpa, 0, "hPa", GAP_READOUT_Y, 4, C_CYAN);
}

static void buildStaticLayer(lgfx::LGFX_Sprite& sprite) {
    sprite.fillScreen(C_BG);

    drawArcLine(sprite, ARC_START_DEG, ARC_END_DEG, OUTER_R + 2, C_GREY);
    drawArcLine(sprite, ARC_START_DEG, ARC_END_DEG, INNER_R, C_DIM);

    drawArcBand(sprite, BME_PRESSURE_MIN_HPA, 990.0f, C_RED);
    drawArcBand(sprite, 990.0f, BME_PRESSURE_NORMAL_LOW_HPA, C_YELLOW);
    drawArcBand(sprite, BME_PRESSURE_NORMAL_LOW_HPA, BME_PRESSURE_NORMAL_HIGH_HPA, C_GREEN);
    drawArcBand(sprite, BME_PRESSURE_NORMAL_HIGH_HPA, 1035.0f, C_YELLOW);
    drawArcBand(sprite, 1035.0f, BME_PRESSURE_MAX_HPA, C_RED);

    drawGapScale(sprite);

    for (int hpa = (int)BME_PRESSURE_MIN_HPA; hpa <= (int)BME_PRESSURE_MAX_HPA; hpa += 5) {
        drawBandTick(sprite, (float)hpa, hpa % 10 == 0);
    }

    for (int hpa = 980; hpa <= 1040; hpa += 20) {
        drawBandLabel(sprite, (float)hpa);
    }
    drawBandLabel(sprite, 1013.0f);

    sprite.setTextSize(1);
    sprite.setTextColor(C_CYAN, C_BG);
    sprite.setTextDatum(TC_DATUM);
    sprite.drawString("ENV", CX, 14);
}

static void drawDynamicFull(lgfx::LovyanGFX& gfx, bool pressureValid, float pressureHpa,
                            bool tempValid, float tempC) {
    if (pressureValid) {
        drawNeedle(gfx, pressureToAngle(pressureHpa), C_WHITE);
        gfx.fillCircle(CX, CY, 4, C_BLUE);
        gfx.fillCircle(CX, CY, 2, C_BG);
    } else {
        gfx.drawCircle(CX, CY, 4, C_GREY);
    }

    drawCenterReadings(gfx, tempValid, tempC, pressureValid, pressureHpa);
}

static void resetCache(bool pressureValid, float pressureHpa, bool tempValid, float tempC) {
    s_lastPressureValid = pressureValid;
    s_lastTempValid = tempValid;
    s_lastPressure = pressureHpa;
    s_lastTemp = tempC;
    s_lastNeedleAngle = pressureValid ? pressureToAngle(pressureHpa) : -1.0f;
}

bool bmeEnvScreenDataChanged(const EnvironmentData& env, bool stale) {
    bool pressureValid = env.pressure_valid && !stale;
    bool tempValid = env.temperature_valid && !stale;

    if (pressureValid != s_lastPressureValid) return true;
    if (tempValid != s_lastTempValid) return true;
    if (pressureValid && fabsf(env.pressure_hpa - s_lastPressure) > 0.3f) return true;
    if (tempValid && fabsf(env.temperature_c - s_lastTemp) > 0.15f) return true;
    return false;
}

void bmeEnvScreenDraw(LGFX& display, DisplayUpdateKind kind, const EnvironmentData& env, bool stale,
                      unsigned long now_ms) {
    (void)now_ms;

    bool pressureValid = env.pressure_valid && !stale;
    bool tempValid = env.temperature_valid && !stale;
    float pressureHpa = pressureValid ? env.pressure_hpa : 0.0f;
    float tempC = tempValid ? env.temperature_c : 0.0f;

    DisplayStaticLayer& layer = displayStaticLayer();
    layer.ensure(display, USER_SCREEN_BME);

    if (kind == DisplayUpdateKind::Full || !layer.isReadyFor(USER_SCREEN_BME)) {
        buildStaticLayer(*layer.sprite());
        layer.markReady();
        layer.pushFull(display);
        drawDynamicFull(display, pressureValid, pressureHpa, tempValid, tempC);
        resetCache(pressureValid, pressureHpa, tempValid, tempC);
        return;
    }

    float needleAngle = pressureValid ? pressureToAngle(pressureHpa) : -1.0f;
    bool needleChanged = pressureValid != s_lastPressureValid ||
                         (pressureValid && fabsf(needleAngle - s_lastNeedleAngle) > 0.5f);
    bool tempChanged = tempValid != s_lastTempValid ||
                       (tempValid && fabsf(tempC - s_lastTemp) > 0.15f);
    bool pressureTextChanged = pressureValid != s_lastPressureValid ||
                               (pressureValid && fabsf(pressureHpa - s_lastPressure) > 0.3f);

    if (needleChanged) {
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

        expandNeedle(s_lastNeedleAngle);
        expandNeedle(needleAngle);

        if (maxX >= minX && maxY >= minY) {
            layer.patchRect(display, minX, minY, maxX - minX + 1, maxY - minY + 1);
        }

        if (pressureValid) {
            drawNeedle(display, needleAngle, C_WHITE);
            display.fillCircle(CX, CY, 4, C_BLUE);
            display.fillCircle(CX, CY, 2, C_BG);
        } else {
            display.drawCircle(CX, CY, 4, C_GREY);
        }
    }

    if (tempChanged || pressureTextChanged) {
        if (!needleChanged) {
            if (tempChanged) {
                layer.patchRect(display, CX - 72, CY - 30, 144, 52);
            }
            if (pressureTextChanged) {
                layer.patchRect(display, CX - 56, GAP_READOUT_Y - 18, 112, 40);
            }
        }
        drawCenterReadings(display, tempValid, tempC, pressureValid, pressureHpa);
    }

    resetCache(pressureValid, pressureHpa, tempValid, tempC);
}
