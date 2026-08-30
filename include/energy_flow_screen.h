#ifndef ENERGY_FLOW_SCREEN_H
#define ENERGY_FLOW_SCREEN_H

#include <Arduino.h>
#include "LGFX_config.h"
#include "energy_data.h"
#include "user_screens.h"

void energyFlowScreenDraw(LGFX& display, DisplayUpdateKind kind, const EnergyData& energy, bool stale,
                          unsigned long now_ms);
bool energyFlowDataChanged(const EnergyData& energy, bool stale);

#endif // ENERGY_FLOW_SCREEN_H
