#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <Arduino.h>
#include "signalk_client.h"
#include "wind_widget.h"

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
    void showWindInstrument(const WindData& wind, bool stale, DisplayConnectionState connState);
    void toggleWindMode();
    bool isWindScreenActive() const { return _windScreenActive; }
    bool isShowingTws() const { return _windDisplay.show_tws; }

private:
    bool _ready = false;
    bool _windScreenActive = false;
    bool _forceWindRedraw = false;
    DisplayConnectionState _lastConnState = DISPLAY_CONN_BOOT;
    WindWidget _windWidget;
    WindWidgetData _windDisplay;
    void drawHeader(const char* title, uint16_t color);
    void drawConnectionBadge(DisplayConnectionState connState);
    bool windDisplayChanged(const WindWidgetData& prev, const WindWidgetData& next,
                            DisplayConnectionState prevConn, DisplayConnectionState nextConn);
};

extern DisplayManager displayManager;

#endif // DISPLAY_MANAGER_H
