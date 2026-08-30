#include "energy_flow_screen.h"
#include "display_compositor.h"
#include "config.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const uint16_t C_BG = 0x0000;
static const uint16_t C_WHITE = 0xFFFF;
static const uint16_t C_GREY = 0x8410;
static const uint16_t C_DIM = 0x2104;
static const uint16_t C_TRACK = 0x1082;
static const uint16_t C_DEMO = 0x8410;

static const int16_t CX = DISPLAY_WIDTH / 2;

static const int16_t BAT_X = CX;
static const int16_t BAT_Y = 118;
static const int16_t FIX_X = 58;
static const int16_t FIX_Y = 58;
static const int16_t PORT_X = 182;
static const int16_t PORT_Y = 58;
static const int16_t BOAT_X = 58;
static const int16_t BOAT_Y = 182;
static const int16_t FRIDGE_X = 182;
static const int16_t FRIDGE_Y = 182;

static const int16_t BAT_R = 30;
static const int16_t NODE_R = 24;

static const float GAUGE_ARC_START = 225.0f;
static const float GAUGE_ARC_SWEEP = 270.0f;

static float s_lastFixed = -999.0f;
static float s_lastPortable = -999.0f;
static float s_lastBattery = -999.0f;
static float s_lastSoc = -999.0f;
static float s_lastBoat = -999.0f;
static float s_lastFridge = -999.0f;
static bool s_lastDemo = false;
static bool s_cacheValid = false;

static const int FLOW_LINE_COUNT = 4;
static int16_t s_lastDotX[FLOW_LINE_COUNT] = {0};
static int16_t s_lastDotY[FLOW_LINE_COUNT] = {0};
static bool s_lastDotValid[FLOW_LINE_COUNT] = {false};

enum GaugeLabelPos : uint8_t {
    LABEL_ABOVE,
    LABEL_BELOW,
};

enum FlowLineId : uint8_t {
    FLOW_FIXED = 0,
    FLOW_PORTABLE,
    FLOW_BOAT,
    FLOW_FRIDGE,
};

static float normalizeAngle(float angle) {
    while (angle < 0.0f) angle += 360.0f;
    while (angle >= 360.0f) angle -= 360.0f;
    return angle;
}

static uint16_t heatColor(float pct) {
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 1.0f) pct = 1.0f;

    uint8_t r5 = (uint8_t)(pct * 31.0f);
    uint8_t b5 = (uint8_t)((1.0f - pct) * 31.0f);
    uint8_t g6;
    if (pct < 0.5f) {
        g6 = (uint8_t)(pct * 2.0f * 42.0f);
    } else {
        g6 = (uint8_t)((1.0f - pct) * 2.0f * 63.0f);
    }
    return (uint16_t)((r5 << 11) | (g6 << 5) | b5);
}

static float gaugePct(float value, float max_value, bool valid) {
    if (!valid || max_value <= 0.0f) return 0.0f;
    float pct = value / max_value;
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 1.0f) pct = 1.0f;
    return pct;
}

static EnergyData demoEnergy(unsigned long now_ms) {
    EnergyData demo;
    float phase = (float)now_ms / 3500.0f;

    demo.fixed_solar_w = 8.0f + 6.0f * (0.5f + 0.5f * sinf(phase));
    demo.portable_solar_w = 35.0f + 20.0f * sinf(phase * 0.7f);
    demo.boat_load_w = 45.0f + 15.0f * sinf(phase * 0.5f);
    demo.fridge_load_w = 18.0f + 8.0f * sinf(phase * 0.9f);
    demo.battery_power_w = demo.fixed_solar_w + demo.portable_solar_w -
                            demo.boat_load_w - demo.fridge_load_w;
    demo.battery_soc_pct = 74.0f + 4.0f * sinf(phase * 0.4f);

    demo.fixed_solar_valid = true;
    demo.portable_solar_valid = true;
    demo.boat_load_valid = true;
    demo.fridge_load_valid = true;
    demo.battery_power_valid = true;
    demo.battery_soc_valid = true;
    return demo;
}

static void edgePoint(int16_t cx, int16_t cy, int16_t r, int16_t tx, int16_t ty,
                      int16_t& ex, int16_t& ey) {
    float dx = (float)(tx - cx);
    float dy = (float)(ty - cy);
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 1.0f) {
        ex = cx;
        ey = cy;
        return;
    }
    ex = cx + (int16_t)(dx / len * (float)r);
    ey = cy + (int16_t)(dy / len * (float)r);
}

