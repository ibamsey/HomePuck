#include "wind_widget.h"
#include "display_compositor.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const uint16_t C_BG = 0x0000;
static const uint16_t C_WHITE = 0xFFFF;
static const uint16_t C_GREY = 0x8410;
static const uint16_t C_CYAN = 0x07FF;
static const uint16_t C_DIM = 0x4208;

static const int16_t CX = WIND_WIDGET_CENTER_X;
static const int16_t CY = WIND_WIDGET_CENTER_Y;
static const int16_t OUTER_R = WIND_WIDGET_OUTER_RADIUS;
static const int16_t INNER_R = WIND_WIDGET_INNER_RADIUS;
static const int16_t MODE_LABEL_Y = 158;
static const int16_t MODE_LABEL_PATCH_X = CX - 22;
static const int16_t MODE_LABEL_PATCH_Y = 152;
static const int16_t MODE_LABEL_PATCH_W = 44;
static const int16_t MODE_LABEL_PATCH_H = 14;
static const int16_t PATCH_MARGIN = 4;

float WindWidget::normalizeAngle(float angle) {
    while (angle < 0.0f) angle += 360.0f;
    while (angle >= 360.0f) angle -= 360.0f;
    return angle;
}

void WindWidget::ourAngleToPoint(int16_t cx, int16_t cy, int16_t radius, float ourAngle, int16_t& x, int16_t& y) {
    float rad = (90.0f - ourAngle) * (float)M_PI / 180.0f;
    x = cx + (int16_t)(cosf(rad) * radius);
    y = cy - (int16_t)(sinf(rad) * radius);
}

