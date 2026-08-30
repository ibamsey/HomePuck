#include "energy_flow_screen.h"
#include "config.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const uint16_t C_BG = 0x0000;
static const uint16_t C_WHITE = 0xFFFF;
static const uint16_t C_GREY = 0x8410;
static const uint16_t C_DIM = 0x4208;
static const uint16_t C_SOLAR = 0xFE60;
static const uint16_t C_BATTERY_CHG = 0x07E0;
static const uint16_t C_BATTERY_DCHG = 0xFD20;
static const uint16_t C_LOAD = 0x07FF;
static const uint16_t C_DEMO = 0x8410;

static const int16_t CX = DISPLAY_WIDTH / 2;

// Hub-and-spoke layout (Home Assistant Power Flow Card style): battery centre,
// solar sources upper-left/right, loads lower-left/right.
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

static const int16_t BAT_R = 28;
static const int16_t NODE_R = 22;

static float s_lastFixed = -999.0f;
static float s_lastPortable = -999.0f;
static float s_lastBattery = -999.0f;
static float s_lastSoc = -999.0f;
static float s_lastBoat = -999.0f;
static float s_lastFridge = -999.0f;
static bool s_lastDemo = false;

static EnergyData demoEnergy(unsigned long now_ms) {
    EnergyData demo;
    float phase = (float)now_ms / 3500.0f;

    demo.fixed_solar_w = 95.0f + 35.0f * sinf(phase);
    demo.portable_solar_w = 28.0f + 8.0f * sinf(phase * 0.7f);
    demo.boat_load_w = 68.0f;
    demo.fridge_load_w = 24.0f;
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

static void drawFlowLine(lgfx::LovyanGFX& gfx, int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                         float watts, uint16_t color, unsigned long now_ms) {
    if (watts < ENERGY_FLOW_MIN_WATTS) return;

    int16_t sx, sy, ex, ey;
    edgePoint(x0, y0, NODE_R, x1, y1, sx, sy);
    edgePoint(x1, y1, NODE_R, x0, y0, ex, ey);

    int passes = 1 + (int)(watts / 70.0f);
    if (passes > 4) passes = 4;

    for (int i = 0; i < passes; i++) {
        gfx.drawLine(sx, sy, ex, ey, color);
    }

    float t = fmodf((float)now_ms / 1200.0f, 1.0f);
    int16_t px = sx + (int16_t)((float)(ex - sx) * t);
    int16_t py = sy + (int16_t)((float)(ey - sy) * t);
    gfx.fillCircle(px, py, 3, color);
}

static void formatWatts(char* buf, size_t len, float watts, bool valid) {
    if (!valid) {
        snprintf(buf, len, "--W");
        return;
    }
    if (fabsf(watts) >= 100.0f) {
        snprintf(buf, len, "%.0fW", watts);
    } else {
        snprintf(buf, len, "%.0fW", watts);
    }
}

static void drawSourceNode(lgfx::LovyanGFX& gfx, int16_t x, int16_t y, const char* label,
                           float watts, bool valid) {
    gfx.drawCircle(x, y, NODE_R, C_SOLAR);
    gfx.fillCircle(x, y, NODE_R - 1, C_DIM);

    gfx.setTextSize(1);
    gfx.setTextDatum(MC_DATUM);
    gfx.setTextColor(C_SOLAR, C_DIM);
    gfx.drawString(label, x, y - 4);

    char buf[12];
    formatWatts(buf, sizeof(buf), watts, valid);
    gfx.setTextColor(valid ? C_WHITE : C_GREY, C_DIM);
    gfx.drawString(buf, x, y + 8);
}

static void drawLoadNode(lgfx::LovyanGFX& gfx, int16_t x, int16_t y, const char* label,
                         float watts, bool valid) {
    gfx.drawCircle(x, y, NODE_R, C_LOAD);
    gfx.fillCircle(x, y, NODE_R - 1, C_DIM);

    gfx.setTextSize(1);
    gfx.setTextDatum(MC_DATUM);
    gfx.setTextColor(C_LOAD, C_DIM);
    gfx.drawString(label, x, y - 4);

    char buf[12];
    formatWatts(buf, sizeof(buf), watts, valid);
    gfx.setTextColor(valid ? C_WHITE : C_GREY, C_DIM);
    gfx.drawString(buf, x, y + 8);
}

static void drawBatteryNode(lgfx::LovyanGFX& gfx, const EnergyData& data) {
    bool charging = data.battery_power_valid && data.battery_power_w > ENERGY_FLOW_MIN_WATTS;
    bool discharging = data.battery_power_valid && data.battery_power_w < -ENERGY_FLOW_MIN_WATTS;
    uint16_t ring = C_GREY;
    if (charging) ring = C_BATTERY_CHG;
    else if (discharging) ring = C_BATTERY_DCHG;

    gfx.drawCircle(BAT_X, BAT_Y, BAT_R, ring);
    gfx.fillCircle(BAT_X, BAT_Y, BAT_R - 1, C_DIM);

    if (data.battery_soc_valid) {
        float soc = data.battery_soc_pct;
        if (soc < 0.0f) soc = 0.0f;
        if (soc > 100.0f) soc = 100.0f;
        int16_t bar_h = (int16_t)((float)(BAT_R * 2 - 8) * soc / 100.0f);
        int16_t bar_x = BAT_X - 5;
        int16_t bar_y = BAT_Y + BAT_R - 4 - bar_h;
        gfx.fillRect(bar_x, bar_y, 10, bar_h, ring);
        gfx.drawRect(bar_x - 1, BAT_Y - BAT_R + 4, 12, BAT_R * 2 - 8, C_GREY);
    }

    gfx.setTextSize(1);
    gfx.setTextDatum(MC_DATUM);
    gfx.setTextColor(C_WHITE, C_DIM);
    if (data.battery_soc_valid) {
        char soc_buf[8];
        snprintf(soc_buf, sizeof(soc_buf), "%.0f%%", data.battery_soc_pct);
        gfx.drawString(soc_buf, BAT_X + 10, BAT_Y - 2);
    } else {
        gfx.drawString("Bat", BAT_X + 8, BAT_Y - 2);
    }

    char pwr_buf[12];
    if (data.battery_power_valid) {
        snprintf(pwr_buf, sizeof(pwr_buf), "%+.0fW", data.battery_power_w);
    } else {
        snprintf(pwr_buf, sizeof(pwr_buf), "--W");
    }
    gfx.setTextColor(data.battery_power_valid ? C_WHITE : C_GREY, C_DIM);
    gfx.drawString(pwr_buf, BAT_X, BAT_Y + 10);
}

static bool energyFieldChanged(float prev, float next, bool valid) {
    if (!valid) return false;
    return prev < -900.0f || fabsf(prev - next) > 2.0f;
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

void energyFlowScreenDraw(LGFX& display, const EnergyData& energy, bool stale, unsigned long now_ms) {
    bool use_demo = false;
#if ENERGY_FLOW_DEMO_WHEN_STALE
    use_demo = stale || !energy.hasAny();
#endif

    EnergyData view = use_demo ? demoEnergy(now_ms) : energy;

    display.fillScreen(C_BG);
    display.drawCircle(CX, DISPLAY_HEIGHT / 2, 112, C_GREY);

    display.setTextSize(1);
    display.setTextDatum(TC_DATUM);
    display.setTextColor(C_WHITE, C_BG);
    display.drawString("Energy Flow", CX, 10);
    if (use_demo) {
        display.setTextColor(C_DEMO, C_BG);
        display.drawString("DEMO", CX, 22);
    }

    if (view.fixed_solar_valid && view.fixed_solar_w > ENERGY_FLOW_MIN_WATTS) {
        drawFlowLine(display, FIX_X, FIX_Y, BAT_X, BAT_Y, view.fixed_solar_w, C_SOLAR, now_ms);
    }
    if (view.portable_solar_valid && view.portable_solar_w > ENERGY_FLOW_MIN_WATTS) {
        drawFlowLine(display, PORT_X, PORT_Y, BAT_X, BAT_Y, view.portable_solar_w, C_SOLAR, now_ms);
    }
    if (view.battery_power_valid && view.battery_power_w < -ENERGY_FLOW_MIN_WATTS) {
        float dchg = -view.battery_power_w;
        if (view.boat_load_valid && view.boat_load_w > ENERGY_FLOW_MIN_WATTS) {
            drawFlowLine(display, BAT_X, BAT_Y, BOAT_X, BOAT_Y, view.boat_load_w, C_BATTERY_DCHG, now_ms);
        }
        if (view.fridge_load_valid && view.fridge_load_w > ENERGY_FLOW_MIN_WATTS) {
            drawFlowLine(display, BAT_X, BAT_Y, FRIDGE_X, FRIDGE_Y, view.fridge_load_w, C_BATTERY_DCHG,
                         now_ms + 400);
        }
        (void)dchg;
    } else {
        if (view.boat_load_valid && view.boat_load_w > ENERGY_FLOW_MIN_WATTS) {
            drawFlowLine(display, BAT_X, BAT_Y, BOAT_X, BOAT_Y, view.boat_load_w, C_LOAD, now_ms);
        }
        if (view.fridge_load_valid && view.fridge_load_w > ENERGY_FLOW_MIN_WATTS) {
            drawFlowLine(display, BAT_X, BAT_Y, FRIDGE_X, FRIDGE_Y, view.fridge_load_w, C_LOAD,
                         now_ms + 400);
        }
    }

    drawSourceNode(display, FIX_X, FIX_Y, "Fixed", view.fixed_solar_w, view.fixed_solar_valid);
    drawSourceNode(display, PORT_X, PORT_Y, "Port", view.portable_solar_w, view.portable_solar_valid);
    drawBatteryNode(display, view);
    drawLoadNode(display, BOAT_X, BOAT_Y, "Boat", view.boat_load_w, view.boat_load_valid);
    drawLoadNode(display, FRIDGE_X, FRIDGE_Y, "Fridge", view.fridge_load_w, view.fridge_load_valid);

    s_lastFixed = view.fixed_solar_w;
    s_lastPortable = view.portable_solar_w;
    s_lastBattery = view.battery_power_w;
    s_lastSoc = view.battery_soc_pct;
    s_lastBoat = view.boat_load_w;
    s_lastFridge = view.fridge_load_w;
    s_lastDemo = use_demo;
}
