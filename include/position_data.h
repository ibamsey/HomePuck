#ifndef POSITION_DATA_H
#define POSITION_DATA_H

#include <Arduino.h>

// GPS fix from OpenPlotter (source priority: gps-ais, gps).
#define SK_PATH_NAVIGATION_POSITION "navigation.position"

struct PositionData {
    double latitude;
    double longitude;
    bool valid;
    unsigned long last_update;

    PositionData() { reset(); }

    void reset() {
        latitude = 0.0;
        longitude = 0.0;
        valid = false;
        last_update = 0;
    }
};

#endif // POSITION_DATA_H
