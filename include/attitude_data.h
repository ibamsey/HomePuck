#ifndef ATTITUDE_DATA_H
#define ATTITUDE_DATA_H

#include <Arduino.h>

// pypilot boat IMU → SignalK (OpenPlotter openplotter-pypilot-read / pypilot write access).
// pypilot publishes a composite object at navigation.attitude {pitch, roll, yaw} (radians).
#define SK_PATH_ATTITUDE       "navigation.attitude"
#define SK_PATH_ATTITUDE_PITCH "navigation.attitude.pitch"
#define SK_PATH_ATTITUDE_ROLL  "navigation.attitude.roll"
#define SK_PATH_ATTITUDE_YAW   "navigation.attitude.yaw"
// Fallback when yaw is not published on attitude branch:
#define SK_PATH_HEADING_YAW    "navigation.headingTrue"

struct AttitudeData {
    float pitch_deg;
    float roll_deg;
    float yaw_deg;

    bool pitch_valid;
    bool roll_valid;
    bool yaw_valid;

    unsigned long last_update;

    AttitudeData() { reset(); }

    void reset() {
        pitch_deg = 0.0f;
        roll_deg = 0.0f;
        yaw_deg = 0.0f;
        pitch_valid = false;
        roll_valid = false;
        yaw_valid = false;
        last_update = 0;
    }

    bool hasAny() const {
        return pitch_valid || roll_valid || yaw_valid;
    }
};

#endif // ATTITUDE_DATA_H
