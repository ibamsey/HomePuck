#ifndef USER_SCREENS_H
#define USER_SCREENS_H

#include <Arduino.h>
#include "LGFX_config.h"
#include "signalk_client.h"

enum UserScreenMode : uint8_t {
    USER_SCREEN_WIND = 0,
    USER_SCREEN_SOG,
    USER_SCREEN_PLACEHOLDER_B,
    USER_SCREEN_COUNT
};

struct UserScreenDrawContext {
    LGFX& display;
    const WindData* wind;
    bool wind_stale;
    uint8_t variation;
    unsigned long now_ms;
};

using UserScreenDrawFn = void (*)(const UserScreenDrawContext& ctx);

struct UserScreenDescriptor {
    UserScreenMode mode;
    const char* debug_name;
    uint8_t max_variations;
    bool needs_wind_data;
    UserScreenDrawFn draw;
};

struct UserScreenNav {
    UserScreenMode mode = USER_SCREEN_WIND;
    uint8_t variation[USER_SCREEN_COUNT] = {0};

    void cycleMode(int delta);
    void cycleVariation(int delta);
    const UserScreenDescriptor* currentDescriptor() const;
};

const UserScreenDescriptor* userScreenDescriptor(UserScreenMode mode);
uint8_t userScreenMaxVariations(UserScreenMode mode);
bool userScreenDataChanged(UserScreenMode mode, const WindData& wind, bool stale, uint8_t variation);

#endif // USER_SCREENS_H
