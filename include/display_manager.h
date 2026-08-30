#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <Arduino.h>
#include "signalk_client.h"
#include "user_screens.h"

enum DisplayConnectionState {
    DISPLAY_CONN_BOOT,
    DISPLAY_CONN_WIFI_CONNECTING,
    DISPLAY_CONN_WIFI_OK,
    DISPLAY_CONN_SIGNALK_CONNECTING,
    DISPLAY_CONN_SIGNALK_OK,
    DISPLAY_CONN_SIGNALK_LOST,
    DISPLAY_CONN_WIFI_LOST
};

class DisplayManager {
public:
    void begin();
    void showStatus(DisplayConnectionState state, const char* detail = "");
    void updateUserScreen(const WindData& wind, bool wind_stale,
                          const EnergyData& energy, bool energy_stale,
                          DisplayConnectionState connState);
    void navigateMode(int delta);
    void navigateVariation(int delta);
    bool isUserScreenActive() const { return _userScreenActive; }
    UserScreenMode getScreenMode() const { return _nav.mode; }
    uint8_t getVariation() const { return _nav.variation[_nav.mode]; }
    const char* getScreenDebugName() const;

private:
    bool _ready = false;
    bool _userScreenActive = false;
    bool _forceRedraw = false;
    UserScreenNav _nav;
    UserScreenMode _lastMode = USER_SCREEN_WIND;
    uint8_t _lastVariation = 0;
    DisplayConnectionState _lastConnState = DISPLAY_CONN_BOOT;
    void drawHeader(const char* title, uint16_t color);
    void drawConnectionBadge(DisplayConnectionState connState);
    bool userScreenNeedsRedraw(const UserScreenDescriptor* desc, const WindData& wind, bool wind_stale,
                               const EnergyData& energy, bool energy_stale,
                               DisplayConnectionState connState);
};

extern DisplayManager displayManager;

#endif // DISPLAY_MANAGER_H
