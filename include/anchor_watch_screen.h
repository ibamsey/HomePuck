#ifndef ANCHOR_WATCH_SCREEN_H
#define ANCHOR_WATCH_SCREEN_H

#include <Arduino.h>
#include "LGFX_config.h"
#include "position_data.h"
#include "user_screens.h"

#define ANCHOR_RING_SPACING_M   25.0f
#define ANCHOR_MAX_RADIUS_M    250.0f
#define ANCHOR_RING_COUNT       10
#define ANCHOR_TRACE_MAX_POINTS 120
#define ANCHOR_TRACE_MIN_STEP_M  0.8f

void anchorWatchScreenDraw(LGFX& display, DisplayUpdateKind kind, const PositionData& position, bool stale,
                           unsigned long now_ms);
bool anchorWatchScreenDataChanged(const PositionData& position, bool stale);
bool anchorWatchScreenZero(const PositionData& position);

#endif // ANCHOR_WATCH_SCREEN_H
