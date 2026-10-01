#ifndef BME_ENV_SCREEN_H
#define BME_ENV_SCREEN_H

#include <Arduino.h>
#include "LGFX_config.h"
#include "environment_data.h"
#include "user_screens.h"

// Annular pressure gauge span (hPa / mbar).
#define BME_PRESSURE_MIN_HPA 960.0f
#define BME_PRESSURE_MAX_HPA 1060.0f
// Typical fair-weather band highlighted on the ring.
#define BME_PRESSURE_NORMAL_LOW_HPA  1000.0f
#define BME_PRESSURE_NORMAL_HIGH_HPA 1025.0f

void bmeEnvScreenDraw(LGFX& display, DisplayUpdateKind kind, const EnvironmentData& env, bool stale,
                      unsigned long now_ms);
bool bmeEnvScreenDataChanged(const EnvironmentData& env, bool stale);

#endif // BME_ENV_SCREEN_H