static void drawGaugeArc(lgfx::LovyanGFX& gfx, int16_t cx, int16_t cy, int16_t outerR, int16_t innerR,
                         float startNorm, float endNorm, uint16_t color) {
    if (endNorm <= startNorm) return;

    float startAngle = normalizeAngle(GAUGE_ARC_START + GAUGE_ARC_SWEEP * startNorm);
    float endAngle = normalizeAngle(GAUGE_ARC_START + GAUGE_ARC_SWEEP * endNorm);
    float span = endAngle - startAngle;
    if (span < 0.0f) span += 360.0f;

    int steps = (int)(span / 3.0f);
    if (steps < 3) steps = 3;

    float prevRad = (90.0f - startAngle) * (float)M_PI / 180.0f;

    for (int i = 1; i <= steps; i++) {
        float t = (float)i / (float)steps;
        float angle = normalizeAngle(startAngle + span * t);
        float rad = (90.0f - angle) * (float)M_PI / 180.0f;

        int16_t xo0 = cx + (int16_t)(cosf(prevRad) * outerR);
        int16_t yo0 = cy - (int16_t)(sinf(prevRad) * outerR);
        int16_t xo1 = cx + (int16_t)(cosf(rad) * outerR);
        int16_t yo1 = cy - (int16_t)(sinf(rad) * outerR);
        gfx.drawLine(xo0, yo0, xo1, yo1, color);

        int16_t xi0 = cx + (int16_t)(cosf(prevRad) * innerR);
        int16_t yi0 = cy - (int16_t)(sinf(prevRad) * innerR);
        int16_t xi1 = cx + (int16_t)(cosf(rad) * innerR);
        int16_t yi1 = cy - (int16_t)(sinf(rad) * innerR);
        gfx.drawLine(xi0, yi0, xi1, yi1, color);

        gfx.drawLine(xo0, yo0, xi0, yi0, color);
        gfx.drawLine(xo1, yo1, xi1, yi1, color);

        prevRad = rad;
    }
}

static void drawFlowLine(lgfx::LovyanGFX& gfx, int16_t x0, int16_t y0, int16_t r0,
                         int16_t x1, int16_t y1, int16_t r1,
                         float watts, float max_watts, unsigned long now_ms,
                         int16_t* outDotX, int16_t* outDotY) {
    if (watts < ENERGY_FLOW_MIN_WATTS || max_watts <= 0.0f) {
        if (outDotX) *outDotX = 0;
        if (outDotY) *outDotY = 0;
        return;
    }

    int16_t sx, sy, ex, ey;
    edgePoint(x0, y0, r0, x1, y1, sx, sy);
    edgePoint(x1, y1, r1, x0, y0, ex, ey);

    uint16_t color = heatColor(gaugePct(watts, max_watts, true));
    int passes = 1 + (int)(watts / (max_watts * 0.35f));
    if (passes > 3) passes = 3;

    for (int i = 0; i < passes; i++) {
        gfx.drawLine(sx, sy, ex, ey, color);
    }

    float t = fmodf((float)now_ms / 1200.0f, 1.0f);
    int16_t px = sx + (int16_t)((float)(ex - sx) * t);
    int16_t py = sy + (int16_t)((float)(ey - sy) * t);
    gfx.fillCircle(px, py, 2, color);

    if (outDotX) *outDotX = px;
    if (outDotY) *outDotY = py;
}

static void drawLabelOutside(lgfx::LovyanGFX& gfx, int16_t x, int16_t y, int16_t radius,
                             const char* label, GaugeLabelPos pos) {
    gfx.setTextSize(1);
    gfx.setTextColor(C_GREY, C_BG);
    if (pos == LABEL_ABOVE) {
        gfx.setTextDatum(TC_DATUM);
        gfx.drawString(label, x, y - radius - 7);
    } else {
        gfx.setTextDatum(TC_DATUM);
        gfx.drawString(label, x, y + radius + 9);
    }
}

static void drawCenterReading(lgfx::LovyanGFX& gfx, int16_t x, int16_t y,
                              const char* main_text, bool valid) {
    size_t len = strlen(main_text);
    uint8_t text_size = 2;
    if (len <= 2) {
        text_size = 3;
    } else if (len > 4) {
        text_size = 1;
    }

    gfx.setTextDatum(MC_DATUM);
    gfx.setTextSize(text_size);
    gfx.setTextColor(valid ? C_WHITE : C_GREY, C_BG);
    gfx.drawString(main_text, x, y);
}

