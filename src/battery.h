#ifndef BATTERY_H
#define BATTERY_H

#include <Arduino.h>
#include "mqtt_client.h"

// ── MAX17048 Battery Monitor ───────────────────────────────────
/**
 * @brief Initialize MAX17048 fuel gauge over I2C.
 * @details
 * Probes the device and stores monitor availability for later reads.
 */
void setup_battery();
/**
 * @brief Publish whether the battery monitor is active.
 * @param[in,out] mqtt MQTT controller used for publishing.
 */
void publish_batteryStatus(mqtt_controller& mqtt);
/**
 * @brief Publish the current battery percentage immediately.
 * @param[in,out] mqtt MQTT controller used for publishing.
 * @param[in] retained When true, publish as retained message.
 * @retval true Percentage was read and published.
 * @retval false Percentage was unavailable and not published.
 */
bool publish_batteryPercentNow(mqtt_controller& mqtt, bool retained = false);
/**
 * @brief Read and publish battery telemetry with threshold-based throttling.
 * @param[in,out] mqtt MQTT controller used for publishing.
 * @details
 * Values are published when a configured delta is exceeded or when a periodic
 * heartbeat interval elapses.
 */
void run_battery(mqtt_controller& mqtt);
/**
 * @brief Read battery telemetry and print values to serial.
 * @param[in,out] mqtt Unused, kept for API symmetry.
 */
void read_battery(mqtt_controller& mqtt);
#endif // BATTERY_H