void WindWidget::pointerBounds(float angle, int16_t width, bool large,
                               int16_t& minX, int16_t& minY, int16_t& maxX, int16_t& maxY) const {
    angle = normalizeAngle(angle);
    int16_t tipR = large ? (INNER_R - 28) : (INNER_R - 12);
    int16_t baseR = large ? (OUTER_R - 1) : (OUTER_R + 3);

    int16_t tipX, tipY;
    ourAngleToPoint(CX, CY, tipR, angle, tipX, tipY);

    float rad = (90.0f - angle) * (float)M_PI / 180.0f;
    float halfWidthAngle = atanf((float)width / 2.0f / (float)baseR);

    int16_t blX = CX + (int16_t)(cosf(rad + halfWidthAngle) * baseR);
    int16_t blY = CY - (int16_t)(sinf(rad + halfWidthAngle) * baseR);
    int16_t brX = CX + (int16_t)(cosf(rad - halfWidthAngle) * baseR);
    int16_t brY = CY - (int16_t)(sinf(rad - halfWidthAngle) * baseR);

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

void WindWidget::patchPointerRegion(LGFX& display, float angle, int16_t width, bool large, bool valid) {
    if (!valid) return;

    int16_t minX, minY, maxX, maxY;
    pointerBounds(angle, width, large, minX, minY, maxX, maxY);
    displayStaticLayer().patchRect(display, minX, minY, maxX - minX + 1, maxY - minY + 1);
}

void WindWidget::drawTick(lgfx::LovyanGFX& gfx, int16_t angle, bool major) {
    int16_t outer = OUTER_R;
    int16_t inner = OUTER_R - (major ? 12 : 8);
    int16_t x0, y0, x1, y1;
    ourAngleToPoint(CX, CY, outer, (float)angle, x0, y0);
    ourAngleToPoint(CX, CY, inner, (float)angle, x1, y1);
    gfx.drawLine(x0, y0, x1, y1, major ? C_WHITE : C_GREY);
}

void WindWidget::drawScale(lgfx::LovyanGFX& gfx) {
    float prevRad = (90.0f - WIND_WIDGET_SCALE_START) * (float)M_PI / 180.0f;
    for (int a = WIND_WIDGET_SCALE_START + 1; a <= WIND_WIDGET_SCALE_END; a++) {
        float rad = (90.0f - a) * (float)M_PI / 180.0f;
        int16_t xo0 = CX + (int16_t)(cosf(prevRad) * OUTER_R);
        int16_t yo0 = CY - (int16_t)(sinf(prevRad) * OUTER_R);
        int16_t xo1 = CX + (int16_t)(cosf(rad) * OUTER_R);
        int16_t yo1 = CY - (int16_t)(sinf(rad) * OUTER_R);
        gfx.drawLine(xo0, yo0, xo1, yo1, C_WHITE);

        int16_t xi0 = CX + (int16_t)(cosf(prevRad) * INNER_R);
        int16_t yi0 = CY - (int16_t)(sinf(prevRad) * INNER_R);
        int16_t xi1 = CX + (int16_t)(cosf(rad) * INNER_R);
        int16_t yi1 = CY - (int16_t)(sinf(rad) * INNER_R);
        gfx.drawLine(xi0, yi0, xi1, yi1, C_WHITE);
        prevRad = rad;
    }

    for (int angle = WIND_WIDGET_SCALE_START; angle <= WIND_WIDGET_SCALE_END; angle += 30) {
        drawTick(gfx, angle, angle % 90 == 0);
    }

    gfx.setTextSize(1);
    gfx.setTextColor(C_GREY, C_BG);
    int16_t labelRadius = INNER_R - 14;
    for (int angle = 30; angle <= 330; angle += 30) {
        int16_t lx, ly;
        ourAngleToPoint(CX, CY, labelRadius, (float)angle, lx, ly);
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", angle);
        gfx.setTextDatum(MC_DATUM);
        gfx.drawString(buf, lx, ly);
    }
}

void WindWidget::drawPointer(lgfx::LovyanGFX& gfx, float angle, int16_t width, uint16_t fill, uint16_t outline, bool outlineOnly) {
    angle = normalizeAngle(angle);
    bool large = width > 14;
    int16_t tipR = large ? (INNER_R - 28) : (INNER_R - 12);
    int16_t baseR = large ? (OUTER_R - 1) : (OUTER_R + 3);

    int16_t tipX, tipY;
    ourAngleToPoint(CX, CY, tipR, angle, tipX, tipY);

    float rad = (90.0f - angle) * (float)M_PI / 180.0f;
    float halfWidthAngle = atanf((float)width / 2.0f / (float)baseR);

    int16_t blX = CX + (int16_t)(cosf(rad + halfWidthAngle) * baseR);
    int16_t blY = CY - (int16_t)(sinf(rad + halfWidthAngle) * baseR);
    int16_t brX = CX + (int16_t)(cosf(rad - halfWidthAngle) * baseR);
    int16_t brY = CY - (int16_t)(sinf(rad - halfWidthAngle) * baseR);

    if (outlineOnly) {
        gfx.drawLine(tipX, tipY, blX, blY, outline);
        gfx.drawLine(blX, blY, brX, brY, outline);
        gfx.drawLine(brX, brY, tipX, tipY, outline);
    } else {
        gfx.fillTriangle(tipX, tipY, blX, blY, brX, brY, fill);
    }
}

void WindWidget::drawBoat(lgfx::LovyanGFX& gfx) {
    const int16_t hullLength = 44;
    const int16_t hullWidth = 7;
    const int16_t hullSpacing = 18;
    const int16_t bowLength = 12;

    int16_t cx = CX;
    int16_t cy = CY - 36;
    int16_t hullPort = cx - hullSpacing / 2;
    int16_t hullStbd = cx + hullSpacing / 2;
    int16_t bowY = cy - hullLength / 2;
    int16_t sternY = cy + hullLength / 2;
    int16_t bowEndY = bowY + bowLength;

    auto hull = [&](int16_t hx) {
        gfx.drawLine(hx, bowY, hx - hullWidth / 2, bowEndY, C_CYAN);
        gfx.drawLine(hx - hullWidth / 2, bowEndY, hx - hullWidth / 2, sternY, C_CYAN);
        gfx.drawLine(hx - hullWidth / 2, sternY, hx + hullWidth / 2, sternY, C_CYAN);
        gfx.drawLine(hx + hullWidth / 2, sternY, hx + hullWidth / 2, bowEndY, C_CYAN);
        gfx.drawLine(hx + hullWidth / 2, bowEndY, hx, bowY, C_CYAN);
    };

    hull(hullPort);
    hull(hullStbd);
    gfx.drawLine(hullPort, bowEndY + 6, hullStbd, bowEndY + 6, C_CYAN);
    gfx.drawLine(hullPort, sternY - 10, hullStbd, sternY - 10, C_CYAN);
}

void WindWidget::drawNoGoLines(lgfx::LovyanGFX& gfx) {
    int16_t startR = INNER_R - 22;
    int16_t endR = startR - 28;
    for (int angle : {30, 330}) {
        int16_t x0, y0, x1, y1;
        ourAngleToPoint(CX, CY, startR, (float)angle, x0, y0);
        ourAngleToPoint(CX, CY, endR, (float)angle, x1, y1);
        gfx.drawLine(x0, y0, x1, y1, C_DIM);
    }
}

void WindWidget::buildStatic(lgfx::LGFX_Sprite& sprite) {
    sprite.fillScreen(C_BG);
    drawScale(sprite);
    drawNoGoLines(sprite);
    drawBoat(sprite);
}

void WindWidget::drawCenter(lgfx::LovyanGFX& gfx, const WindWidgetData& data, bool restoreFromStatic) {
    float speed = data.show_tws ? data.tws_knots : data.aws_knots;

    char speedBuf[16];
    if (data.speed_valid) {
        snprintf(speedBuf, sizeof(speedBuf), "%.1f", speed);
    } else {
        snprintf(speedBuf, sizeof(speedBuf), "--.-");
    }

    if (restoreFromStatic) {
        displayStaticLayer().patchRect(static_cast<LGFX&>(gfx), CX - 58, CY - 18, 116, 48);
    } else {
        gfx.fillRect(CX - 58, CY - 18, 116, 48, C_BG);
    }

    gfx.setTextDatum(MC_DATUM);
    gfx.setTextSize(3);
    gfx.setTextColor(data.speed_valid ? C_WHITE : C_GREY, C_BG);
    gfx.drawString(speedBuf, CX, CY + 2);

    gfx.setTextSize(1);
    gfx.setTextColor(C_GREY, C_BG);
    gfx.drawString("kts", CX, CY + 24);
}

void WindWidget::drawModeIndicator(lgfx::LovyanGFX& gfx, bool show_tws) {
    gfx.setTextSize(1);
    gfx.setTextDatum(TC_DATUM);
    gfx.setTextColor(C_CYAN, C_BG);
    gfx.drawString(show_tws ? "TWS" : "AWS", CX, MODE_LABEL_Y);
}

void WindWidget::drawDynamicFull(LGFX& display, const WindWidgetData& data) {
    if (data.awa_valid) {
        drawPointer(display, data.awa_degrees, 20, C_WHITE, C_WHITE, false);
    }
    if (data.twd_valid) {
        drawPointer(display, data.twd_degrees, 14, C_CYAN, C_WHITE, false);
        drawPointer(display, data.twd_degrees, 14, C_CYAN, C_WHITE, true);
    }
    drawCenter(display, data, false);
    drawModeIndicator(display, data.show_tws);
}

void WindWidget::resetCache(const WindWidgetData& data) {
    _cacheValid = true;
    _lastAwa = data.awa_degrees;
    _lastTwd = data.twd_degrees;
    _lastAwaValid = data.awa_valid;
    _lastTwdValid = data.twd_valid;
    _lastSpeedValid = data.speed_valid;
    _lastShowTws = data.show_tws;
    _lastSpeed = data.show_tws ? data.tws_knots : data.aws_knots;
}

void WindWidget::drawDynamicPartial(LGFX& display, const WindWidgetData& data) {
    bool awaChanged = data.awa_valid != _lastAwaValid ||
                      (data.awa_valid && fabsf(data.awa_degrees - _lastAwa) > 0.05f);
    bool twdChanged = data.twd_valid != _lastTwdValid ||
                      (data.twd_valid && fabsf(data.twd_degrees - _lastTwd) > 0.05f);
    float speed = data.show_tws ? data.tws_knots : data.aws_knots;
    bool speedChanged = data.speed_valid != _lastSpeedValid ||
                         data.show_tws != _lastShowTws ||
                         (data.speed_valid && fabsf(speed - _lastSpeed) > 0.01f);
    bool modeChanged = data.show_tws != _lastShowTws;

    int16_t patchMinX = 0;
    int16_t patchMinY = 0;
    int16_t patchMaxX = -1;
    int16_t patchMaxY = -1;

    if (awaChanged || twdChanged) {
        int16_t minX = DISPLAY_WIDTH - 1;
        int16_t minY = DISPLAY_HEIGHT - 1;
        int16_t maxX = 0;
        int16_t maxY = 0;

        auto expand = [&](float angle, int16_t width, bool large, bool valid) {
            if (!valid) return;
            int16_t bx0, by0, bx1, by1;
            pointerBounds(angle, width, large, bx0, by0, bx1, by1);
            if (bx0 < minX) minX = bx0;
            if (by0 < minY) minY = by0;
            if (bx1 > maxX) maxX = bx1;
            if (by1 > maxY) maxY = by1;
        };

        if (awaChanged) {
            expand(_lastAwa, 20, true, _lastAwaValid);
            expand(data.awa_degrees, 20, true, data.awa_valid);
        }
        if (twdChanged) {
            expand(_lastTwd, 14, false, _lastTwdValid);
            expand(data.twd_degrees, 14, false, data.twd_valid);
        }

        if (maxX >= minX && maxY >= minY) {
            patchMinX = minX;
            patchMinY = minY;
            patchMaxX = maxX;
            patchMaxY = maxY;
            displayStaticLayer().patchRect(display, minX, minY, maxX - minX + 1, maxY - minY + 1);
        }
    }

    if (data.awa_valid && (awaChanged || !_cacheValid)) {
        drawPointer(display, data.awa_degrees, 20, C_WHITE, C_WHITE, false);
    } else if (_lastAwaValid && !data.awa_valid) {
        patchPointerRegion(display, _lastAwa, 20, true, true);
    }

    if (data.twd_valid && (twdChanged || !_cacheValid)) {
        drawPointer(display, data.twd_degrees, 14, C_CYAN, C_WHITE, false);
        drawPointer(display, data.twd_degrees, 14, C_CYAN, C_WHITE, true);
    } else if (_lastTwdValid && !data.twd_valid) {
        patchPointerRegion(display, _lastTwd, 14, false, true);
    }

    bool patchHitCenter = patchMaxX >= patchMinX &&
                          displayRectsOverlap(patchMinX, patchMinY,
                                              patchMaxX - patchMinX + 1, patchMaxY - patchMinY + 1,
                                              CX - 58, CY - 18, 116, 48);
    bool patchHitModeLabel = patchMaxX >= patchMinX &&
                             displayRectsOverlap(patchMinX, patchMinY,
                                                 patchMaxX - patchMinX + 1, patchMaxY - patchMinY + 1,
                                                 MODE_LABEL_PATCH_X, MODE_LABEL_PATCH_Y,
                                                 MODE_LABEL_PATCH_W, MODE_LABEL_PATCH_H);

    if (speedChanged || patchHitCenter) {
        drawCenter(display, data, true);
    }

    if (modeChanged) {
        displayStaticLayer().patchRect(display, MODE_LABEL_PATCH_X, MODE_LABEL_PATCH_Y,
                                       MODE_LABEL_PATCH_W, MODE_LABEL_PATCH_H);
        drawModeIndicator(display, data.show_tws);
    } else if (patchHitModeLabel) {
        displayStaticLayer().patchRect(display, MODE_LABEL_PATCH_X, MODE_LABEL_PATCH_Y,
                                       MODE_LABEL_PATCH_W, MODE_LABEL_PATCH_H);
        drawModeIndicator(display, data.show_tws);
    }

    resetCache(data);
}

void WindWidget::draw(LGFX& display, DisplayUpdateKind kind, const WindWidgetData& data) {
    DisplayStaticLayer& layer = displayStaticLayer();
    layer.ensure(display, USER_SCREEN_WIND);

    if (kind == DisplayUpdateKind::Full || !layer.isReadyFor(USER_SCREEN_WIND)) {
        buildStatic(*layer.sprite());
        layer.markReady();
        layer.pushFull(display);
        drawDynamicFull(display, data);
        resetCache(data);
        return;
    }

    drawDynamicPartial(display, data);
}

void WindWidget::toggleMode(WindWidgetData& data) {
    data.show_tws = !data.show_tws;
}
