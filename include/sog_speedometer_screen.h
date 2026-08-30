#ifndef SOG_SPEEDOMETER_SCREEN_H
#define SOG_SPEEDOMETER_SCREEN_H

#include <Arduino.h>
#include "LGFX_config.h"
#include "signalk_client.h"
#include "user_screens.h"

#define SOG_SPEEDO_MAX_KTS 15.0f
#define SOG_SPEEDO_PEAK_TAU_SEC 10.0f

void sogSpeedometerScreenDraw(LGFX& display, DisplayUpdateKind kind, const WindData& wind, bool stale,
                              unsigned long now_ms);
bool sogSpeedometerDataChanged(const WindData& wind, bool stale);

#endif // SOG_SPEEDOMETER_SCREEN_H
