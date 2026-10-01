#ifndef ENVIRONMENT_DATA_H
#define ENVIRONMENT_DATA_H

#include <Arduino.h>

// BMP280 on OpenPlotter I2C @ 0x76 (see openplotter/README.md).
#define SK_PATH_OUTSIDE_PRESSURE  "environment.outside.pressure"
#define SK_PATH_CABIN_TEMPERATURE "environment.inside.cabin.temperature"

struct EnvironmentData {
    float temperature_c;
    float pressure_hpa;

    bool temperature_valid;
    bool pressure_valid;

    unsigned long last_update;

    EnvironmentData() { reset(); }

    void reset() {
        temperature_c = 0.0f;
        pressure_hpa = 0.0f;
        temperature_valid = false;
        pressure_valid = false;
        last_update = 0;
    }

    bool hasAny() const {
        return temperature_valid || pressure_valid;
    }
};

#endif // ENVIRONMENT_DATA_H