static void drawGaugeShell(lgfx::LovyanGFX& gfx, int16_t x, int16_t y, int16_t radius,
                           const char* label, GaugeLabelPos label_pos) {
    gfx.fillCircle(x, y, radius - 4, C_DIM);
    drawGaugeArc(gfx, x, y, radius, radius - 5, 0.0f, 1.0f, C_TRACK);
    gfx.drawCircle(x, y, radius, C_GREY);
    drawLabelOutside(gfx, x, y, radius, label, label_pos);
}

static void drawGaugeDynamic(lgfx::LovyanGFX& gfx, int16_t x, int16_t y, int16_t radius,
                             float watts, float max_watts, bool valid) {
    float pct = gaugePct(watts, max_watts, valid);
    uint16_t fill = heatColor(pct);

    gfx.fillCircle(x, y, radius - 4, C_DIM);
    drawGaugeArc(gfx, x, y, radius, radius - 5, 0.0f, 1.0f, C_TRACK);
    if (valid && pct > 0.01f) {
        drawGaugeArc(gfx, x, y, radius, radius - 5, 0.0f, pct, fill);
    }
    gfx.drawCircle(x, y, radius, valid ? fill : C_GREY);

    char value_buf[8];
    if (valid) {
        snprintf(value_buf, sizeof(value_buf), "%.0f", watts);
    } else {
        snprintf(value_buf, sizeof(value_buf), "--");
    }
    drawCenterReading(gfx, x, y, value_buf, valid);
}

static void drawBatteryDynamic(lgfx::LovyanGFX& gfx, const EnergyData& data) {
    bool valid = data.battery_soc_valid;
    float soc = valid ? data.battery_soc_pct : 0.0f;
    float pct = gaugePct(soc, ENERGY_GAUGE_BATTERY_MAX_PCT, valid);
    uint16_t fill = heatColor(pct);

    gfx.fillCircle(BAT_X, BAT_Y, BAT_R - 4, C_DIM);
    drawGaugeArc(gfx, BAT_X, BAT_Y, BAT_R, BAT_R - 6, 0.0f, 1.0f, C_TRACK);
    if (valid && pct > 0.01f) {
        drawGaugeArc(gfx, BAT_X, BAT_Y, BAT_R, BAT_R - 6, 0.0f, pct, fill);
    }
    gfx.drawCircle(BAT_X, BAT_Y, BAT_R, valid ? fill : C_GREY);

    char value_buf[8];
    if (valid) {
        snprintf(value_buf, sizeof(value_buf), "%.0f%%", soc);
    } else {
        snprintf(value_buf, sizeof(value_buf), "--%");
    }
    drawCenterReading(gfx, BAT_X, BAT_Y, value_buf, valid);
}

static void patchNodeRegion(LGFX& display, int16_t x, int16_t y, int16_t radius) {
    int16_t patchR = radius + 14;
    displayStaticLayer().patchRect(display, x - patchR, y - patchR, patchR * 2 + 1, patchR * 2 + 1);
}

static void patchDotRegion(LGFX& display, int16_t px, int16_t py) {
    displayStaticLayer().patchRect(display, px - 6, py - 6, 13, 13);
}

static void buildStaticLayer(lgfx::LGFX_Sprite& sprite) {
    sprite.fillScreen(C_BG);
    sprite.drawCircle(CX, DISPLAY_HEIGHT / 2, 112, C_GREY);

    sprite.setTextSize(1);
    sprite.setTextDatum(TC_DATUM);
    sprite.setTextColor(C_WHITE, C_BG);
    sprite.drawString("Energy Flow", CX, 8);

    drawGaugeShell(sprite, FIX_X, FIX_Y, NODE_R, "Fixed", LABEL_BELOW);
    drawGaugeShell(sprite, PORT_X, PORT_Y, NODE_R, "Portable", LABEL_BELOW);
    drawGaugeShell(sprite, BAT_X, BAT_Y, BAT_R, "Battery", LABEL_BELOW);
    drawGaugeShell(sprite, BOAT_X, BOAT_Y, NODE_R, "Boat", LABEL_ABOVE);
    drawGaugeShell(sprite, FRIDGE_X, FRIDGE_Y, NODE_R, "Fridge", LABEL_ABOVE);
}

