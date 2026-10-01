#include "user_screens.h"
#include "config.h"
#include "wind_widget.h"
#include "sog_speedometer_screen.h"
#include "energy_flow_screen.h"
#include "bme_env_screen.h"
#include "attitude_screen.h"
#include "anchor_watch_screen.h"
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
                           const EnergyData& energy, bool energy_stale,
                           const EnvironmentData& environment, bool environment_stale,
                           const AttitudeData& attitude, bool attitude_stale,
                           const PositionData& position, bool position_stale,
                           uint8_t variation, unsigned long now_ms) {
    switch (mode) {
        case USER_SCREEN_WIND:
            return windScreenDataChanged(wind, wind_stale, variation);
        case USER_SCREEN_SOG:
            return sogSpeedometerDataChanged(wind, wind_stale);
        case USER_SCREEN_ENERGY:
            return energyFlowDataChanged(energy, energy_stale, now_ms);
        case USER_SCREEN_BME:
            return bmeEnvScreenDataChanged(environment, environment_stale);
        case USER_SCREEN_ATTITUDE:
            return attitudeScreenDataChanged(attitude, attitude_stale);
        case USER_SCREEN_ANCHOR:
            return anchorWatchScreenDataChanged(position, position_stale);
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

static void drawBmeEnv(const UserScreenDrawContext& ctx) {
    if (!ctx.environment) return;
    bmeEnvScreenDraw(ctx.display, ctx.update_kind, *ctx.environment, ctx.environment_stale, ctx.now_ms);
}

static void drawAttitude(const UserScreenDrawContext& ctx) {
    if (!ctx.attitude) return;
    attitudeScreenDraw(ctx.display, ctx.update_kind, *ctx.attitude, ctx.attitude_stale, ctx.now_ms);
}

static void drawAnchorWatch(const UserScreenDrawContext& ctx) {
    if (!ctx.position) return;
    anchorWatchScreenDraw(ctx.display, ctx.update_kind, *ctx.position, ctx.position_stale, ctx.now_ms);
}

static const UserScreenDescriptor kUserScreens[] = {
    { USER_SCREEN_WIND,     "Wind",     USER_SCREEN_WIND_VARIATIONS,     true,  false, false, false, false, false, drawWindScreen },
    { USER_SCREEN_SOG,      "SOG",      USER_SCREEN_SOG_VARIATIONS,      true,  false, false, false, false, false, drawSogSpeedometer },
    { USER_SCREEN_ENERGY,   "Energy",   USER_SCREEN_ENERGY_VARIATIONS,   false, true,  false, false, false, false, drawEnergyFlow },
    { USER_SCREEN_BME,      "BME",      USER_SCREEN_BME_VARIATIONS,      false, false, true,  false, false, false, drawBmeEnv },
    { USER_SCREEN_ATTITUDE, "Attitude", USER_SCREEN_ATTITUDE_VARIATIONS, false, false, false, true,  false, false, drawAttitude },
    { USER_SCREEN_ANCHOR,   "Anchor",   USER_SCREEN_ANCHOR_VARIATIONS, false, false, false, false, true,  true,  drawAnchorWatch },
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
