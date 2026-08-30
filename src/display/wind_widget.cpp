#include "wind_widget.h"
#include <math.h>

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

WindWidget::~WindWidget() {
    delete _staticLayer;
    _staticLayer = nullptr;
}

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

void WindWidget::ensureStatic(LGFX& display) {
    if (_staticReady) return;

    if (_staticLayer == nullptr) {
        _staticLayer = new lgfx::LGFX_Sprite(&display);
    }
    if (_staticLayer->getBuffer() == nullptr) {
        _staticLayer->createSprite(WIND_WIDGET_SIZE, WIND_WIDGET_SIZE);
    }

    _staticLayer->fillScreen(C_BG);
    drawScale(*_staticLayer);
    drawNoGoLines(*_staticLayer);
    drawBoat(*_staticLayer);
    _staticReady = true;
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

void WindWidget::drawCenter(lgfx::LovyanGFX& gfx, const WindWidgetData& data) {
    float speed = data.show_tws ? data.tws_knots : data.aws_knots;

    char speedBuf[16];
    if (data.speed_valid) {
        snprintf(speedBuf, sizeof(speedBuf), "%.1f", speed);
    } else {
        snprintf(speedBuf, sizeof(speedBuf), "--.-");
    }

    gfx.fillRect(CX - 58, CY - 18, 116, 48, C_BG);

    gfx.setTextDatum(MC_DATUM);
    gfx.setTextSize(3);
    gfx.setTextColor(data.speed_valid ? C_WHITE : C_GREY, C_BG);
    gfx.drawString(speedBuf, CX, CY + 2);

    gfx.setTextSize(1);
    gfx.setTextColor(C_GREY, C_BG);
    gfx.drawString("kts", CX, CY + 24);
}

void WindWidget::drawModeIndicator(lgfx::LovyanGFX& gfx, bool show_tws) {
    const int16_t y = 196;
    const int16_t pillW = 38;
    const int16_t pillH = 18;

    auto drawPill = [&](const char* label, int16_t cx, bool active) {
        int16_t x = cx - pillW / 2;
        int16_t y0 = y - pillH / 2;
        if (active) {
            gfx.fillRoundRect(x, y0, pillW, pillH, 4, C_CYAN);
            gfx.setTextColor(C_BG, C_CYAN);
        } else {
            gfx.drawRoundRect(x, y0, pillW, pillH, 4, C_GREY);
            gfx.setTextColor(C_GREY, C_BG);
        }
        gfx.setTextSize(1);
        gfx.setTextDatum(MC_DATUM);
        gfx.drawString(label, cx, y + 1);
    };

    drawPill("AWS", CX - 24, !show_tws);
    drawPill("TWS", CX + 24, show_tws);
}

void WindWidget::drawDynamic(LGFX& display, const WindWidgetData& data) {
    _staticLayer->pushSprite(&display, 0, 0);

    if (data.awa_valid) {
        drawPointer(display, data.awa_degrees, 20, C_WHITE, C_WHITE, false);
    }
    if (data.twd_valid) {
        // Smaller cyan triangle with white outline (matches e-ink TWD style)
        drawPointer(display, data.twd_degrees, 14, C_CYAN, C_WHITE, false);
        drawPointer(display, data.twd_degrees, 14, C_CYAN, C_WHITE, true);
    }

    drawCenter(display, data);
    drawModeIndicator(display, data.show_tws);
}

void WindWidget::draw(LGFX& display, const WindWidgetData& data) {
    ensureStatic(display);
    drawDynamic(display, data);
}

void WindWidget::toggleMode(WindWidgetData& data) {
    data.show_tws = !data.show_tws;
}
