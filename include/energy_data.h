#ifndef ENERGY_DATA_H
#define ENERGY_DATA_H

#include <Arduino.h>

// SignalK paths — wired when Victron A/B and Ecoworthy BMS feeds are live on OpenPlotter.
// Victron A: fixed roof panels + house/boat load shunt
#define SK_PATH_VICTRON_A_SOLAR "electrical.solar.0.power"
#define SK_PATH_VICTRON_A_LOAD  "electrical.consumption.0.power"
// Victron B: portable panel controller + fridge circuit
#define SK_PATH_VICTRON_B_SOLAR "electrical.solar.1.power"
#define SK_PATH_VICTRON_B_LOAD  "electrical.consumption.1.power"
// Ecoworthy 100 Ah LiFePO4 via Bluetooth → SignalK battery plugin
#define SK_PATH_BATTERY_POWER   "electrical.batteries.0.power"
#define SK_PATH_BATTERY_SOC     "electrical.batteries.0.capacity.stateOfCharge"

struct EnergyData {
    float fixed_solar_w;
    float portable_solar_w;
    float battery_power_w;   // + charge, − discharge (SignalK convention)
    float battery_soc_pct;   // 0–100 for display
    float boat_load_w;
    float fridge_load_w;

    bool fixed_solar_valid;
    bool portable_solar_valid;
    bool battery_power_valid;
    bool battery_soc_valid;
    bool boat_load_valid;
    bool fridge_load_valid;

    unsigned long last_update;

    EnergyData() { reset(); }

    void reset() {
        fixed_solar_w = 0.0f;
        portable_solar_w = 0.0f;
        battery_power_w = 0.0f;
        battery_soc_pct = 0.0f;
        boat_load_w = 0.0f;
        fridge_load_w = 0.0f;
        fixed_solar_valid = false;
        portable_solar_valid = false;
        battery_power_valid = false;
        battery_soc_valid = false;
        boat_load_valid = false;
        fridge_load_valid = false;
        last_update = 0;
    }

    bool hasAny() const {
        return fixed_solar_valid || portable_solar_valid || battery_power_valid ||
               battery_soc_valid || boat_load_valid || fridge_load_valid;
    }
};

#endif // ENERGY_DATA_H
