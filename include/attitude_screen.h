#ifndef ATTITUDE_SCREEN_H
#define ATTITUDE_SCREEN_H

#include <Arduino.h>
#include "LGFX_config.h"
#include "attitude_data.h"
#include "user_screens.h"

void attitudeScreenDraw(LGFX& display, DisplayUpdateKind kind, const AttitudeData& attitude, bool stale,
                        unsigned long now_ms);
bool attitudeScreenDataChanged(const AttitudeData& attitude, bool stale);

#endif // ATTITUDE_SCREEN_H
