#include "user_screens.h"
#include "config.h"
#include "wind_widget.h"
#include "sog_speedometer_screen.h"
#include "energy_flow_screen.h"
#include <math.h>

static WindWidget s_windWidget;
static WindWidgetData s_windDisplay;

static WindWidgetData buildWindWidgetData(const WindData& wind, bool stale, uint8_t variation) {
    WindWidgetData next;
    next.show_tws = (variation == WIND_VAR_TWS);
    next.awa_degrees = wind.awa_degrees;
    next.twd_degrees = wind.twdDialDegrees();
    next.aws_knots = wind.aws_knots;
    next.tws_knots = wind.tws_knots;
    next.awa_valid = wind.awa_valid && !stale;
    next.twd_valid = wind.twd_valid && !stale;
    next.speed_valid = (next.show_tws ? wind.tws_valid : wind.aws_valid) && !stale;
    return next;
}

static bool windWidgetDataChanged(const WindWidgetData& prev, const WindWidgetData& next) {
    if (prev.show_tws != next.show_tws) return true;
    if (prev.awa_valid != next.awa_valid || prev.twd_valid != next.twd_valid ||
        prev.speed_valid != next.speed_valid) {
        return true;
    }
    if (prev.awa_valid && fabsf(prev.awa_degrees - next.awa_degrees) > 0.5f) return true;
    if (prev.twd_valid && fabsf(prev.twd_degrees - next.twd_degrees) > 0.5f) return true;
    if (prev.speed_valid) {
        float prevSpeed = prev.show_tws ? prev.tws_knots : prev.aws_knots;
        float nextSpeed = next.show_tws ? next.tws_knots : next.aws_knots;
        if (fabsf(prevSpeed - nextSpeed) > 0.05f) return true;
    }
    return false;
}

static bool windScreenDataChanged(const WindData& wind, bool stale, uint8_t variation) {
    WindWidgetData next = buildWindWidgetData(wind, stale, variation);
    return windWidgetDataChanged(s_windDisplay, next);
}

bool userScreenDataChanged(UserScreenMode mode, const WindData& wind, bool wind_stale,
                           const EnergyData& energy, bool energy_stale, uint8_t variation) {
    switch (mode) {
        case USER_SCREEN_WIND:
            return windScreenDataChanged(wind, wind_stale, variation);
        case USER_SCREEN_SOG:
            return sogSpeedometerDataChanged(wind, wind_stale);
        case USER_SCREEN_ENERGY:
            return energyFlowDataChanged(energy, energy_stale);
        default:
            return false;
    }
}

static void drawWindScreen(const UserScreenDrawContext& ctx) {
    if (!ctx.wind) return;

    WindWidgetData next = buildWindWidgetData(*ctx.wind, ctx.wind_stale, ctx.variation);
    s_windDisplay = next;
    s_windWidget.draw(ctx.display, ctx.update_kind, s_windDisplay);
}

static void drawSogSpeedometer(const UserScreenDrawContext& ctx) {
    if (!ctx.wind) return;
    sogSpeedometerScreenDraw(ctx.display, ctx.update_kind, *ctx.wind, ctx.wind_stale, ctx.now_ms);
}

static void drawEnergyFlow(const UserScreenDrawContext& ctx) {
    if (!ctx.energy) return;
    energyFlowScreenDraw(ctx.display, ctx.update_kind, *ctx.energy, ctx.energy_stale, ctx.now_ms);
}

static const UserScreenDescriptor kUserScreens[] = {
    { USER_SCREEN_WIND,   "Wind",   USER_SCREEN_WIND_VARIATIONS,   true,  false, drawWindScreen },
    { USER_SCREEN_SOG,    "SOG",    USER_SCREEN_SOG_VARIATIONS,    true,  false, drawSogSpeedometer },
    { USER_SCREEN_ENERGY, "Energy", USER_SCREEN_ENERGY_VARIATIONS, false, true,  drawEnergyFlow },
};

static const size_t kUserScreenCount = sizeof(kUserScreens) / sizeof(kUserScreens[0]);

const UserScreenDescriptor* userScreenDescriptor(UserScreenMode mode) {
    for (size_t i = 0; i < kUserScreenCount; i++) {
        if (kUserScreens[i].mode == mode) {
            return &kUserScreens[i];
        }
    }
    return nullptr;
}

uint8_t userScreenMaxVariations(UserScreenMode mode) {
    const UserScreenDescriptor* desc = userScreenDescriptor(mode);
    return desc ? desc->max_variations : 1;
}

void UserScreenNav::cycleMode(int delta) {
    int next = (int)mode + delta;
    while (next < 0) {
        next += USER_SCREEN_COUNT;
    }
    mode = static_cast<UserScreenMode>(next % USER_SCREEN_COUNT);
}

void UserScreenNav::cycleVariation(int delta) {
    const UserScreenDescriptor* desc = currentDescriptor();
    if (!desc || desc->max_variations <= 1) return;

    int next = (int)variation[mode] + delta;
    uint8_t max_var = desc->max_variations;
    while (next < 0) {
        next += max_var;
    }
    variation[mode] = static_cast<uint8_t>(next % max_var);
}

const UserScreenDescriptor* UserScreenNav::currentDescriptor() const {
    return userScreenDescriptor(mode);
}