static void drawDemoLabel(lgfx::LovyanGFX& gfx, bool use_demo) {
    gfx.fillRect(CX - 24, 16, 48, 10, C_BG);
    if (use_demo) {
        gfx.setTextSize(1);
        gfx.setTextDatum(TC_DATUM);
        gfx.setTextColor(C_DEMO, C_BG);
        gfx.drawString("DEMO", CX, 18);
    }
}

static void drawAllFlowLines(lgfx::LovyanGFX& gfx, const EnergyData& view, unsigned long now_ms) {
    if (view.fixed_solar_valid && view.fixed_solar_w > ENERGY_FLOW_MIN_WATTS) {
        drawFlowLine(gfx, FIX_X, FIX_Y, NODE_R, BAT_X, BAT_Y, BAT_R,
                     view.fixed_solar_w, ENERGY_GAUGE_FIXED_MAX_W, now_ms,
                     &s_lastDotX[FLOW_FIXED], &s_lastDotY[FLOW_FIXED]);
        s_lastDotValid[FLOW_FIXED] = true;
    } else {
        s_lastDotValid[FLOW_FIXED] = false;
    }

    if (view.portable_solar_valid && view.portable_solar_w > ENERGY_FLOW_MIN_WATTS) {
        drawFlowLine(gfx, PORT_X, PORT_Y, NODE_R, BAT_X, BAT_Y, BAT_R,
                     view.portable_solar_w, ENERGY_GAUGE_PORTABLE_MAX_W, now_ms + 200,
                     &s_lastDotX[FLOW_PORTABLE], &s_lastDotY[FLOW_PORTABLE]);
        s_lastDotValid[FLOW_PORTABLE] = true;
    } else {
        s_lastDotValid[FLOW_PORTABLE] = false;
    }

    if (view.boat_load_valid && view.boat_load_w > ENERGY_FLOW_MIN_WATTS) {
        drawFlowLine(gfx, BAT_X, BAT_Y, BAT_R, BOAT_X, BOAT_Y, NODE_R,
                     view.boat_load_w, ENERGY_GAUGE_BOAT_MAX_W, now_ms + 300,
                     &s_lastDotX[FLOW_BOAT], &s_lastDotY[FLOW_BOAT]);
        s_lastDotValid[FLOW_BOAT] = true;
    } else {
        s_lastDotValid[FLOW_BOAT] = false;
    }

    if (view.fridge_load_valid && view.fridge_load_w > ENERGY_FLOW_MIN_WATTS) {
        drawFlowLine(gfx, BAT_X, BAT_Y, BAT_R, FRIDGE_X, FRIDGE_Y, NODE_R,
                     view.fridge_load_w, ENERGY_GAUGE_FRIDGE_MAX_W, now_ms + 500,
                     &s_lastDotX[FLOW_FRIDGE], &s_lastDotY[FLOW_FRIDGE]);
        s_lastDotValid[FLOW_FRIDGE] = true;
    } else {
        s_lastDotValid[FLOW_FRIDGE] = false;
    }
}

static void drawAllGaugesDynamic(lgfx::LovyanGFX& gfx, const EnergyData& view) {
    drawGaugeDynamic(gfx, FIX_X, FIX_Y, NODE_R,
                     view.fixed_solar_w, ENERGY_GAUGE_FIXED_MAX_W, view.fixed_solar_valid);
    drawGaugeDynamic(gfx, PORT_X, PORT_Y, NODE_R,
                     view.portable_solar_w, ENERGY_GAUGE_PORTABLE_MAX_W, view.portable_solar_valid);
    drawBatteryDynamic(gfx, view);
    drawGaugeDynamic(gfx, BOAT_X, BOAT_Y, NODE_R,
                     view.boat_load_w, ENERGY_GAUGE_BOAT_MAX_W, view.boat_load_valid);
    drawGaugeDynamic(gfx, FRIDGE_X, FRIDGE_Y, NODE_R,
                     view.fridge_load_w, ENERGY_GAUGE_FRIDGE_MAX_W, view.fridge_load_valid);
}

