#ifndef BATTERY_H
#define BATTERY_H

#include <Arduino.h>
#include "mqtt_client.h"

// ── MAX17048 Battery Monitor ───────────────────────────────────
void setup_battery();         // Initialize MAX17048, returns false on error
void publish_batteryStatus(mqtt_controller mqtt);
void run_battery(mqtt_controller mqtt);

#endif // BATTERY_H