static void updateFieldCache(const EnergyData& view, bool use_demo) {
    s_lastFixed = view.fixed_solar_w;
    s_lastPortable = view.portable_solar_w;
    s_lastBattery = view.battery_power_w;
    s_lastSoc = view.battery_soc_pct;
    s_lastBoat = view.boat_load_w;
    s_lastFridge = view.fridge_load_w;
    s_lastDemo = use_demo;
    s_cacheValid = true;
}

static bool energyFieldChanged(float prev, float next, bool valid) {
    if (!valid) return false;
    return prev < -900.0f || fabsf(prev - next) > 1.0f;
}

bool energyFlowDataChanged(const EnergyData& energy, bool stale) {
#if ENERGY_FLOW_DEMO_WHEN_STALE
    if (stale || !energy.hasAny()) {
        return true;
    }
#else
    (void)stale;
    if (!energy.hasAny()) return false;
#endif

    bool changed = energyFieldChanged(s_lastFixed, energy.fixed_solar_w, energy.fixed_solar_valid) ||
                   energyFieldChanged(s_lastPortable, energy.portable_solar_w, energy.portable_solar_valid) ||
                   energyFieldChanged(s_lastBattery, energy.battery_power_w, energy.battery_power_valid) ||
                   energyFieldChanged(s_lastSoc, energy.battery_soc_pct, energy.battery_soc_valid) ||
                   energyFieldChanged(s_lastBoat, energy.boat_load_w, energy.boat_load_valid) ||
                   energyFieldChanged(s_lastFridge, energy.fridge_load_w, energy.fridge_load_valid);

#if ENERGY_FLOW_DEMO_WHEN_STALE
    if (s_lastDemo != (stale || !energy.hasAny())) changed = true;
#endif

    return changed;
}

void energyFlowScreenDraw(LGFX& display, DisplayUpdateKind kind, const EnergyData& energy, bool stale,
                          unsigned long now_ms) {
    bool use_demo = false;
#if ENERGY_FLOW_DEMO_WHEN_STALE
    use_demo = stale || !energy.hasAny();
#endif

    EnergyData view = use_demo ? demoEnergy(now_ms) : energy;

    DisplayStaticLayer& layer = displayStaticLayer();
    layer.ensure(display, USER_SCREEN_ENERGY);

    if (kind == DisplayUpdateKind::Full || !layer.isReadyFor(USER_SCREEN_ENERGY) ||
        use_demo != s_lastDemo) {
        buildStaticLayer(*layer.sprite());
        layer.markReady();
        layer.pushFull(display);
        drawDemoLabel(display, use_demo);
        drawAllFlowLines(display, view, now_ms);
        drawAllGaugesDynamic(display, view);
        updateFieldCache(view, use_demo);
        return;
    }

    bool demoChanged = use_demo != s_lastDemo;
    if (demoChanged) {
        drawDemoLabel(display, use_demo);
    }

    bool fixedChanged = energyFieldChanged(s_lastFixed, view.fixed_solar_w, view.fixed_solar_valid);
    bool portableChanged = energyFieldChanged(s_lastPortable, view.portable_solar_w, view.portable_solar_valid);
    bool batteryChanged = energyFieldChanged(s_lastBattery, view.battery_power_w, view.battery_power_valid) ||
                          energyFieldChanged(s_lastSoc, view.battery_soc_pct, view.battery_soc_valid);
    bool boatChanged = energyFieldChanged(s_lastBoat, view.boat_load_w, view.boat_load_valid);
    bool fridgeChanged = energyFieldChanged(s_lastFridge, view.fridge_load_w, view.fridge_load_valid);

    for (int i = 0; i < FLOW_LINE_COUNT; i++) {
        if (s_lastDotValid[i]) {
            patchDotRegion(display, s_lastDotX[i], s_lastDotY[i]);
        }
    }

    if (fixedChanged) patchNodeRegion(display, FIX_X, FIX_Y, NODE_R);
    if (portableChanged) patchNodeRegion(display, PORT_X, PORT_Y, NODE_R);
    if (batteryChanged) patchNodeRegion(display, BAT_X, BAT_Y, BAT_R);
    if (boatChanged) patchNodeRegion(display, BOAT_X, BOAT_Y, NODE_R);
    if (fridgeChanged) patchNodeRegion(display, FRIDGE_X, FRIDGE_Y, NODE_R);

    drawAllFlowLines(display, view, now_ms);
    drawAllGaugesDynamic(display, view);
    updateFieldCache(view, use_demo);
}
